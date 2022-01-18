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
#define LOG_CLASS "WssApi"
#include "wss_api.h"
#include "wss_client.h"
#include "request_info.h"
#include "http_helper.h"
#include "channel_info.h"
#include "signaling_fsm.h"
#include "netio.h"

/******************************************************************************
 * DEFINITION
 ******************************************************************************/
#define WSS_API_ENTER() DLOGD("%s enter", __func__);
#define WSS_API_EXIT()  DLOGD("%s exit", __func__);

#define WSS_API_SECURE_PORT                "443"
#define WSS_API_CONNECTION_TIMEOUT         (2 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define WSS_API_COMPLETION_TIMEOUT         (5 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define WSS_API_SEND_BUFFER_MAX_SIZE       (4096)
#define WSS_API_RECV_BUFFER_MAX_SIZE       (2048)
#define WSS_API_PARAM_CHANNEL_ARN          "X-Amz-ChannelARN"
#define WSS_API_PARAM_CLIENT_ID            "X-Amz-ClientId"
#define WSS_API_HEADER_FIELD_CONNECTION    "Connection"
#define WSS_API_HEADER_FIELD_UPGRADE       "upgrade"
#define WSS_API_HEADER_FIELD_SEC_WS_ACCEPT "sec-websocket-accept"
#define WSS_API_HEADER_VALUE_UPGRADE       "upgrade"
#define WSS_API_HEADER_VALUE_WS            "websocket"
#define WSS_API_ENDPOINT_MASTER            "%s?%s=%s"
#define WSS_API_ENDPOINT_VIEWER            "%s?%s=%s&%s=%s"

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS wss_api_connect(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode)
{
    WSS_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    /* Variables for network connection */
    UINT32 uBytesReceived = 0;

    /* Variables for HTTP request */
    PCHAR pUrl = NULL;
    PRequestInfo pRequestInfo = NULL;
    CHAR clientKey[WSS_CLIENT_BASED64_RANDOM_SEED_LEN + 1];
    PCHAR pHost = NULL;

    UINT32 uHttpStatusCode = 0;
    HttpResponseContext* pHttpRspCtx = NULL;
    UINT32 urlLen = 0;
    // new net io.
    NetIoHandle xNetIoHandle = NULL;
    uint8_t* pHttpSendBuffer = NULL;
    uint8_t* pHttpRecvBuffer = NULL;

    CHK(pSignalingClient != NULL, STATUS_WSS_API_NULL_ARG);
    CHK(pSignalingClient->channelDescription.channelEndpointWss[0] != '\0', STATUS_INTERNAL_ERROR);
    CHK(NULL != (pHost = (CHAR*) MEMALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN)), STATUS_WSS_API_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpSendBuffer = (uint8_t*) MEMCALLOC(WSS_API_SEND_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpRecvBuffer = (uint8_t*) MEMCALLOC(WSS_API_RECV_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    // Prepare the json params for the call
    if (pSignalingClient->pChannelInfo->channelRoleType == SIGNALING_CHANNEL_ROLE_TYPE_VIEWER) {
        urlLen = STRLEN(WSS_API_ENDPOINT_VIEWER) + STRLEN(pSignalingClient->channelDescription.channelEndpointWss) +
            STRLEN(WSS_API_PARAM_CHANNEL_ARN) + STRLEN(pSignalingClient->channelDescription.channelArn) + STRLEN(WSS_API_PARAM_CLIENT_ID) +
            STRLEN(pSignalingClient->clientInfo.signalingClientInfo.clientId) + 1;
        CHK(NULL != (pUrl = (PCHAR) MEMALLOC(urlLen)), STATUS_WSS_API_NOT_ENOUGH_MEMORY);

        CHK(SNPRINTF(pUrl, urlLen, WSS_API_ENDPOINT_VIEWER, pSignalingClient->channelDescription.channelEndpointWss, WSS_API_PARAM_CHANNEL_ARN,
                     pSignalingClient->channelDescription.channelArn, WSS_API_PARAM_CLIENT_ID,
                     pSignalingClient->clientInfo.signalingClientInfo.clientId) > 0,
            STATUS_WSS_API_BUF_OVERFLOW);
    } else {
        urlLen = STRLEN(WSS_API_ENDPOINT_MASTER) + STRLEN(pSignalingClient->channelDescription.channelEndpointWss) +
            STRLEN(WSS_API_PARAM_CHANNEL_ARN) + STRLEN(pSignalingClient->channelDescription.channelArn) + 1;
        CHK(NULL != (pUrl = (PCHAR) MEMALLOC(urlLen)), STATUS_WSS_API_NOT_ENOUGH_MEMORY);

        CHK(SNPRINTF(pUrl, urlLen, WSS_API_ENDPOINT_MASTER, pSignalingClient->channelDescription.channelEndpointWss, WSS_API_PARAM_CHANNEL_ARN,
                     pSignalingClient->channelDescription.channelArn) > 0,
            STATUS_WSS_API_BUF_OVERFLOW);
    }

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (xNetIoHandle = NetIo_create()), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK_STATUS(NetIo_setRecvTimeout(xNetIoHandle, WSS_API_COMPLETION_TIMEOUT));
    CHK_STATUS(NetIo_setSendTimeout(xNetIoHandle, WSS_API_COMPLETION_TIMEOUT));
    CHK_STATUS(createRequestInfo(pUrl, NULL, pSignalingClient->pChannelInfo->pRegion, pSignalingClient->pChannelInfo->pCertPath, NULL, NULL,
                                 SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, WSS_API_CONNECTION_TIMEOUT,
                                 WSS_API_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT, pSignalingClient->pAwsCredentials,
                                 &pRequestInfo));
    pRequestInfo->verb = HTTP_REQUEST_VERB_GET;
    MEMSET(clientKey, 0, WSS_CLIENT_BASED64_RANDOM_SEED_LEN + 1);
    CHK_STATUS(wss_client_generateClientKey(clientKey, WSS_CLIENT_BASED64_RANDOM_SEED_LEN + 1));

    CHK_STATUS(http_req_pack(pRequestInfo, HTTP_REQUEST_VERB_GET_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pHttpSendBuffer,
                             WSS_API_SEND_BUFFER_MAX_SIZE, TRUE, TRUE, clientKey));

    CHK_STATUS(NetIo_connect(xNetIoHandle, pHost, WSS_API_SECURE_PORT));

    CHK(NetIo_send(xNetIoHandle, (unsigned char*) pHttpSendBuffer, STRLEN((PCHAR) pHttpSendBuffer)) == STATUS_SUCCESS, STATUS_SEND_DATA_FAILED);
    CHK_STATUS(NetIo_recv(xNetIoHandle, (unsigned char*) pHttpRecvBuffer, WSS_API_RECV_BUFFER_MAX_SIZE, &uBytesReceived));

    CHK(uBytesReceived > 0, STATUS_RECV_DATA_FAILED);

    struct list_head* requiredHeader = MEMALLOC(sizeof(struct list_head));
    INIT_LIST_HEAD(requiredHeader);
    http_parser_addRequiredHeader(requiredHeader, WSS_API_HEADER_FIELD_CONNECTION, STRLEN(WSS_API_HEADER_FIELD_CONNECTION), NULL, 0);
    http_parser_addRequiredHeader(requiredHeader, WSS_API_HEADER_FIELD_UPGRADE, STRLEN(WSS_API_HEADER_FIELD_UPGRADE), NULL, 0);
    http_parser_addRequiredHeader(requiredHeader, WSS_API_HEADER_FIELD_SEC_WS_ACCEPT, STRLEN(WSS_API_HEADER_FIELD_SEC_WS_ACCEPT), NULL, 0);

    CHK_STATUS(http_parser_start(&pHttpRspCtx, (CHAR*) pHttpRecvBuffer, (UINT32) uBytesReceived, requiredHeader));

    PHttpField node;
    node = http_parser_getValueByField(requiredHeader, WSS_API_HEADER_FIELD_CONNECTION, STRLEN(WSS_API_HEADER_FIELD_CONNECTION));
    CHK(node != NULL && node->valueLen == STRLEN(WSS_API_HEADER_VALUE_UPGRADE) &&
            MEMCMP(node->value, WSS_API_HEADER_VALUE_UPGRADE, node->valueLen) == 0,
        STATUS_WSS_UPGRADE_CONNECTION_ERROR);

    node = http_parser_getValueByField(requiredHeader, WSS_API_HEADER_FIELD_UPGRADE, STRLEN(WSS_API_HEADER_FIELD_UPGRADE));
    CHK(node != NULL && node->valueLen == STRLEN(WSS_API_HEADER_VALUE_WS) && MEMCMP(node->value, WSS_API_HEADER_VALUE_WS, node->valueLen) == 0,
        STATUS_WSS_UPGRADE_PROTOCOL_ERROR);

    node = http_parser_getValueByField(requiredHeader, WSS_API_HEADER_FIELD_SEC_WS_ACCEPT, STRLEN(WSS_API_HEADER_FIELD_SEC_WS_ACCEPT));
    CHK(node != NULL && wss_client_validateAcceptKey(clientKey, WSS_CLIENT_BASED64_RANDOM_SEED_LEN, node->value, node->valueLen) == STATUS_SUCCESS,
        STATUS_WSS_ACCEPT_KEY_ERROR);
    uHttpStatusCode = http_parser_getHttpStatusCode(pHttpRspCtx);

    /* Check HTTP results */
    if (uHttpStatusCode == HTTP_STATUS_SWITCH_PROTOCOL) {
        /**
         * switch to wss client.
         */
        /* We got a success response here. */
        PWssClientContext pWssClientCtx = NULL;
        uHttpStatusCode = HTTP_STATUS_UNKNOWN;

        wss_client_create(&pWssClientCtx, xNetIoHandle, pSignalingClient, wss_api_handleDataMsg, wss_api_handleCtrlMsg, wss_api_handleDisconnection);
        pSignalingClient->pWssContext = pWssClientCtx;
        CHK_STATUS(THREAD_CREATE_EX(&pWssClientCtx->listenerTid, WSS_LISTENER_THREAD_NAME, WSS_LISTENER_THREAD_SIZE, wss_client_start,
                                    (PVOID) pWssClientCtx));
        CHK_STATUS(THREAD_DETACH(pWssClientCtx->listenerTid));

        uHttpStatusCode = HTTP_STATUS_OK;
    }
    CHK(uHttpStatusCode == HTTP_STATUS_OK, retStatus);

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pHttpRspCtx != NULL) {
        retStatus = http_parser_detroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGD("destroying http parset failed.");
        }
    }

    if (STATUS_FAILED(retStatus) && pSignalingClient != NULL) {
        // Fix-up the timeout case
        HTTP_STATUS_CODE serviceCallResult = (retStatus == STATUS_OPERATION_TIMED_OUT) ? HTTP_STATUS_NETWORK_CONNECTION_TIMEOUT : HTTP_STATUS_UNKNOWN;
        // Trigger termination
        if (xNetIoHandle != NULL) {
            NetIo_disconnect(xNetIoHandle);
            NetIo_terminate(xNetIoHandle);
        }

        if (pSignalingClient->pWssContext != NULL) {
            wss_api_disconnect(pSignalingClient);
        }

        uHttpStatusCode = serviceCallResult;
    }

    if (pHttpStatusCode != NULL) {
        *pHttpStatusCode = uHttpStatusCode;
    }

    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    SAFE_MEMFREE(pHttpSendBuffer);
    SAFE_MEMFREE(pHttpRecvBuffer);
    freeRequestInfo(pRequestInfo);

    WSS_API_EXIT();
    return retStatus;
}

