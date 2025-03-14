/*
 * Copyright 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#define LOG_CLASS "SocketConnection"

#include "socket_connection.h"
#include "ice_agent.h"
#include <netdb.h>

/// internal function prototype
STATUS socket_connection_sendWithRetry(PSocketConnection pSocketConnection, PBYTE buf, UINT32 bufLen, PKvsIpAddress pDestIp, PUINT32 pBytesWritten);

STATUS socket_connection_create(KVS_IP_FAMILY_TYPE familyType, KVS_SOCKET_PROTOCOL protocol, PKvsIpAddress pBindAddr, PKvsIpAddress pPeerIpAddr,
                                UINT64 customData, ConnectionDataAvailableFunc dataAvailableFn, UINT32 sendBufSize,
                                PSocketConnection* ppSocketConnection)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSocketConnection pSocketConnection = NULL;

    CHK(ppSocketConnection != NULL, STATUS_SOCKET_CONN_NULL_ARG);
    CHK(protocol == KVS_SOCKET_PROTOCOL_UDP || pPeerIpAddr != NULL, STATUS_SOCKET_CONN_INVALID_ARG);

    pSocketConnection = (PSocketConnection) MEMCALLOC(1, SIZEOF(SocketConnection));
    CHK(pSocketConnection != NULL, STATUS_SOCKET_CONN_NOT_ENOUGH_MEMORY);

    pSocketConnection->lock = MUTEX_CREATE(FALSE);
    CHK(pSocketConnection->lock != INVALID_MUTEX_VALUE, STATUS_SOCKET_CONN_INVALID_OPERATION);

    CHK_STATUS(net_createSocket(familyType, protocol, sendBufSize, &pSocketConnection->localSocket));
    if (pBindAddr) {
        CHK_STATUS(net_bindSocket(pBindAddr, pSocketConnection->localSocket));
        pSocketConnection->hostIpAddr = *pBindAddr;
    }

    pSocketConnection->bTlsSession = FALSE;
    pSocketConnection->protocol = protocol;
    if (protocol == KVS_SOCKET_PROTOCOL_TCP) {
        pSocketConnection->peerIpAddr = *pPeerIpAddr;
        CHK_STATUS(net_connectSocket(pPeerIpAddr, pSocketConnection->localSocket));
    }
    ATOMIC_STORE_BOOL(&pSocketConnection->connectionClosed, FALSE);
    ATOMIC_STORE_BOOL(&pSocketConnection->receiveData, FALSE);
    ATOMIC_STORE_BOOL(&pSocketConnection->inUse, FALSE);
    pSocketConnection->dataAvailableCallbackCustomData = customData;
    pSocketConnection->dataAvailableCallbackFn = dataAvailableFn;

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (STATUS_FAILED(retStatus) && pSocketConnection != NULL) {
        socket_connection_free(&pSocketConnection);
        pSocketConnection = NULL;
    }

    if (ppSocketConnection != NULL) {
        *ppSocketConnection = pSocketConnection;
    }

    LEAVES();
    return retStatus;
}

STATUS socket_connection_free(PSocketConnection* ppSocketConnection)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSocketConnection pSocketConnection = NULL;
    UINT64 shutdownTimeout;

    CHK(ppSocketConnection != NULL, STATUS_SOCKET_CONN_NULL_ARG);
    pSocketConnection = *ppSocketConnection;
    CHK(pSocketConnection != NULL, retStatus);
    ATOMIC_STORE_BOOL(&pSocketConnection->connectionClosed, TRUE);

    // Await for the socket connection to be released
    shutdownTimeout = GETTIME() + KVS_ICE_TURN_CONNECTION_SHUTDOWN_TIMEOUT;
    while (ATOMIC_LOAD_BOOL(&pSocketConnection->inUse) && GETTIME() < shutdownTimeout) {
        THREAD_SLEEP(KVS_ICE_SHORT_CHECK_DELAY);
    }

    if (ATOMIC_LOAD_BOOL(&pSocketConnection->inUse)) {
        DLOGW("Shutting down socket connection timedout after %" PRIu64 " seconds",
              KVS_ICE_TURN_CONNECTION_SHUTDOWN_TIMEOUT / HUNDREDS_OF_NANOS_IN_A_SECOND);
    }

    if (IS_VALID_MUTEX_VALUE(pSocketConnection->lock)) {
        MUTEX_FREE(pSocketConnection->lock);
        pSocketConnection->lock = INVALID_MUTEX_VALUE;
    }

    if (pSocketConnection->pTlsSession != NULL) {
        tls_session_free(&pSocketConnection->pTlsSession);
    }

    if (STATUS_FAILED(retStatus = net_closeSocket(pSocketConnection->localSocket))) {
        DLOGW("Failed to close the local socket with 0x%08x", retStatus);
    }
    MEMFREE(pSocketConnection);

    *ppSocketConnection = NULL;

CleanUp:

    LEAVES();
    return retStatus;
}
/**
 * @brief
 *
 * @param[in] customData the user data.
 * @param[in] pBuffer the buffer address.
 * @param[in] bufferLen the length of the buffer.
 *
 * @return STATUS - status of operation
 */
