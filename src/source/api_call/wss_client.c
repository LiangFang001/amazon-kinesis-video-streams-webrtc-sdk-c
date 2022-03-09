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
#define LOG_CLASS "WssClient"
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

#include <sys/socket.h>

#include "kvs/error.h"
#include "kvs/common_defs.h"
#include "kvs/platform_utils.h"
#include "network.h"
#include "wss_client.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
#define WSS_CLIENT_ENTER() // DLOGD("%s(%d) enter", __func__, __LINE__)
#define WSS_CLIENT_EXIT()  // DLOGD("%s(%d) exit", __func__, __LINE__);

#define IO_LOCK(pWssClientCtx)   MUTEX_LOCK(pWssClientCtx->ioLock)
#define IO_UNLOCK(pWssClientCtx) MUTEX_UNLOCK(pWssClientCtx->ioLock)

#define WSLAY_SUCCESS 0

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
static STATUS wss_client_generateRandomNumber(PUINT8 num, UINT32 len)
{
    WSS_CLIENT_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    CHK(mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0) == 0, STATUS_WSS_GENERATE_RANDOM_NUM_ERROR);
    CHK(mbedtls_ctr_drbg_random(&ctr_drbg, (UINT8*) num, len) == 0, STATUS_WSS_GENERATE_RANDOM_NUM_ERROR);

CleanUp:
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    WSS_CLIENT_EXIT();
    return retStatus;
}

STATUS wss_client_generateClientKey(PCHAR buf, UINT32 bufLen)
{
    WSS_CLIENT_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 olen = 0;
    UINT8 randomNum[WSS_CLIENT_RANDOM_SEED_LEN + 1];
    MEMSET(randomNum, 0, WSS_CLIENT_RANDOM_SEED_LEN + 1);

    CHK_STATUS(wss_client_generateRandomNumber(randomNum, WSS_CLIENT_RANDOM_SEED_LEN));
    CHK(mbedtls_base64_encode((UINT8*) buf, bufLen, (VOID*) &olen, (UINT8*) randomNum, WSS_CLIENT_RANDOM_SEED_LEN) == 0,
        STATUS_WSS_GENERATE_CLIENT_KEY_ERROR);
    CHK(olen == WSS_CLIENT_BASED64_RANDOM_SEED_LEN, STATUS_WSS_GENERATE_CLIENT_KEY_ERROR);

CleanUp:
    WSS_CLIENT_EXIT();
    return retStatus;
}

static STATUS wss_client_generateAcceptKey(PCHAR clientKey, UINT32 clientKeyLen, PCHAR acceptKey, UINT32 acceptKeyLen)
{
    WSS_CLIENT_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 bufLen = WSS_CLIENT_BASED64_RANDOM_SEED_LEN + WSS_CLIENT_RFC6455_UUID_LEN + 1;
    UINT8 buf[bufLen];
    UINT8 obuf[WSS_CLIENT_SHA1_RANDOM_SEED_W_UUID_LEN + 1];
    UINT32 olen = 0;
    MEMSET(buf, 0, bufLen);
    MEMSET(obuf, 0, WSS_CLIENT_SHA1_RANDOM_SEED_W_UUID_LEN + 1);
    MEMCPY(buf, clientKey, STRLEN(clientKey));
    MEMCPY(buf + STRLEN(clientKey), WSS_CLIENT_RFC6455_UUID, STRLEN(WSS_CLIENT_RFC6455_UUID));
    mbedtls_sha1(buf, STRLEN((PCHAR) buf), obuf);

    CHK(mbedtls_base64_encode((UINT8*) acceptKey, acceptKeyLen, (VOID*) &olen, (UINT8*) obuf, 20) == 0, STATUS_WSS_GENERATE_ACCEPT_KEY_ERROR);
    CHK(olen == WSS_CLIENT_ACCEPT_KEY_LEN, STATUS_WSS_GENERATE_ACCEPT_KEY_ERROR);

CleanUp:
    WSS_CLIENT_EXIT();
    return retStatus;
}