STATUS wss_api_send(PSignalingClient pSignalingClient, PBYTE pSendBuf, UINT32 bufLen)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    CHK(pSignalingClient != NULL && pSignalingClient->pWssContext != NULL, STATUS_WSS_API_NULL_ARG);

    DLOGD("Sending data over web socket: %s", pSendBuf);
    CHK_STATUS(wss_client_sendText(pSignalingClient->pWssContext, pSendBuf, bufLen));

CleanUp:
    LEAVES();
    return retStatus;
}

STATUS wss_api_handleDataMsg(PVOID pUSerData, PCHAR pMessage, UINT32 messageLen)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingMessageWrapper pSignalingMessageWrapper = NULL;
    PSignalingClient pSignalingClient = (PSignalingClient) pUSerData;

    CHK(pSignalingClient != NULL, STATUS_WSS_API_NULL_ARG);

    // If we have a signalingMessage and if there is a correlation id specified then the response should be non-empty
    if (pMessage == NULL || messageLen == 0) {
        // Check if anything needs to be done
        CHK_WARN(pMessage != NULL && messageLen != 0, retStatus, "Signaling received an empty message");
    }

    CHK(NULL != (pSignalingMessageWrapper = (PSignalingMessageWrapper) MEMCALLOC(1, SIZEOF(SignalingMessageWrapper))),
        STATUS_WSS_API_NOT_ENOUGH_MEMORY);

    CHK(wss_api_rsp_receivedMessage(pMessage, messageLen, pSignalingMessageWrapper) == STATUS_SUCCESS, STATUS_WSS_API_PARSE_RSP);

    pSignalingMessageWrapper->pSignalingClient = pSignalingClient;

    // CHK_STATUS(signaling_dispatchMsg((PVOID) pSignalingMessageWrapper));
    if (pSignalingClient->pDispatchMsgHandler != NULL) {
        CHK_STATUS(pSignalingClient->pDispatchMsgHandler((PVOID) pSignalingMessageWrapper));
    } else {
        SAFE_MEMFREE(pSignalingMessageWrapper);
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (pSignalingClient != NULL && STATUS_FAILED(retStatus)) {
        ATOMIC_INCREMENT(&pSignalingClient->diagnostics.numberOfRuntimeErrors);
        if (pSignalingClient->signalingClientCallbacks.errorReportFn != NULL) {
            retStatus = pSignalingClient->signalingClientCallbacks.errorReportFn(pSignalingClient->signalingClientCallbacks.customData, retStatus,
                                                                                 pMessage, messageLen);
        }
    }
    LEAVES();
    return retStatus;
}