static STATUS socket_connection_tlsSessionOutboundPacket(UINT64 customData, PBYTE pBuffer, UINT32 bufferLen)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSocketConnection pSocketConnection = NULL;
    CHK(customData != 0, STATUS_SOCKET_CONN_NULL_ARG);

    pSocketConnection = (PSocketConnection) customData;
    CHK_STATUS(socket_connection_sendWithRetry(pSocketConnection, pBuffer, bufferLen, NULL, NULL));

CleanUp:
    return retStatus;
}

VOID socket_connection_tlsSessionOnStateChange(UINT64 customData, TLS_SESSION_STATE state)
{
    PSocketConnection pSocketConnection = NULL;
    if (customData == 0) {
        return;
    }

    pSocketConnection = (PSocketConnection) customData;
    switch (state) {
        case TLS_SESSION_STATE_NEW:
            pSocketConnection->tlsHandshakeStartTime = INVALID_TIMESTAMP_VALUE;
            break;
        case TLS_SESSION_STATE_CONNECTING:
            pSocketConnection->tlsHandshakeStartTime = GETTIME();
            break;
        case TLS_SESSION_STATE_CONNECTED:
            if (IS_VALID_TIMESTAMP(pSocketConnection->tlsHandshakeStartTime)) {
                DLOGD("TLS handshake done. Time taken %" PRIu64 " ms",
                      (GETTIME() - pSocketConnection->tlsHandshakeStartTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
                pSocketConnection->tlsHandshakeStartTime = INVALID_TIMESTAMP_VALUE;
            }
            break;
        case TLS_SESSION_STATE_CLOSED:
            ATOMIC_STORE_BOOL(&pSocketConnection->connectionClosed, TRUE);
            break;
    }
}

STATUS socket_connection_initSecureConnection(PSocketConnection pSocketConnection, BOOL isServer)
{
    ENTERS();
    TlsSessionCallbacks callbacks;
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pSocketConnection != NULL, STATUS_SOCKET_CONN_NULL_ARG);
    CHK(pSocketConnection->pTlsSession == NULL, STATUS_SOCKET_CONN_INVALID_ARG);

    callbacks.tlsOutBoundPacketFnCustomData = callbacks.tlsStateChangeFnCustomData = (UINT64) pSocketConnection;
    callbacks.tlsOutboundPacketFn = socket_connection_tlsSessionOutboundPacket;
    callbacks.tlsStateChangeFn = socket_connection_tlsSessionOnStateChange;

    CHK_STATUS(tls_session_create(&callbacks, &pSocketConnection->pTlsSession));
    CHK_STATUS(tls_session_start(pSocketConnection->pTlsSession, isServer));
    pSocketConnection->bTlsSession = TRUE;

CleanUp:
    if (STATUS_FAILED(retStatus) && pSocketConnection->pTlsSession != NULL) {
        tls_session_free(&pSocketConnection->pTlsSession);
    }

    LEAVES();
    return retStatus;
}

STATUS socket_connection_send(PSocketConnection pSocketConnection, PBYTE pBuf, UINT32 bufLen, PKvsIpAddress pDestIp)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;

    CHK(pSocketConnection != NULL, STATUS_SOCKET_CONN_NULL_ARG);
    CHK((pSocketConnection->protocol == KVS_SOCKET_PROTOCOL_TCP || pDestIp != NULL), STATUS_SOCKET_CONN_INVALID_ARG);

    // Using a single CHK_WARN might output too much spew in bad network conditions
    if (ATOMIC_LOAD_BOOL(&pSocketConnection->connectionClosed)) {
        DLOGE("Warning: Failed to send data. Socket closed already");
        CHK(FALSE, STATUS_SOCKET_CONN_CLOSED_ALREADY);
    }

    MUTEX_LOCK(pSocketConnection->lock);
    locked = TRUE;

    /* Should have a valid buffer */
    CHK(pBuf != NULL && bufLen > 0, STATUS_SOCKET_CONN_INVALID_ARG);
    if (pSocketConnection->protocol == KVS_SOCKET_PROTOCOL_TCP && pSocketConnection->bTlsSession) {
        CHK_STATUS(tls_session_send(pSocketConnection->pTlsSession, pBuf, bufLen));
    } else if (pSocketConnection->protocol == KVS_SOCKET_PROTOCOL_TCP) {
        CHK_STATUS(retStatus = socket_connection_sendWithRetry(pSocketConnection, pBuf, bufLen, NULL, NULL));
    } else if (pSocketConnection->protocol == KVS_SOCKET_PROTOCOL_UDP) {
        CHK_STATUS(retStatus = socket_connection_sendWithRetry(pSocketConnection, pBuf, bufLen, pDestIp, NULL));
    } else {
        CHECK_EXT(FALSE, "socket_connection_send should not reach here. Nothing is sent.");
    }

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pSocketConnection->lock);
    }

    return retStatus;
}

