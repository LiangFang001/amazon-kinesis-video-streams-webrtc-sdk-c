/**
 * Kinesis Video TLS
 */
#define LOG_CLASS "TLS_mbedtls"

#include "io_buffer.h"
#include "Rtp.h"

STATUS tls_session_create(PTlsSessionCallbacks pCallbacks, PTlsSession* ppTlsSession)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PTlsSession pTlsSession = NULL;

    CHK(ppTlsSession != NULL && pCallbacks != NULL && pCallbacks->tlsOutboundPacketFn != NULL, STATUS_TLS_NULL_ARG);

    pTlsSession = (PTlsSession) MEMCALLOC(1, SIZEOF(TlsSession));
    CHK(pTlsSession != NULL, STATUS_TLS_NOT_ENOUGH_MEMORY);

    CHK_STATUS(io_buffer_create(DEFAULT_MTU_SIZE, &pTlsSession->pReadBuffer));
    pTlsSession->callbacks = *pCallbacks;
    pTlsSession->state = TLS_SESSION_STATE_NEW;

    // initialize mbedtls stuff with sane values
    mbedtls_entropy_init(&pTlsSession->entropy);
    mbedtls_ctr_drbg_init(&pTlsSession->ctrDrbg);
    mbedtls_x509_crt_init(&pTlsSession->cacert);
    mbedtls_ssl_config_init(&pTlsSession->sslCtxConfig);
    mbedtls_ssl_init(&pTlsSession->sslCtx);
    CHK(mbedtls_ctr_drbg_seed(&pTlsSession->ctrDrbg, mbedtls_entropy_func, &pTlsSession->entropy, NULL, 0) == 0, STATUS_TLS_CREATE_SSL_FAILED);
    CHK(mbedtls_x509_crt_parse_file(&pTlsSession->cacert, KVS_CA_CERT_PATH) == 0, STATUS_TLS_INVALID_CA_CERT_PATH);

CleanUp:
    if (STATUS_FAILED(retStatus) && pTlsSession != NULL) {
        tls_session_free(&pTlsSession);
    }

    if (ppTlsSession != NULL) {
        *ppTlsSession = pTlsSession;
    }

    LEAVES();
    return retStatus;
}

STATUS tls_session_free(PTlsSession* ppTlsSession)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PTlsSession pTlsSession = NULL;

    CHK(ppTlsSession != NULL, STATUS_TLS_NULL_ARG);

    pTlsSession = *ppTlsSession;
    CHK(pTlsSession != NULL, retStatus);

    mbedtls_entropy_free(&pTlsSession->entropy);
    mbedtls_ctr_drbg_free(&pTlsSession->ctrDrbg);
    mbedtls_x509_crt_free(&pTlsSession->cacert);
    mbedtls_ssl_config_free(&pTlsSession->sslCtxConfig);
    mbedtls_ssl_free(&pTlsSession->sslCtx);

    io_buffer_free(&pTlsSession->pReadBuffer);
    retStatus = tls_session_shutdown(pTlsSession);
    SAFE_MEMFREE(*ppTlsSession);

CleanUp:
    return retStatus;
}

INT32 tls_session_sendCallback(PVOID customData, const unsigned char* buf, ULONG len)
{
    STATUS retStatus = STATUS_SUCCESS;
    PTlsSession pTlsSession = (PTlsSession) customData;

    CHK(pTlsSession != NULL, STATUS_TLS_NULL_ARG);

    pTlsSession->callbacks.tlsOutboundPacketFn(pTlsSession->callbacks.tlsOutBoundPacketFnCustomData, (PBYTE) buf, len);

CleanUp:

    return STATUS_FAILED(retStatus) ? -retStatus : len;
}

INT32 tls_session_recvCallback(PVOID customData, unsigned char* buf, ULONG len)
{
    STATUS retStatus = STATUS_SUCCESS;
    PTlsSession pTlsSession = (PTlsSession) customData;
    PIOBuffer pBuffer;
    UINT32 readBytes = MBEDTLS_ERR_SSL_WANT_READ;

    CHK(pTlsSession != NULL, STATUS_TLS_NULL_ARG);

    pBuffer = pTlsSession->pReadBuffer;

    if (pBuffer->off < pBuffer->len) {
        retStatus = io_buffer_read(pBuffer, buf, len, &readBytes);
    }

CleanUp:

    return STATUS_FAILED(retStatus) ? -retStatus : readBytes;
}