STATUS wss_api_handleCtrlMsg(PVOID pUserData, UINT8 opcode, PCHAR pMessage, UINT32 messageLen)
{
    WSS_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL connected;
    PCHAR pCurPtr;
    PSignalingClient pSignalingClient = (PSignalingClient) pUserData;

    CHK(pSignalingClient != NULL, STATUS_WSS_API_NULL_ARG);

    DLOGD("opcode:%x", opcode);
    if (opcode == WSLAY_PONG) {
        DLOGD("<== pong, len: %ld", messageLen);
    } else if (opcode == WSLAY_PING) {
        DLOGD("<== ping, len: %ld", messageLen);
    } else if (opcode == WSLAY_CONNECTION_CLOSE) {
        DLOGD("<== connection close, len: %ld, reason:%s", messageLen, pMessage);
        pCurPtr = pMessage == NULL ? "(None)" : (PCHAR) pMessage;
        DLOGW("Client connection failed. Connection error string: %s", pCurPtr);

        PSignalingMessageWrapper pSignalingMessageWrapper = NULL;

        CHK(NULL != (pSignalingMessageWrapper = (PSignalingMessageWrapper) MEMCALLOC(1, SIZEOF(SignalingMessageWrapper))),
            STATUS_WSS_API_NOT_ENOUGH_MEMORY);
        pSignalingMessageWrapper->receivedSignalingMessage.signalingMessage.messageType = SIGNALING_MESSAGE_TYPE_CTRL_CLOSE;
        pSignalingMessageWrapper->pSignalingClient = pSignalingClient;

        // CHK_STATUS(signaling_dispatchMsg((PVOID) pSignalingMessageWrapper));
        if (pSignalingClient->pDispatchMsgHandler != NULL) {
            CHK_STATUS(pSignalingClient->pDispatchMsgHandler((PVOID) pSignalingMessageWrapper));
        } else {
            SAFE_MEMFREE(pSignalingMessageWrapper);
        }

    } else {
        DLOGD("<== ctrl msg(%d), len: %ld", opcode, messageLen);
    }

CleanUp:

    WSS_API_EXIT();
    return retStatus;
}