STATUS socket_connection_read(PSocketConnection pSocketConnection, PBYTE pBuf, UINT32 bufferLen, PUINT32 pDataLen)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;

    CHK(pSocketConnection != NULL && pBuf != NULL && pDataLen != NULL, STATUS_SOCKET_CONN_NULL_ARG);
    CHK(bufferLen != 0, STATUS_SOCKET_CONN_INVALID_ARG);

    MUTEX_LOCK(pSocketConnection->lock);
    locked = TRUE;

    // return early if connection is not secure
    CHK(pSocketConnection->bTlsSession, retStatus);

    CHK_STATUS(tls_session_read(pSocketConnection->pTlsSession, pBuf, bufferLen, pDataLen));

CleanUp:

    // CHK_LOG_ERR might be too verbose
    if (STATUS_FAILED(retStatus)) {
        DLOGD("Warning: reading socket data failed with 0x%08x", retStatus);
    }

    if (locked) {
        MUTEX_UNLOCK(pSocketConnection->lock);
    }

    return retStatus;
}

STATUS socket_connection_close(PSocketConnection pSocketConnection)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pSocketConnection != NULL, STATUS_SOCKET_CONN_NULL_ARG);
    CHK(!ATOMIC_LOAD_BOOL(&pSocketConnection->connectionClosed), retStatus);

    MUTEX_LOCK(pSocketConnection->lock);
    DLOGD("Close socket %d", pSocketConnection->localSocket);
    ATOMIC_STORE_BOOL(&pSocketConnection->connectionClosed, TRUE);
    if (pSocketConnection->pTlsSession != NULL) {
        tls_session_shutdown(pSocketConnection->pTlsSession);
    }
    MUTEX_UNLOCK(pSocketConnection->lock);