STATUS tls_session_start(PTlsSession pTlsSession, BOOL isServer)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    INT32 sslRet;

    CHK(pTlsSession != NULL, STATUS_TLS_NULL_ARG);
    CHK(pTlsSession->state == TLS_SESSION_STATE_NEW, retStatus);

    CHK(mbedtls_ssl_config_defaults(&pTlsSession->sslCtxConfig, isServer ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0,
        STATUS_TLS_CREATE_SSL_FAILED);

    mbedtls_ssl_conf_ca_chain(&pTlsSession->sslCtxConfig, &pTlsSession->cacert, NULL);
    mbedtls_ssl_conf_authmode(&pTlsSession->sslCtxConfig, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&pTlsSession->sslCtxConfig, mbedtls_ctr_drbg_random, &pTlsSession->ctrDrbg);
    CHK(mbedtls_ssl_setup(&pTlsSession->sslCtx, &pTlsSession->sslCtxConfig) == 0, STATUS_TLS_SSL_CTX_SETUP_FAILED);
    mbedtls_ssl_set_mtu(&pTlsSession->sslCtx, DEFAULT_MTU_SIZE);
    mbedtls_ssl_set_bio(&pTlsSession->sslCtx, pTlsSession, (mbedtls_ssl_send_t*) tls_session_sendCallback,
                        (mbedtls_ssl_recv_t*) tls_session_recvCallback, NULL);

    /* init and send handshake */
    tls_session_changeState(pTlsSession, TLS_SESSION_STATE_CONNECTING);
    sslRet = mbedtls_ssl_handshake(&pTlsSession->sslCtx);
    CHK(sslRet == MBEDTLS_ERR_SSL_WANT_READ || sslRet == MBEDTLS_ERR_SSL_WANT_WRITE, STATUS_TLS_SSL_HANDSHAKE_FAILED);
    LOG_MBEDTLS_ERROR("mbedtls_ssl_handshake", sslRet);

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS tls_session_read(PTlsSession pTlsSession, PBYTE pData, UINT32 bufferLen, PUINT32 pDataLen)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    INT32 sslRet, readBytes = 0;
    BOOL iterate = TRUE;
    PIOBuffer pReadBuffer;

    CHK(pTlsSession != NULL && pData != NULL && pDataLen != NULL, STATUS_TLS_NULL_ARG);
    CHK(pTlsSession->state != TLS_SESSION_STATE_NEW, STATUS_TLS_CONNECTION_NOT_READY_TO_SEND);
    CHK(pTlsSession->state != TLS_SESSION_STATE_CLOSED, STATUS_SOCKET_CONN_CLOSED_ALREADY);

    pReadBuffer = pTlsSession->pReadBuffer;
    CHK_STATUS(io_buffer_write(pReadBuffer, pData, *pDataLen));

    // read application data
    while (iterate && pReadBuffer->off < pReadBuffer->len && bufferLen > 0) {
        sslRet = mbedtls_ssl_read(&pTlsSession->sslCtx, pData + readBytes, bufferLen);
        if (sslRet > 0) {
            readBytes += sslRet;
            bufferLen -= sslRet;
        } else if (sslRet == 0 || sslRet == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            // if sslRet is 0, the connection is closed already.
            // if sslRet is MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY, the client notified us that the connection is going to be closed.
            // In either case, we'll make sure that the state will change to CLOSED. If it's already closed, it'll be just a noop.
            DLOGI("Detected TLS close_notify alert");
            CHK_STATUS(tls_session_shutdown(pTlsSession));
            iterate = FALSE;
        } else if (sslRet == MBEDTLS_ERR_SSL_WANT_READ || sslRet == MBEDTLS_ERR_SSL_WANT_WRITE) {
            iterate = FALSE;
        } else {
            LOG_MBEDTLS_ERROR("mbedtls_ssl_read", sslRet);
            readBytes = 0;
            retStatus = STATUS_INTERNAL_ERROR;
            iterate = FALSE;
        }
    }

    if (pTlsSession->sslCtx.state == MBEDTLS_SSL_HANDSHAKE_OVER) {
        tls_session_changeState(pTlsSession, TLS_SESSION_STATE_CONNECTED);
    }

CleanUp:
    if (pDataLen != NULL) {
        *pDataLen = readBytes;
    }

    // CHK_LOG_ERR might be too verbose
    if (STATUS_FAILED(retStatus)) {
        DLOGD("Warning: reading socket data failed with 0x%08x", retStatus);
    }

    LEAVES();
    return retStatus;
}

STATUS tls_session_send(PTlsSession pTlsSession, PBYTE pData, UINT32 dataLen)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 writtenBytes = 0;
    BOOL iterate = TRUE;
    INT32 sslRet;

    CHK(pTlsSession != NULL, STATUS_TLS_NULL_ARG);

    while (iterate && writtenBytes < dataLen) {
        sslRet = mbedtls_ssl_write(&pTlsSession->sslCtx, pData + writtenBytes, dataLen - writtenBytes);
        if (sslRet > 0) {
            writtenBytes += sslRet;
        } else if (sslRet == MBEDTLS_ERR_SSL_WANT_READ || sslRet == MBEDTLS_ERR_SSL_WANT_WRITE) {
            iterate = FALSE;
        } else {
            LOG_MBEDTLS_ERROR("mbedtls_ssl_write", sslRet);
            writtenBytes = 0;
            retStatus = STATUS_INTERNAL_ERROR;
            iterate = FALSE;
        }
    }

CleanUp:
    CHK_LOG_ERR(retStatus);
    LEAVES();
    return retStatus;
}

STATUS tls_session_shutdown(PTlsSession pTlsSession)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pTlsSession != NULL, STATUS_TLS_NULL_ARG);
    CHK(pTlsSession->state != TLS_SESSION_STATE_CLOSED, retStatus);

    while (mbedtls_ssl_close_notify(&pTlsSession->sslCtx) == MBEDTLS_ERR_SSL_WANT_WRITE) {
        // keep flushing outgoing buffer until nothing left
    }
    CHK_STATUS(tls_session_changeState(pTlsSession, TLS_SESSION_STATE_CLOSED));

CleanUp:

    CHK_LOG_ERR(retStatus);

    return retStatus;
}