STATUS wss_api_handleDisconnection(PVOID pUserData, STATUS errCode)
{
    WSS_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL connected;
    PCHAR pCurPtr;
    PSignalingClient pSignalingClient = (PSignalingClient) pUserData;

    CHK(pSignalingClient != NULL, STATUS_WSS_API_NULL_ARG);

    PSignalingMessageWrapper pSignalingMessageWrapper = NULL;

    CHK(NULL != (pSignalingMessageWrapper = (PSignalingMessageWrapper) MEMCALLOC(1, SIZEOF(SignalingMessageWrapper))),
        STATUS_WSS_API_NOT_ENOUGH_MEMORY);
    pSignalingMessageWrapper->receivedSignalingMessage.signalingMessage.messageType = SIGNALING_MESSAGE_TYPE_CTRL_LISTENER_TREMINATED;
    pSignalingMessageWrapper->pSignalingClient = pSignalingClient;

    if (pSignalingClient->pDispatchMsgHandler != NULL) {
        CHK_STATUS(pSignalingClient->pDispatchMsgHandler((PVOID) pSignalingMessageWrapper));
    } else {
        SAFE_MEMFREE(pSignalingMessageWrapper);
    }

CleanUp:
    WSS_API_EXIT();
    return retStatus;
}

STATUS wss_api_disconnect(PSignalingClient pSignalingClient)
{
    WSS_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pSignalingClient != NULL, STATUS_WSS_API_NULL_ARG);
    PWssClientContext pWssClientCtx = (PWssClientContext) pSignalingClient->pWssContext;
    CHK(pWssClientCtx != NULL, STATUS_WSS_API_NULL_ARG);

    if (IS_VALID_TID_VALUE(pWssClientCtx->listenerTid)) {
        THREAD_CANCEL(pWssClientCtx->listenerTid);
        pWssClientCtx->listenerTid = INVALID_TID_VALUE;
    }

    // waiting the termination of listener thread.
    if (pWssClientCtx != NULL) {
        wss_client_close(pWssClientCtx);
        pSignalingClient->pWssContext = NULL;
    }

CleanUp:

    WSS_API_EXIT();
    return retStatus;
}
