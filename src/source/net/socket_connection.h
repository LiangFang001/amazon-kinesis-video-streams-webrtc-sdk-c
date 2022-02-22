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
#ifndef __KINESIS_VIDEO_WEBRTC_SOCKET_CONNECTION__
#define __KINESIS_VIDEO_WEBRTC_SOCKET_CONNECTION__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "network.h"
#include "tls.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
#define SOCKET_SEND_RETRY_TIMEOUT_MICRO_SECOND 500000
#define MAX_SOCKET_WRITE_RETRY                 3

#define CLOSE_SOCKET_IF_CANT_RETRY(e, ps)                                                                                                            \
    if ((e) != EAGAIN && (e) != EWOULDBLOCK && (e) != EINTR && (e) != EINPROGRESS && (e) != EPERM && (e) != EALREADY && (e) != ENETUNREACH) {        \
        DLOGD("Close socket %d", (ps)->localSocket);                                                                                                 \
        ATOMIC_STORE_BOOL(&(ps)->connectionClosed, TRUE);                                                                                            \
    }

typedef struct __SocketConnection SocketConnection;
typedef STATUS (*ConnectionDataAvailableFunc)(UINT64, struct __SocketConnection*, PBYTE, UINT32, PKvsIpAddress, PKvsIpAddress);

struct __SocketConnection {
    /* Indicate whether this socket is marked for cleanup */
    volatile ATOMIC_BOOL connectionClosed;
    /* Process incoming bits */
    volatile ATOMIC_BOOL receiveData;

    /* Socket is in use and can't be freed */
    volatile ATOMIC_BOOL inUse;

    INT32 localSocket;
    KVS_SOCKET_PROTOCOL protocol;
    KvsIpAddress peerIpAddr;
    KvsIpAddress hostIpAddr;

    BOOL bTlsSession;        //!< indicate this socket connectino is secure or not.
    PTlsSession pTlsSession; //!< the context of the tls session.

    MUTEX lock;

    ConnectionDataAvailableFunc dataAvailableCallbackFn; //!< the callback when the data is ready.
    UINT64 dataAvailableCallbackCustomData;
    UINT64 tlsHandshakeStartTime;
};
typedef struct __SocketConnection* PSocketConnection;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
/**
 * @brief Create a SocketConnection object and store it in PSocketConnection. creates a socket based on KVS_SOCKET_PROTOCOL specified, and bind it to
 * the host ip address. If the protocol is tcp, then peer ip address is required and it will try to establish the tcp connection.
 *
 * @param[in] familyType Family for the socket. Must be one of KVS_IP_FAMILY_TYPE
 * @param[in] protocol socket protocol. TCP or UDP
 * @param[in] pBindAddr host ip address to bind to (OPTIONAL)
 * @param[in] pPeerIpAddr peer ip address to connect in case of TCP (OPTIONAL)
 * @param[in] customData data available callback custom data
 * @param[in] dataAvailableFn data available callback (OPTIONAL)
 * @param[in] sendBufSize send buffer size in bytes
 * @param[in, out] ppSocketConnection the resulting SocketConnection struct
 *
 * @return STATUS status of execution
 */
STATUS socket_connection_create(KVS_IP_FAMILY_TYPE familyType, KVS_SOCKET_PROTOCOL protocol, PKvsIpAddress pBindAddr, PKvsIpAddress pPeerIpAddr,
                                UINT64 customData, ConnectionDataAvailableFunc dataAvailableFn, UINT32 sendBufSize,
                                PSocketConnection* ppSocketConnection);

/**
 * @brief Free the SocketConnection struct
 *
 * @param[in, out] ppSocketConnection SocketConnection to be freed
 *
 * @return STATUS status of execution
 */
STATUS socket_connection_free(PSocketConnection* ppSocketConnection);

/**
 * @brief Given a created SocketConnection, initialize TLS or DTLS handshake depending on the socket protocol
 *
 * @param[in] pSocketConnection the SocketConnection struct
 * @param[in] isServer will SocketConnection act as server during the TLS or DTLS handshake
 *
 * @return STATUS status of execution
 */
STATUS socket_connection_initSecureConnection(PSocketConnection pSocketConnection, BOOL isServer);

/**
 * @brief Given a created SocketConnection, send data through the underlying socket. If socket type is UDP, then destination
 * address is required. If socket type is tcp, destination address is ignored and data is send to the peer address provided
 * at SocketConnection creation. If socket_connection_initSecureConnection has been called then data will be encrypted,
 * otherwise data will be sent as is.
 *
 * @param[in] pSocketConnection the SocketConnection struct
 * @param[in] pBuf buffer containing unencrypted data
 * @param[in] bufLen length of buffer
 * @param[in] pDestIp destination address. Required only if socket type is UDP.
 *
 * @return STATUS status of execution.
 */
STATUS socket_connection_send(PSocketConnection pSocketConnection, PBYTE pBuf, UINT32 bufLen, PKvsIpAddress pDestIp);

/**
 * @brief This api only supports tls session. If PSocketConnection is not secure then nothing happens, otherwise assuming the bytes passed in are
 * encrypted, and the encryted data will be replaced with unencrypted data at function return.
 *
 * @param[in] pSocketConnection the SocketConnection struct
 * @param[in, out] pBuf buffer containing encrypted data. Will contain unencrypted on successful return
 * @param[in] bufferLen available length of buffer
 * @param[in, out] pDataLen length of encrypted data. Will contain length of decrypted data on successful return
 *
 * @return STATUS status of execution.
 */
STATUS socket_connection_read(PSocketConnection pSocketConnection, PBYTE pBuf, UINT32 bufferLen, PUINT32 pDataLen);

/**
 * Mark PSocketConnection as closed
 *
 * @param - PSocketConnection - IN - the SocketConnection struct
 *
 * @return - STATUS - status of execution
 */
STATUS socket_connection_close(PSocketConnection);

/**
 * Check if PSocketConnection is closed
 *
 * @param[in] PSocketConnection the SocketConnection struct
 *
 * @return BOOL whether connection is closed
 */
BOOL socket_connection_isClosed(PSocketConnection pSocketConnection);

/**
 * @brief   Return whether socket has been connected. Return TRUE for UDP sockets.
 *          Return TRUE for TCP sockets once the connection has been established, otherwise return FALSE.
 *
 * @param[in] pSocketConnection the SocketConnection struct
 *
 * @return STATUS status of execution
 */
BOOL socket_connection_isConnected(PSocketConnection pSocketConnection);

// internal functions
VOID socket_connection_tlsSessionOnStateChange(UINT64, TLS_SESSION_STATE);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_SOCKET_CONNECTION__ */