STATUS wss_client_validateAcceptKey(PCHAR clientKey, UINT32 clientKeyLen, PCHAR acceptKey, UINT32 acceptKeyLen)
{
    WSS_CLIENT_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    UINT8 tmpKey[WSS_CLIENT_ACCEPT_KEY_LEN + 1];
    MEMSET(tmpKey, 0, WSS_CLIENT_ACCEPT_KEY_LEN + 1);

    CHK_STATUS(wss_client_generateAcceptKey(clientKey, clientKeyLen, (PCHAR) tmpKey, WSS_CLIENT_ACCEPT_KEY_LEN + 1));
    CHK(MEMCMP(tmpKey, acceptKey, WSS_CLIENT_ACCEPT_KEY_LEN) == 0, STATUS_WSS_VALIDATE_ACCEPT_KEY_ERROR);

CleanUp:
    WSS_CLIENT_EXIT();
    return retStatus;
}

static INT32 wss_client_socketSend(PWssClientContext pWssClientCtx, const UINT8* data, SIZE_T len, INT32 flags)
{
    UNUSED_PARAM(flags);
    int res = NetIo_send(pWssClientCtx->xNetIoHandle, data, len);
    if (res == 0) {
        return len;
    } else {
        return res;
    }
}

static INT32 wss_client_socketRead(PWssClientContext pWssClientCtx, UINT8* data, SIZE_T len, INT32 flags)
{
    UINT32 uBytesReceived = 0;
    int res = NetIo_recv(pWssClientCtx->xNetIoHandle, data, len, &uBytesReceived);
    if (res == 0) {
        return uBytesReceived;
    } else {
        return res;
    }
}

static SSIZE_T wslay_send_callback(wslay_event_context_ptr ctx, const UINT8* data, SIZE_T len, INT32 flags, VOID* user_data)
{
    PWssClientContext pWssClientCtx = (PWssClientContext) user_data;
    SSIZE_T r = wss_client_socketSend(pWssClientCtx, data, len, flags);
    if (r != 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        } else {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        }
    }
    return len;
}

static SSIZE_T wslay_recv_callback(wslay_event_context_ptr ctx, UINT8* data, SIZE_T len, INT32 flags, VOID* user_data)
{
    PWssClientContext pWssClientCtx = (PWssClientContext) user_data;
    SSIZE_T r = wss_client_socketRead(pWssClientCtx, data, len, flags);
    if (r == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            wslay_event_set_error(ctx, WSLAY_ERR_WOULDBLOCK);
        } else {
            wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        }
    } else if (r == 0) {
        wslay_event_set_error(ctx, WSLAY_ERR_CALLBACK_FAILURE);
        r = -1;
    }
    return r;
}

static INT32 wslay_genmask_callback(wslay_event_context_ptr ctx, UINT8* buf, SIZE_T len, VOID* user_data)
{
    PWssClientContext pWssClientCtx = (PWssClientContext) user_data;
    wss_client_generateRandomNumber(buf, len);
    return 0;
}

static VOID wslay_msg_recv_callback(wslay_event_context_ptr ctx, const struct wslay_event_on_msg_recv_arg* arg, VOID* user_data)
{
    PWssClientContext pWssClientCtx = (PWssClientContext) user_data;
    if (!wslay_is_ctrl_frame(arg->opcode)) {
        pWssClientCtx->messageHandler(pWssClientCtx->pUserData, arg->msg, arg->msg_length);
    } else {
        pWssClientCtx->ctrlMessageHandler(pWssClientCtx->pUserData, arg->opcode, arg->msg, arg->msg_length);
        if (arg->opcode == WSLAY_PONG) {
            pWssClientCtx->pingCounter = 0;
        }
    }
}

INT32 wss_client_wantRead(PWssClientContext pWssClientCtx)
{
    WSS_CLIENT_ENTER();
    INT32 retStatus = TRUE;
    IO_LOCK(pWssClientCtx);
    retStatus = wslay_event_want_read(pWssClientCtx->event_ctx);
    IO_UNLOCK(pWssClientCtx);
    WSS_CLIENT_EXIT();
    return retStatus;
}

STATUS wss_client_onReadEvent(PWssClientContext pWssClientCtx)
{
    WSS_CLIENT_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    IO_LOCK(pWssClientCtx);
    if (wslay_event_get_read_enabled(pWssClientCtx->event_ctx) == 1) {
        retStatus = wslay_event_recv(pWssClientCtx->event_ctx) == 0 ? STATUS_SUCCESS : STATUS_WSS_CLIENT_RECV_FAILED;
    }
    IO_UNLOCK(pWssClientCtx);
    WSS_CLIENT_EXIT();
    return retStatus;
}