CleanUp:

    CHK_LOG_ERR(retStatus);

    return retStatus;
}

BOOL socket_connection_isClosed(PSocketConnection pSocketConnection)
{
    if (pSocketConnection == NULL) {
        return TRUE;
    } else {
        return ATOMIC_LOAD_BOOL(&pSocketConnection->connectionClosed);
    }
}

BOOL socket_connection_isConnected(PSocketConnection pSocketConnection)
{
    INT32 retVal;
    struct sockaddr* peerSockAddr = NULL;
    socklen_t addrLen;
    struct sockaddr_in ipv4PeerAddr;
    struct sockaddr_in6 ipv6PeerAddr;

    CHECK(pSocketConnection != NULL);

    if (pSocketConnection->protocol == KVS_SOCKET_PROTOCOL_UDP) {
        return TRUE;
    }

    if (pSocketConnection->peerIpAddr.family == KVS_IP_FAMILY_TYPE_IPV4) {
        addrLen = SIZEOF(struct sockaddr_in);
        MEMSET(&ipv4PeerAddr, 0x00, SIZEOF(ipv4PeerAddr));
        ipv4PeerAddr.sin_family = AF_INET;
        ipv4PeerAddr.sin_port = pSocketConnection->peerIpAddr.port;
        MEMCPY(&ipv4PeerAddr.sin_addr, pSocketConnection->peerIpAddr.address, IPV4_ADDRESS_LENGTH);
        peerSockAddr = (struct sockaddr*) &ipv4PeerAddr;
    } else {
        addrLen = SIZEOF(struct sockaddr_in6);
        MEMSET(&ipv6PeerAddr, 0x00, SIZEOF(ipv6PeerAddr));
        ipv6PeerAddr.sin6_family = AF_INET6;
        ipv6PeerAddr.sin6_port = pSocketConnection->peerIpAddr.port;
        MEMCPY(&ipv6PeerAddr.sin6_addr, pSocketConnection->peerIpAddr.address, IPV6_ADDRESS_LENGTH);
        peerSockAddr = (struct sockaddr*) &ipv6PeerAddr;
    }

    retVal = connect(pSocketConnection->localSocket, peerSockAddr, addrLen);
    if (retVal == 0 || net_getErrorCode() == EISCONN) {
        if (net_getErrorCode() == EISCONN) {
            DLOGD("this socket is already connected.");
        }
        return TRUE;
    }

    DLOGW("socket connection check failed with errno %s(%d)", net_getErrorString(net_getErrorCode()), net_getErrorCode());
    return FALSE;
}
/**
 * @brief send the data to socket layer.
 *
 * @param[in] pSocketConnection the context of the socket.
 * @param[in] buf the pointer of the send buffer.
 * @param[in] bufLen the lenght of the send buffer.
 * @param[in] pDestIp the ip address of destion.
 * @param[in] pBytesWritten the bytes written.
 *
 * @return STATUS status of execution.
 */
