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
#define LOG_CLASS "WssApi"
#include "../Include_i.h"
#include "wss_api.h"
#include "wss_client.h"
#include "request_info.h"
#include "network_api.h"
#include "http_helper.h"
#include "channel_info.h"
#include "signaling_fsm.h"

#define WSS_API_ENTER()
#define WSS_API_EXIT()

#define API_ENDPOINT_TCP_PORT                    "443"
#define API_CALL_CONNECTION_TIMEOUT              (2 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define API_CALL_COMPLETION_TIMEOUT              (5 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define API_CALL_CONNECTING_RETRY                (3)
#define API_CALL_CONNECTING_RETRY_INTERVAL_IN_MS (1000)

#define HTTP_HEADER_FIELD_CONNECTION    "Connection"
#define HTTP_HEADER_FIELD_UPGRADE       "upgrade"
#define HTTP_HEADER_FIELD_SEC_WS_ACCEPT "sec-websocket-accept"
#define HTTP_HEADER_VALUE_UPGRADE       "upgrade"
#define HTTP_HEADER_VALUE_WS            "websocket"

// Parameterized string for WSS connect
#define URL_TEMPLATE_ENDPOINT_MASTER "%s?%s=%s"
#define URL_TEMPLATE_ENDPOINT_VIEWER "%s?%s=%s&%s=%s"
#define URL_PARAM_CHANNEL_ARN        "X-Amz-ChannelARN"
#define URL_PARAM_CLIENT_ID          "X-Amz-ClientId"