static STATUS wss_client_send(PWssClientContext pWssClientCtx, struct wslay_event_msg* arg)
{
    WSS_CLIENT_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    IO_LOCK(pWssClientCtx);
    // #TBD, wslay will memcpy this message buffer, so we can release the message buffer.
    // But this is a tradeoff. We can evaluate this design later.
    if (wslay_event_get_write_enabled(pWssClientCtx->event_ctx) == 1) {
        // send the message out immediately.
        CHK(wslay_event_queue_msg(pWssClientCtx->event_ctx, arg) == WSLAY_SUCCESS, STATUS_WSS_CLIENT_SEND_QUEUE_MSG_FAILED);
        CHK(wslay_event_send(pWssClientCtx->event_ctx) == WSLAY_SUCCESS, STATUS_WSS_CLIENT_SEND_FAILED);
    }

CleanUp:
    IO_UNLOCK(pWssClientCtx);
    WSS_CLIENT_EXIT();
    return retStatus;
}

STATUS wss_client_sendText(PWssClientContext pWssClientCtx, UINT8* buf, UINT32 len)
{
    struct wslay_event_msg arg;
    arg.opcode = WSLAY_TEXT_FRAME;
    arg.msg = buf;
    arg.msg_length = len;
    return wss_client_send(pWssClientCtx, &arg);
}

STATUS wss_client_sendBinary(PWssClientContext pWssClientCtx, UINT8* buf, UINT32 len)
{
    struct wslay_event_msg arg;
    arg.opcode = WSLAY_BINARY_FRAME;
    arg.msg = buf;
    arg.msg_length = len;
    return wss_client_send(pWssClientCtx, &arg);
}

STATUS wss_client_sendPing(PWssClientContext pWssClientCtx)
{
    struct wslay_event_msg arg;
    MEMSET(&arg, 0, sizeof(arg));
    arg.opcode = WSLAY_PING;
    arg.msg_length = 0;
    DLOGD("wss ping ==>");
    return wss_client_send(pWssClientCtx, &arg);
}

VOID wss_client_create(PWssClientContext* ppWssClientCtx, NetIoHandle xNetIoHandle, PVOID pUserData, MessageHandlerFunc pFunc,
                       CtrlMessageHandlerFunc pCtrlFunc, TerminationHandlerFunc pTerminationHandlerFunc)
{
    WSS_CLIENT_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PWssClientContext pWssClientCtx = NULL;
    struct wslay_event_callbacks callbacks = {
        wslay_recv_callback,    /* wslay_event_recv_callback */
        wslay_send_callback,    /* wslay_event_send_callback */
        wslay_genmask_callback, /* wslay_event_genmask_callback */
        NULL,                   /* wslay_event_on_frame_recv_start_callback */
        NULL,                   /* wslay_event_on_frame_recv_chunk_callback */
        NULL,                   /* wslay_event_on_frame_recv_end_callback */
        wslay_msg_recv_callback /* wslay_event_on_msg_recv_callback */
    };

    *ppWssClientCtx = NULL;
    CHK(NULL != (pWssClientCtx = (PWssClientContext) MEMCALLOC(1, SIZEOF(WssClientContext))), STATUS_NOT_ENOUGH_MEMORY);

    pWssClientCtx->event_callbacks = callbacks;
    pWssClientCtx->xNetIoHandle = xNetIoHandle;
    pWssClientCtx->pUserData = pUserData;
    pWssClientCtx->messageHandler = pFunc;
    pWssClientCtx->ctrlMessageHandler = pCtrlFunc;
    pWssClientCtx->terminationHandler = pTerminationHandlerFunc;

    // the initialization of the mutex
    pWssClientCtx->ioLock = MUTEX_CREATE(FALSE);
    CHK(IS_VALID_MUTEX_VALUE(pWssClientCtx->ioLock), STATUS_INVALID_OPERATION);

    pWssClientCtx->pingCounter = 0;

    wslay_event_context_client_init(&pWssClientCtx->event_ctx, &pWssClientCtx->event_callbacks, pWssClientCtx);
    pWssClientCtx->clientTid = INVALID_TID_VALUE;
    *ppWssClientCtx = pWssClientCtx;
CleanUp:
    WSS_CLIENT_EXIT();
    return;
}