STATUS socket_connection_sendWithRetry(PSocketConnection pSocketConnection, PBYTE buf, UINT32 bufLen, PKvsIpAddress pDestIp, PUINT32 pBytesWritten)
{
    STATUS retStatus = STATUS_SUCCESS;
    INT32 socketWriteAttempt = 0;
    SSIZE_T socketResult = 0;
    UINT32 bytesWritten = 0;
    INT32 errorNum = 0;

    fd_set wfds;
    struct timeval tv;
    socklen_t addrLen = 0;
    struct sockaddr* destAddr = NULL;
    struct sockaddr_in* pIpv4Addr = NULL;
    struct sockaddr_in6* pIpv6Addr = NULL;

    CHK(pSocketConnection != NULL, STATUS_SOCKET_CONN_NULL_ARG);
    CHK(buf != NULL && bufLen > 0, STATUS_SOCKET_CONN_INVALID_ARG);

    if (pDestIp != NULL) {
        if (IS_IPV4_ADDR(pDestIp)) {
            CHK(NULL != (pIpv4Addr = (struct sockaddr_in*) MEMALLOC(SIZEOF(struct sockaddr_in))), STATUS_SOCKET_CONN_NOT_ENOUGH_MEMORY);
            addrLen = SIZEOF(struct sockaddr_in);
            MEMSET(pIpv4Addr, 0x00, SIZEOF(struct sockaddr_in));
            pIpv4Addr->sin_family = AF_INET;
            pIpv4Addr->sin_port = pDestIp->port;
            MEMCPY(&pIpv4Addr->sin_addr, pDestIp->address, IPV4_ADDRESS_LENGTH);
            destAddr = (struct sockaddr*) pIpv4Addr;

        } else {
            CHK(NULL != (pIpv6Addr = (struct sockaddr_in6*) MEMALLOC(SIZEOF(struct sockaddr_in6))), STATUS_SOCKET_CONN_NOT_ENOUGH_MEMORY);
            addrLen = SIZEOF(struct sockaddr_in6);
            MEMSET(pIpv6Addr, 0x00, SIZEOF(struct sockaddr_in6));
            pIpv6Addr->sin6_family = AF_INET6;
            pIpv6Addr->sin6_port = pDestIp->port;
            MEMCPY(&pIpv6Addr->sin6_addr, pDestIp->address, IPV6_ADDRESS_LENGTH);
            destAddr = (struct sockaddr*) pIpv6Addr;
        }
    }
    // start sending the data.
    while (socketWriteAttempt < MAX_SOCKET_WRITE_RETRY && bytesWritten < bufLen) {
        socketResult = sendto(pSocketConnection->localSocket, buf + bytesWritten, bufLen - bytesWritten, NO_SIGNAL, destAddr, addrLen);
        if (socketResult < 0) {
            errorNum = net_getErrorCode();
            if (errorNum == EAGAIN || errorNum == EWOULDBLOCK) {
                FD_ZERO(&wfds);
                FD_SET(pSocketConnection->localSocket, &wfds);
                tv.tv_sec = 0;
                tv.tv_usec = SOCKET_SEND_RETRY_TIMEOUT_MICRO_SECOND;
                socketResult = select(pSocketConnection->localSocket + 1, NULL, &wfds, NULL, &tv);

                if (socketResult == 0) {
                    /* loop back and try again */
                    DLOGE("select() timed out");
                } else if (socketResult < 0) {
                    DLOGE("select() failed with errno %s", net_getErrorString(net_getErrorCode()));
                    break;
                }
            } else if (errorNum == EINTR) {
                /* nothing need to be done, just retry */
            } else {
                /* fatal error from send() */
                DLOGE("sendto() failed with errno %s", net_getErrorString(errorNum));
                break;
            }

            // Indicate an attempt only on error
            socketWriteAttempt++;
        } else {
            bytesWritten += socketResult;
        }
        if (socketWriteAttempt > 1) {
            DLOGD("sendto retry: %d/%d", socketWriteAttempt, MAX_SOCKET_WRITE_RETRY);
        }
    }

    if (pBytesWritten != NULL) {
        *pBytesWritten = bytesWritten;
    }

    if (socketResult < 0) {
        DLOGE("fail to send data and close the socket.");
        CLOSE_SOCKET_IF_CANT_RETRY(errorNum, pSocketConnection);
    }

    if (bytesWritten < bufLen) {
        DLOGE("Failed to send data. Bytes sent %u. Data len %u. Retry count %u", bytesWritten, bufLen, socketWriteAttempt);
        retStatus = STATUS_NET_SEND_DATA_FAILED;
    }

CleanUp:
    SAFE_MEMFREE(pIpv4Addr);
    SAFE_MEMFREE(pIpv6Addr);
    // CHK_LOG_ERR might be too verbose in this case
    if (STATUS_FAILED(retStatus)) {
        DLOGD("Warning: Send data failed with 0x%08x", retStatus);
    }
    CHK_LOG_ERR(retStatus);

    return retStatus;
}