STATUS wss_api_connect(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode)
{
    WSS_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    /* Variables for network connection */
    NetworkContext_t* pNetworkContext = NULL;
    SIZE_T uConnectionRetryCnt = 0;
    UINT32 uBytesToSend = 0, uBytesReceived = 0;

    /* Variables for HTTP request */
    PCHAR pUrl = NULL;
    PRequestInfo pRequestInfo = NULL;
    CHAR clientKey[WSS_CLIENT_BASED64_RANDOM_SEED_LEN + 1];
    PCHAR pHost = NULL;

    UINT32 uHttpStatusCode = 0;
    HttpResponseContext* pHttpRspCtx = NULL;
    UINT32 urlLen = 0;

    CHK(pSignalingClient != NULL, STATUS_WSS_API_NULL_ARG);
    CHK(pSignalingClient->channelEndpointWss[0] != '\0', STATUS_INTERNAL_ERROR);
    ATOMIC_STORE_BOOL(&pSignalingClient->connected, FALSE);
    CHK(NULL != (pHost = (CHAR*) MEMALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN)), STATUS_WSS_API_NOT_ENOUGH_MEMORY);

    // Prepare the json params for the call
    if (pSignalingClient->pChannelInfo->channelRoleType == SIGNALING_CHANNEL_ROLE_TYPE_VIEWER) {
        urlLen = STRLEN(URL_TEMPLATE_ENDPOINT_VIEWER) + STRLEN(pSignalingClient->channelEndpointWss) + STRLEN(URL_PARAM_CHANNEL_ARN) +
            STRLEN(pSignalingClient->channelDescription.channelArn) + STRLEN(URL_PARAM_CLIENT_ID) +
            STRLEN(pSignalingClient->clientInfo.signalingClientInfo.clientId) + 1;
        CHK(NULL != (pUrl = (PCHAR) MEMALLOC(urlLen)), STATUS_WSS_API_NOT_ENOUGH_MEMORY);
        SNPRINTF(pUrl, urlLen, URL_TEMPLATE_ENDPOINT_VIEWER, pSignalingClient->channelEndpointWss, URL_PARAM_CHANNEL_ARN,
                 pSignalingClient->channelDescription.channelArn, URL_PARAM_CLIENT_ID, pSignalingClient->clientInfo.signalingClientInfo.clientId);
    } else {
        urlLen = STRLEN(URL_TEMPLATE_ENDPOINT_MASTER) + STRLEN(pSignalingClient->channelEndpointWss) + STRLEN(URL_PARAM_CHANNEL_ARN) +
            STRLEN(pSignalingClient->channelDescription.channelArn) + 1;
        CHK(NULL != (pUrl = (PCHAR) MEMALLOC(urlLen)), STATUS_WSS_API_NOT_ENOUGH_MEMORY);

        SNPRINTF(pUrl, urlLen, URL_TEMPLATE_ENDPOINT_MASTER, pSignalingClient->channelEndpointWss, URL_PARAM_CHANNEL_ARN,
                 pSignalingClient->channelDescription.channelArn);
    }

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (pNetworkContext = (NetworkContext_t*) MEMALLOC(sizeof(NetworkContext_t))), STATUS_WSS_API_NOT_ENOUGH_MEMORY);
    CHK_STATUS(initNetworkContext(pNetworkContext));
    CHK_STATUS(createRequestInfo(pUrl, NULL, pSignalingClient->pChannelInfo->pRegion, pSignalingClient->pChannelInfo->pCertPath, NULL, NULL,
                                 SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, API_CALL_CONNECTION_TIMEOUT,
                                 API_CALL_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pSignalingClient->pAwsCredentials, &pRequestInfo));
    pRequestInfo->verb = HTTP_REQUEST_VERB_GET;
    MEMSET(clientKey, 0, WSS_CLIENT_BASED64_RANDOM_SEED_LEN + 1);
    wss_client_generateClientKey(clientKey, WSS_CLIENT_BASED64_RANDOM_SEED_LEN + 1);

    httpPackSendBuf(pRequestInfo, HTTP_REQUEST_VERB_GET_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pNetworkContext->pHttpSendBuffer,
                    MAX_HTTP_SEND_BUFFER_LEN, TRUE, TRUE, clientKey);

    for (uConnectionRetryCnt = 0; uConnectionRetryCnt < API_CALL_CONNECTING_RETRY; uConnectionRetryCnt++) {
        if ((retStatus = connectToServer(pNetworkContext, pHost, API_ENDPOINT_TCP_PORT)) == STATUS_SUCCESS) {
            break;
        }
        THREAD_SLEEP(API_CALL_CONNECTING_RETRY_INTERVAL_IN_MS * HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }
    CHK_STATUS(retStatus);

    uBytesToSend = STRLEN((PCHAR) pNetworkContext->pHttpSendBuffer);
    CHK(uBytesToSend == networkSend(pNetworkContext, pNetworkContext->pHttpSendBuffer, uBytesToSend), STATUS_SEND_DATA_FAILED);

    uBytesReceived = networkRecv(pNetworkContext, pNetworkContext->pHttpRecvBuffer, pNetworkContext->uHttpRecvBufferLen);

    CHK(uBytesReceived > 0, STATUS_RECV_DATA_FAILED);

    struct list_head* requiredHeader = malloc(sizeof(struct list_head));
    INIT_LIST_HEAD(requiredHeader);
    httpParserAddRequiredHeader(requiredHeader, HTTP_HEADER_FIELD_CONNECTION, STRLEN(HTTP_HEADER_FIELD_CONNECTION), NULL, 0);
    httpParserAddRequiredHeader(requiredHeader, HTTP_HEADER_FIELD_UPGRADE, STRLEN(HTTP_HEADER_FIELD_UPGRADE), NULL, 0);
    httpParserAddRequiredHeader(requiredHeader, HTTP_HEADER_FIELD_SEC_WS_ACCEPT, STRLEN(HTTP_HEADER_FIELD_SEC_WS_ACCEPT), NULL, 0);

    CHK_STATUS(httpParserStart(&pHttpRspCtx, (CHAR*) pNetworkContext->pHttpRecvBuffer, (UINT32) uBytesReceived, requiredHeader));

    PHttpField node;
    node = httpParserGetValueByField(requiredHeader, HTTP_HEADER_FIELD_CONNECTION, STRLEN(HTTP_HEADER_FIELD_CONNECTION));
    CHK(node != NULL && node->valueLen == STRLEN(HTTP_HEADER_VALUE_UPGRADE) && MEMCMP(node->value, HTTP_HEADER_VALUE_UPGRADE, node->valueLen) == 0,
        STATUS_WSS_UPGRADE_CONNECTION_ERROR);

    node = httpParserGetValueByField(requiredHeader, HTTP_HEADER_FIELD_UPGRADE, STRLEN(HTTP_HEADER_FIELD_UPGRADE));
    CHK(node != NULL && node->valueLen == STRLEN(HTTP_HEADER_VALUE_WS) && MEMCMP(node->value, HTTP_HEADER_VALUE_WS, node->valueLen) == 0,
        STATUS_WSS_UPGRADE_PROTOCOL_ERROR);

    node = httpParserGetValueByField(requiredHeader, HTTP_HEADER_FIELD_SEC_WS_ACCEPT, STRLEN(HTTP_HEADER_FIELD_SEC_WS_ACCEPT));
    CHK(node != NULL && wss_client_validateAcceptKey(clientKey, WSS_CLIENT_BASED64_RANDOM_SEED_LEN, node->value, node->valueLen) == STATUS_SUCCESS,
        STATUS_WSS_ACCEPT_KEY_ERROR);
    uHttpStatusCode = httpParserGetHttpStatusCode(pHttpRspCtx);

    /* Check HTTP results */
    if (uHttpStatusCode == SERVICE_CALL_RESULT_UPGRADE) {
        TID threadId;
        /**
         * switch to wss client.
         */
        /* We got a success response here. */
        PWssClientContext wssClientCtx = NULL;
        uHttpStatusCode = SERVICE_CALL_UNKNOWN;

        wss_client_create(&wssClientCtx, pNetworkContext, pSignalingClient, wss_api_handleDataMsg, wss_api_handleCtrlMsg, wss_api_handleTermination);

        pSignalingClient->pWssContext = wssClientCtx;

        // CHK_STATUS(THREAD_CREATE(&threadId, wss_client_start, (PVOID) ));
        CHK_STATUS(THREAD_CREATE_EX(&threadId, LWS_LISTENER_THREAD_NAME, LWS_LISTENER_THREAD_SIZE, wss_client_start, (PVOID) wssClientCtx));

        CHK_STATUS(THREAD_DETACH(threadId));

        uHttpStatusCode = SERVICE_CALL_RESULT_OK;
        ATOMIC_STORE_BOOL(&pSignalingClient->connected, TRUE);
    }
    CHK(uHttpStatusCode == SERVICE_CALL_RESULT_OK, retStatus);

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pHttpRspCtx != NULL) {
        retStatus = httpParserDetroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGD("destroying http parset failed.");
        }
    }

    if (STATUS_FAILED(retStatus) && pSignalingClient != NULL) {
        // Fix-up the timeout case
        SERVICE_CALL_RESULT serviceCallResult =
            (retStatus == STATUS_OPERATION_TIMED_OUT) ? SERVICE_CALL_NETWORK_CONNECTION_TIMEOUT : SERVICE_CALL_UNKNOWN;
        // Trigger termination
        if (pNetworkContext != NULL) {
            disconnectFromServer(pNetworkContext);
            terminateNetworkContext(pNetworkContext);
            MEMFREE(pNetworkContext);
        }

        if (pSignalingClient->pWssContext != NULL) {
            wss_api_terminate(pSignalingClient, serviceCallResult);
        }

        uHttpStatusCode = serviceCallResult;
    }

    if (pHttpStatusCode != NULL) {
        *pHttpStatusCode = uHttpStatusCode;
    }

    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
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

        connected = ATOMIC_EXCHANGE_BOOL(&pSignalingClient->connected, FALSE);

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