PVOID wss_client_routine(PWssClientContext pWssClientCtx)
{
    WSS_CLIENT_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    // for the inteface of socket.
    INT32 nfds = 0;
    INT32 retval;
    fd_set rfds;
    struct timeval tv;
    // for ping-pong.
    UINT32 counter = 0;

    DLOGD("Wss client is up");
    wslay_event_config_set_callbacks(pWssClientCtx->event_ctx, &pWssClientCtx->event_callbacks);
    NetIo_setRecvTimeout(pWssClientCtx->xNetIoHandle, WSS_CLIENT_POLLING_INTERVAL);

    nfds = NetIo_getSocket(pWssClientCtx->xNetIoHandle);
    FD_ZERO(&rfds);

    // check the wss client want to read or write or not.

    while (pWssClientCtx != NULL && wss_client_wantRead(pWssClientCtx)) {
        // need to setup the timeout of epoll in order to let the wss cleint thread to write the buffer out.
        FD_SET(nfds, &rfds);
        // #TBD, this may need to be modified.
        tv.tv_sec = 0;
        tv.tv_usec = WSS_CLIENT_POLLING_INTERVAL * 1000;

        retval = select(nfds + 1, &rfds, NULL, NULL, &tv);

        if (retval == -1) {
            DLOGE("select() failed with errno %s", net_getErrorString(net_getErrorCode()));
            continue;
        }

        if (FD_ISSET(nfds, &rfds)) {
            if (wss_client_onReadEvent(pWssClientCtx)) {
                DLOGE("on read event failed");
            }
        }
        // for ping-pong
        if (pWssClientCtx->pingCounter > WSS_CLIENT_PING_PONG_COUNTER) {
            pWssClientCtx->ctrlMessageHandler(pWssClientCtx->pUserData, WSLAY_CONNECTION_CLOSE, "connection lost", STRLEN("connection lost"));
        }

        pWssClientCtx->pingCounter++;
        if (pWssClientCtx->pingCounter == WSS_CLIENT_PING_PONG_COUNTER) {
            CHK_STATUS(wss_client_sendPing(pWssClientCtx));
            pWssClientCtx->pingCounter = 0;
        }
    }

CleanUp:

    if (pWssClientCtx->terminationHandler != NULL) {
        pWssClientCtx->terminationHandler(pWssClientCtx->pUserData, retStatus);
    }

    DLOGD("Wss client is down");
    THREAD_EXIT(NULL);
    WSS_CLIENT_EXIT();
    return (PVOID)(ULONG_PTR) retStatus;
}

STATUS wss_client_start(PWssClientContext pWssClientCtx)
{
    STATUS retStatus = STATUS_SUCCESS;

    if (!IS_VALID_TID_VALUE(pWssClientCtx->clientTid)) {
        CHK_STATUS(THREAD_CREATE_EX(&pWssClientCtx->clientTid, WSS_CLIENT_THREAD_NAME, WSS_CLIENT_THREAD_SIZE, TRUE, wss_client_routine,
                                    (PVOID) pWssClientCtx));
    }

CleanUp:

    return retStatus;
}

VOID wss_client_close(PWssClientContext pWssClientCtx)
{
    INT32 retStatus = 0;

    CHK(pWssClientCtx != NULL, STATUS_WSS_CLIENT_NULL_ARG);

    if (IS_VALID_MUTEX_VALUE(pWssClientCtx->ioLock)) {
        // exit the thread of wss_client_routine.
        IO_LOCK(pWssClientCtx);
        wslay_event_shutdown_read(pWssClientCtx->event_ctx);
        wslay_event_shutdown_write(pWssClientCtx->event_ctx);
        IO_UNLOCK(pWssClientCtx);
    }

    // make sure the thread of wss_client_routine exit.
    if (IS_VALID_TID_VALUE(pWssClientCtx->clientTid)) {
        // waiting the termination of listener thread.
        THREAD_JOIN(pWssClientCtx->clientTid, NULL);
        pWssClientCtx->clientTid = INVALID_TID_VALUE;
    }

    wslay_event_context_free(pWssClientCtx->event_ctx);
    pWssClientCtx->event_ctx = NULL;

    if (IS_VALID_MUTEX_VALUE(pWssClientCtx->ioLock)) {
        MUTEX_FREE(pWssClientCtx->ioLock);
        pWssClientCtx->ioLock = INVALID_MUTEX_VALUE;
    }
    
    if (pWssClientCtx->xNetIoHandle != NULL) {
        NetIo_disconnect(pWssClientCtx->xNetIoHandle);
        NetIo_terminate(pWssClientCtx->xNetIoHandle);
        pWssClientCtx->xNetIoHandle = NULL;
    }

CleanUp:
    SAFE_MEMFREE(pWssClientCtx);
    return;
}