STATUS wss_api_handleTermination(PVOID pUserData, STATUS errCode)
{
    WSS_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL connected;
    PCHAR pCurPtr;
    PSignalingClient pSignalingClient = (PSignalingClient) pUserData;

    CHK(pSignalingClient != NULL, STATUS_WSS_API_NULL_ARG);

    if (STATUS_FAILED(errCode) && pSignalingClient != NULL) {
        ATOMIC_STORE(&pSignalingClient->result, (SIZE_T) SERVICE_CALL_UNKNOWN);
    }
CleanUp:
    WSS_API_EXIT();
    return retStatus;
}

STATUS wss_api_terminate(PSignalingClient pSignalingClient, SERVICE_CALL_RESULT callResult)
{
    WSS_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pSignalingClient != NULL, STATUS_WSS_API_NULL_ARG);

    ATOMIC_STORE_BOOL(&pSignalingClient->connected, FALSE);
    ATOMIC_STORE(&pSignalingClient->result, (SIZE_T) callResult);

    // waiting the termination of listener thread.
    if (pSignalingClient->pWssContext != NULL) {
        wss_client_close(pSignalingClient->pWssContext);
        pSignalingClient->pWssContext = NULL;
    }

CleanUp:

    WSS_API_EXIT();
    return retStatus;
}
