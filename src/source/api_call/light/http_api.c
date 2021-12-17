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
#define LOG_CLASS "HttpApi"
#include "../Include_i.h"
#include "http_api.h"
#include "network_api.h"
#include "http_helper.h"
#include "channel_info.h"
#include "wss_api.h"

#define HTTP_API_ENTER() DLOGD("%s(%d) enter", __func__, __LINE__)
#define HTTP_API_EXIT()  DLOGD("%s(%d) exit", __func__, __LINE__)

#define API_ENDPOINT_TCP_PORT                    "443"
#define API_CALL_CONNECTION_TIMEOUT              (2 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define API_CALL_COMPLETION_TIMEOUT              (5 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define API_CALL_CONNECTING_RETRY                (3)
#define API_CALL_CONNECTING_RETRY_INTERVAL_IN_MS (1000)
#define API_CALL_CHANNEL_PROTOCOL                "\"WSS\", \"HTTPS\""

// API postfix definitions
// https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_CreateSignalingChannel.html
#define API_CREATE_SIGNALING_CHANNEL "/createSignalingChannel"
// https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_DescribeSignalingChannel.html
#define API_DESCRIBE_SIGNALING_CHANNEL "/describeSignalingChannel"
// https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_GetSignalingChannelEndpoint.html
#define API_GET_SIGNALING_CHANNEL_ENDPOINT "/getSignalingChannelEndpoint"
// https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_DeleteSignalingChannel.html
#define API_DELETE_SIGNALING_CHANNEL "/deleteSignalingChannel"
// https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_AWSAcuitySignalingService_GetIceServerConfig.html
#define API_GET_ICE_CONFIG "/v1/get-ice-server-config"
// https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_ListSignalingChannels.html
#define API_LIST_SIGNALING_CHANNEL "/listSignalingChannels"
// https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_UpdateSignalingChannel.html
#define API_UPDATE_SIGNALING_CHANNEL "/updateSignalingChannel"

#define IOT_REQUEST_CONNECTION_TIMEOUT (3 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define IOT_REQUEST_COMPLETION_TIMEOUT (5 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define ROLE_ALIASES_PATH              ((PCHAR) "/role-aliases")
#define CREDENTIAL_SERVICE             ((PCHAR) "/credentials")
#define IOT_THING_NAME_HEADER          "x-amzn-iot-thingname"
/**
 * @brief   API_CreateSignalingChannel
 * POST /createSignalingChannel HTTP/1.1
 * Content-type: application/json
 *
 * {
 *    "ChannelName": "string",
 *    "ChannelType": "string",
 *    "SingleMasterConfiguration": {
 *       "MessageTtlSeconds": number
 *    },
 *    "Tags": [
 *       {
 *          "Key": "string",
 *          "Value": "string"
 *       }
 *    ]
 * }
 */
// #YC_TBD, need to add the rest part.
#define BODY_TEMPLATE_CREATE_CHANNEL                                                                                                                 \
    "{\n\t\"ChannelName\": \"%s\""                                                                                                                   \
    "\n}"

// Parameterized string for TagStream API - we should have at least one tag
#define BODY_TEMPLATE_TAGS ",\n\t\"Tags\": [%s\n\t]"
#define BODY_TEMPLATE_TAG  "\n\t\t{\"Key\": \"%s\", \"Value\": \"%s\"},"
/**
 * @brief   API_DescribeSignalingChannel
 * POST /describeSignalingChannel HTTP/1.1
 * Content-type: application/json
 *
 * {
 *    "ChannelARN": "string",
 *    "ChannelName": "string"
 * }
 */
#define BODY_TEMPLATE_DESCRIBE_CHANNEL "{\n\t\"ChannelName\": \"%s\"\n}"
/**
 * @brief   API_GetSignalingChannelEndpoint
 * POST /getSignalingChannelEndpoint HTTP/1.1
 * Content-type: application/json
 *
 * {
 *    "ChannelARN": "string",
 *    "SingleMasterChannelEndpointConfiguration": {
 *       "Protocols": [ "string" ],
 *       "Role": "string"
 *    }
 * }
 */
#define BODY_TEMPLATE_GET_CHANNEL_ENDPOINT                                                                                                           \
    "{\n\t\"ChannelARN\": \"%s\","                                                                                                                   \
    "\n\t\"SingleMasterChannelEndpointConfiguration\": {"                                                                                            \
    "\n\t\t\"Protocols\": [%s],"                                                                                                                     \
    "\n\t\t\"Role\": \"%s\""                                                                                                                         \
    "\n\t}\n}"
/**
 * @brief   API_DeleteSignalingChannel
 * POST /deleteSignalingChannel HTTP/1.1
 * Content-type: application/json
 *
 * {
 *    "ChannelARN": "string",
 *    "CurrentVersion": "string"
 * }
 */
#define BODY_TEMPLATE_DELETE_CHANNEL                                                                                                                 \
    "{\n\t\"ChannelARN\": \"%s\","                                                                                                                   \
    "\n\t\"CurrentVersion\": \"%s\"\n}"
/**
 * @brief   API_AWSAcuitySignalingService_GetIceServerConfig
 * POST /v1/get-ice-server-config HTTP/1.1
 * Content-type: application/json
 *
 * {
 *    "ChannelARN": "string",
 *    "ClientId": "string",
 *    "Service": "string",
 *    "Username": "string"
 * }
 */
#define BODY_TEMPLATE_GET_ICE_CONFIG                                                                                                                 \
    "{\n\t\"ChannelARN\": \"%s\","                                                                                                                   \
    "\n\t\"ClientId\": \"%s\","                                                                                                                      \
    "\n\t\"Service\": \"TURN\""                                                                                                                      \
    "\n}"
/**
 * @brief   API_ListSignalingChannels
 * POST /listSignalingChannels HTTP/1.1
 * Content-type: application/json
 *
 * {
 *    "ChannelNameCondition": {
 *       "ComparisonOperator": "string",
 *       "ComparisonValue": "string"
 *    },
 *    "MaxResults": number,
 *    "NextToken": "string"
 * }
 */
//#define BODY_TEMPLATE_LIST_CHANNELS

/**
 * @brief   API_UpdateSignalingChannel
 * POST /updateSignalingChannel HTTP/1.1
 * Content-type: application/json
 *
 * {
 *    "ChannelARN": "string",
 *    "CurrentVersion": "string",
 *    "SingleMasterConfiguration": {
 *       "MessageTtlSeconds": number
 *    }
 * }
 */
//#define BODY_TEMPLATE_UPDATE_CHANNEL
// #YC_TBD.
#define MAX_STRLEN_OF_UINT32 (10)

//////////////////////////////////////////////////////////////////////////
// api calls
//////////////////////////////////////////////////////////////////////////
STATUS http_api_createChannel(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode)
{
    HTTP_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PChannelInfo pChannelInfo = pSignalingClient->pChannelInfo;

    /* Variables for network connection */
    NetworkContext_t* pNetworkContext = NULL;
    SIZE_T uConnectionRetryCnt = 0;
    UINT32 uBytesToSend = 0, uBytesReceived = 0;

    /* Variables for HTTP request */
    PCHAR pUrl = NULL;
    PRequestInfo pRequestInfo = NULL;
    PCHAR pHttpBody = NULL;
    UINT32 httpBodyLen;
    PCHAR pHost = NULL;
    // rsp
    UINT32 uHttpStatusCode = SERVICE_CALL_RESULT_NOT_SET;
    HttpResponseContext* pHttpRspCtx = NULL;
    PCHAR pResponseStr;
    UINT32 resultLen;

    CHK(NULL != (pHost = (PCHAR) MEMALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pUrl = (PCHAR) MEMALLOC(STRLEN(pSignalingClient->pChannelInfo->pControlPlaneUrl) + STRLEN(API_CREATE_SIGNALING_CHANNEL) + 1)),
        STATUS_HTTP_NOT_ENOUGH_MEMORY);
    // Create the API url
    STRCPY(pUrl, pSignalingClient->pChannelInfo->pControlPlaneUrl);
    STRCAT(pUrl, API_CREATE_SIGNALING_CHANNEL);
    httpBodyLen = SIZEOF(BODY_TEMPLATE_CREATE_CHANNEL) + STRLEN(pChannelInfo->pChannelName) + 1;
    CHK(NULL != (pHttpBody = (CHAR*) MEMALLOC(httpBodyLen)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    /* generate HTTP request body */
    SNPRINTF(pHttpBody, httpBodyLen, BODY_TEMPLATE_CREATE_CHANNEL, pChannelInfo->pChannelName);
    // Create the request info with the body
    CHK_STATUS(createRequestInfo(pUrl, pHttpBody, pSignalingClient->pChannelInfo->pRegion, (PCHAR) pSignalingClient->pChannelInfo->pCertPath, NULL,
                                 NULL, SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, API_CALL_CONNECTION_TIMEOUT,
                                 API_CALL_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pSignalingClient->pAwsCredentials, &pRequestInfo));

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (pNetworkContext = (NetworkContext_t*) MEMALLOC(SIZEOF(NetworkContext_t))), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK_STATUS(initNetworkContext(pNetworkContext));

    httpPackSendBuf(pRequestInfo, HTTP_REQUEST_VERB_POST_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pNetworkContext->pHttpSendBuffer,
                    MAX_HTTP_SEND_BUFFER_LEN, FALSE, TRUE, NULL);

    for (uConnectionRetryCnt = 0; uConnectionRetryCnt < API_CALL_CONNECTING_RETRY; uConnectionRetryCnt++) {
        DLOGD("pHost:%s, pUrl:%s", pHost, pUrl);
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

    CHK(httpParserStart(&pHttpRspCtx, (CHAR*) pNetworkContext->pHttpRecvBuffer, (UINT32) uBytesReceived, NULL) == STATUS_SUCCESS,
        STATUS_HTTP_PARSER_ERROR);

    pResponseStr = httpParserGetHttpBodyLocation(pHttpRspCtx);
    resultLen = httpParserGetHttpBodyLength(pHttpRspCtx);
    uHttpStatusCode = httpParserGetHttpStatusCode(pHttpRspCtx);

    /* Check HTTP results */
    CHK(uHttpStatusCode == SERVICE_CALL_RESULT_OK && resultLen != 0 && pResponseStr != NULL, STATUS_HTTP_STATUS_CODE_ERROR);
    CHK(http_api_rsp_createChannel((const CHAR*) pResponseStr, resultLen, pSignalingClient) == STATUS_SUCCESS, STATUS_HTTP_RSP_ERROR);

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pHttpStatusCode != NULL) {
        *pHttpStatusCode = uHttpStatusCode;
    }

    if (pNetworkContext != NULL) {
        disconnectFromServer(pNetworkContext);
        terminateNetworkContext(pNetworkContext);
        MEMFREE(pNetworkContext);
    }

    if (pHttpRspCtx != NULL) {
        retStatus = httpParserDetroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGE("destroying http parset failed. \n");
        }
    }
    SAFE_MEMFREE(pHttpBody);
    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    freeRequestInfo(&pRequestInfo);

    HTTP_API_EXIT();
    return retStatus;
}

STATUS http_api_describeChannel(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode)
{
    HTTP_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PChannelInfo pChannelInfo = pSignalingClient->pChannelInfo;

    /* Variables for network connection */
    NetworkContext_t* pNetworkContext = NULL;
    SIZE_T uConnectionRetryCnt = 0;
    UINT32 uBytesToSend = 0, uBytesReceived = 0;

    /* Variables for HTTP request */
    // http req.
    PCHAR pUrl = NULL;
    PRequestInfo pRequestInfo = NULL;
    PCHAR pHttpBody = NULL;
    UINT32 httpBodyLen;
    PCHAR pHost = NULL;
    // rsp
    UINT32 uHttpStatusCode = 0;
    HttpResponseContext* pHttpRspCtx = NULL;
    PCHAR pResponseStr;
    UINT32 resultLen;

    CHK(NULL != (pHost = (PCHAR) MEMALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pUrl = (PCHAR) MEMALLOC(STRLEN(pSignalingClient->pChannelInfo->pControlPlaneUrl) + STRLEN(API_DESCRIBE_SIGNALING_CHANNEL) + 1)),
        STATUS_HTTP_NOT_ENOUGH_MEMORY);
    httpBodyLen = STRLEN(BODY_TEMPLATE_DESCRIBE_CHANNEL) + STRLEN(pSignalingClient->pChannelInfo->pChannelName) + 1;
    CHK(NULL != (pHttpBody = (PCHAR) MEMALLOC(httpBodyLen)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    // Create the http url
    STRCPY(pUrl, pSignalingClient->pChannelInfo->pControlPlaneUrl);
    STRCAT(pUrl, API_DESCRIBE_SIGNALING_CHANNEL);
    // create the http body
    SNPRINTF(pHttpBody, httpBodyLen, BODY_TEMPLATE_DESCRIBE_CHANNEL, pSignalingClient->pChannelInfo->pChannelName);

    // Create the request info with the body
    CHK_STATUS(createRequestInfo(pUrl, pHttpBody, pSignalingClient->pChannelInfo->pRegion, pSignalingClient->pChannelInfo->pCertPath, NULL, NULL,
                                 SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, API_CALL_CONNECTION_TIMEOUT,
                                 API_CALL_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pSignalingClient->pAwsCredentials, &pRequestInfo));

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (pNetworkContext = (NetworkContext_t*) MEMALLOC(SIZEOF(NetworkContext_t))), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    CHK_STATUS(initNetworkContext(pNetworkContext));

    httpPackSendBuf(pRequestInfo, HTTP_REQUEST_VERB_POST_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pNetworkContext->pHttpSendBuffer,
                    MAX_HTTP_SEND_BUFFER_LEN, FALSE, TRUE, NULL);

    for (uConnectionRetryCnt = 0; uConnectionRetryCnt < API_CALL_CONNECTING_RETRY; uConnectionRetryCnt++) {
        DLOGD("pHost:%s, pUrl:%s", pHost, pUrl);
        if ((retStatus = connectToServer(pNetworkContext, pHost, API_ENDPOINT_TCP_PORT)) == STATUS_SUCCESS) {
            break;
        }
        THREAD_SLEEP(API_CALL_CONNECTING_RETRY_INTERVAL_IN_MS * HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }
    CHK_STATUS(retStatus);

    uBytesToSend = STRLEN((PCHAR) pNetworkContext->pHttpSendBuffer);
    DLOGD("pNetworkContext->pHttpSendBuffer:%s", pNetworkContext->pHttpSendBuffer);
    CHK(uBytesToSend == networkSend(pNetworkContext, pNetworkContext->pHttpSendBuffer, uBytesToSend), STATUS_SEND_DATA_FAILED);
    uBytesReceived = networkRecv(pNetworkContext, pNetworkContext->pHttpRecvBuffer, pNetworkContext->uHttpRecvBufferLen);
    DLOGD("pNetworkContext->pHttpRecvBuffer:%s", pNetworkContext->pHttpRecvBuffer);
    CHK(uBytesReceived > 0, STATUS_RECV_DATA_FAILED);

    CHK(httpParserStart(&pHttpRspCtx, (CHAR*) pNetworkContext->pHttpRecvBuffer, (UINT32) uBytesReceived, NULL) == STATUS_SUCCESS,
        STATUS_HTTP_PARSER_ERROR);
    pResponseStr = httpParserGetHttpBodyLocation(pHttpRspCtx);
    resultLen = httpParserGetHttpBodyLength(pHttpRspCtx);
    uHttpStatusCode = httpParserGetHttpStatusCode(pHttpRspCtx);

    /* Check HTTP results */
    CHK(uHttpStatusCode == SERVICE_CALL_RESULT_OK && resultLen != 0 && pResponseStr != NULL, STATUS_HTTP_STATUS_CODE_ERROR);
    CHK(http_api_rsp_describeChannel((const CHAR*) pResponseStr, resultLen, pSignalingClient) == STATUS_SUCCESS, STATUS_HTTP_RSP_ERROR);

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pHttpStatusCode != NULL) {
        *pHttpStatusCode = uHttpStatusCode;
    }

    if (pNetworkContext != NULL) {
        disconnectFromServer(pNetworkContext);
        terminateNetworkContext(pNetworkContext);
        MEMFREE(pNetworkContext);
    }
    if (pHttpRspCtx != NULL) {
        retStatus = httpParserDetroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGE("destroying http parset failed. \n");
        }
    }
    SAFE_MEMFREE(pHttpBody);
    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    freeRequestInfo(&pRequestInfo);

    HTTP_API_EXIT();
    return retStatus;
}

STATUS http_api_getChannelEndpoint(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode)
{
    HTTP_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PChannelInfo pChannelInfo = pSignalingClient->pChannelInfo;

    /* Variables for network connection */
    NetworkContext_t* pNetworkContext = NULL;
    SIZE_T uConnectionRetryCnt = 0;
    UINT32 uBytesToSend = 0, uBytesReceived = 0;

    /* Variables for HTTP request */
    PCHAR pUrl = NULL;
    PRequestInfo pRequestInfo = NULL;
    PCHAR pHttpBody = NULL;
    UINT32 httpBodyLen;
    PCHAR pHost = NULL;
    // rsp
    UINT32 uHttpStatusCode = 0;
    HttpResponseContext* pHttpRspCtx = NULL;
    PCHAR pResponseStr;
    UINT32 resultLen;

    CHK(NULL != (pHost = (PCHAR) MEMALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pUrl = (PCHAR) MEMALLOC(STRLEN(pSignalingClient->pChannelInfo->pControlPlaneUrl) + STRLEN(API_GET_SIGNALING_CHANNEL_ENDPOINT) + 1)),
        STATUS_HTTP_NOT_ENOUGH_MEMORY);

    // Create the API url
    STRCPY(pUrl, pSignalingClient->pChannelInfo->pControlPlaneUrl);
    STRCAT(pUrl, API_GET_SIGNALING_CHANNEL_ENDPOINT);
    httpBodyLen = SIZEOF(BODY_TEMPLATE_GET_CHANNEL_ENDPOINT) + STRLEN(pSignalingClient->channelDescription.channelArn) +
        STRLEN(API_CALL_CHANNEL_PROTOCOL) + STRLEN(getStringFromChannelRoleType(pChannelInfo->channelRoleType)) + 1;
    CHK(NULL != (pHttpBody = (PCHAR) MEMALLOC(httpBodyLen)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    /* generate HTTP request body */
    SNPRINTF(pHttpBody, httpBodyLen, BODY_TEMPLATE_GET_CHANNEL_ENDPOINT, pSignalingClient->channelDescription.channelArn, API_CALL_CHANNEL_PROTOCOL,
             getStringFromChannelRoleType(pChannelInfo->channelRoleType));
    // Create the request info with the body
    CHK_STATUS(createRequestInfo(pUrl, pHttpBody, pSignalingClient->pChannelInfo->pRegion, pSignalingClient->pChannelInfo->pCertPath, NULL, NULL,
                                 SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, API_CALL_CONNECTION_TIMEOUT,
                                 API_CALL_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pSignalingClient->pAwsCredentials, &pRequestInfo));

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (pNetworkContext = (NetworkContext_t*) MEMALLOC(SIZEOF(NetworkContext_t))), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK_STATUS(initNetworkContext(pNetworkContext) != STATUS_SUCCESS);

    httpPackSendBuf(pRequestInfo, HTTP_REQUEST_VERB_POST_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pNetworkContext->pHttpSendBuffer,
                    MAX_HTTP_SEND_BUFFER_LEN, FALSE, TRUE, NULL);

    for (uConnectionRetryCnt = 0; uConnectionRetryCnt < API_CALL_CONNECTING_RETRY; uConnectionRetryCnt++) {
        DLOGD("pHost:%s, pUrl:%s", pHost, pUrl);
        if ((retStatus = connectToServer(pNetworkContext, pHost, API_ENDPOINT_TCP_PORT)) == STATUS_SUCCESS) {
            break;
        }
        THREAD_SLEEP(API_CALL_CONNECTING_RETRY_INTERVAL_IN_MS * HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }
    CHK_STATUS(retStatus);

    uBytesToSend = STRLEN((PCHAR) pNetworkContext->pHttpSendBuffer);
    DLOGD("pNetworkContext->pHttpSendBuffer:%s", pNetworkContext->pHttpSendBuffer);
    CHK(uBytesToSend == networkSend(pNetworkContext, pNetworkContext->pHttpSendBuffer, uBytesToSend), STATUS_SEND_DATA_FAILED);
    uBytesReceived = networkRecv(pNetworkContext, pNetworkContext->pHttpRecvBuffer, pNetworkContext->uHttpRecvBufferLen);
    DLOGD("pNetworkContext->pHttpRecvBuffer(%d):%s", uBytesReceived, pNetworkContext->pHttpRecvBuffer);
    CHK(uBytesReceived > 0, STATUS_RECV_DATA_FAILED);

    CHK(httpParserStart(&pHttpRspCtx, (CHAR*) pNetworkContext->pHttpRecvBuffer, (UINT32) uBytesReceived, NULL) == STATUS_SUCCESS,
        STATUS_HTTP_PARSER_ERROR);
    pResponseStr = httpParserGetHttpBodyLocation(pHttpRspCtx);
    resultLen = httpParserGetHttpBodyLength(pHttpRspCtx);
    uHttpStatusCode = httpParserGetHttpStatusCode(pHttpRspCtx);

    /* Check HTTP results */
    CHK(uHttpStatusCode == SERVICE_CALL_RESULT_OK && resultLen != 0 && pResponseStr != NULL, STATUS_HTTP_STATUS_CODE_ERROR);
    CHK(http_api_rsp_getChannelEndpoint((const CHAR*) pResponseStr, resultLen, pSignalingClient) == STATUS_SUCCESS, STATUS_HTTP_RSP_ERROR);
    /* We got a success response here. */

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pHttpStatusCode != NULL) {
        *pHttpStatusCode = uHttpStatusCode;
    }

    if (pNetworkContext != NULL) {
        disconnectFromServer(pNetworkContext);
        terminateNetworkContext(pNetworkContext);
        MEMFREE(pNetworkContext);
    }

    if (pHttpRspCtx != NULL) {
        retStatus = httpParserDetroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGE("destroying http parset failed. \n");
        }
    }
    SAFE_MEMFREE(pHttpBody);
    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    freeRequestInfo(&pRequestInfo);
    HTTP_API_EXIT();
    return retStatus;
}

STATUS http_api_getIceConfig(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode)
{
    HTTP_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PChannelInfo pChannelInfo = pSignalingClient->pChannelInfo;

    /* Variables for network connection */
    NetworkContext_t* pNetworkContext = NULL;
    SIZE_T uConnectionRetryCnt = 0;
    UINT32 uBytesToSend = 0, uBytesReceived = 0;

    /* Variables for HTTP request */
    // http req.
    PCHAR pUrl = NULL;
    PRequestInfo pRequestInfo = NULL;
    PCHAR pHttpBody = NULL;
    UINT32 httpBodyLen;
    PCHAR pHost = NULL;
    // rsp
    UINT32 uHttpStatusCode = 0;
    HttpResponseContext* pHttpRspCtx = NULL;
    PCHAR pResponseStr;
    UINT32 resultLen;

    CHK(NULL != (pHost = (PCHAR) MEMALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pUrl = (PCHAR) MEMALLOC(STRLEN(pSignalingClient->channelEndpointHttps) + STRLEN(API_GET_ICE_CONFIG) + 1)),
        STATUS_HTTP_NOT_ENOUGH_MEMORY);
    httpBodyLen = SIZEOF(BODY_TEMPLATE_GET_ICE_CONFIG) + STRLEN(pSignalingClient->channelDescription.channelArn) +
        STRLEN(pSignalingClient->clientInfo.signalingClientInfo.clientId) + 1;
    CHK(NULL != (pHttpBody = (PCHAR) MEMALLOC(httpBodyLen)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    STRCPY(pUrl, pSignalingClient->channelEndpointHttps);
    STRCAT(pUrl, API_GET_ICE_CONFIG);
    /* generate HTTP request body */
    SNPRINTF(pHttpBody, httpBodyLen, BODY_TEMPLATE_GET_ICE_CONFIG, pSignalingClient->channelDescription.channelArn,
             pSignalingClient->clientInfo.signalingClientInfo.clientId);

    // Create the request info with the body
    CHK_STATUS(createRequestInfo(pUrl, pHttpBody, pSignalingClient->pChannelInfo->pRegion, pSignalingClient->pChannelInfo->pCertPath, NULL, NULL,
                                 SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, API_CALL_CONNECTION_TIMEOUT,
                                 API_CALL_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pSignalingClient->pAwsCredentials, &pRequestInfo));

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (pNetworkContext = (NetworkContext_t*) MEMALLOC(SIZEOF(NetworkContext_t))), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    CHK_STATUS(initNetworkContext(pNetworkContext) != STATUS_SUCCESS);

    httpPackSendBuf(pRequestInfo, HTTP_REQUEST_VERB_POST_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pNetworkContext->pHttpSendBuffer,
                    MAX_HTTP_SEND_BUFFER_LEN, FALSE, TRUE, NULL);

    for (uConnectionRetryCnt = 0; uConnectionRetryCnt < API_CALL_CONNECTING_RETRY; uConnectionRetryCnt++) {
        DLOGD("pHost:%s, pUrl:%s", pHost, pUrl);
        if ((retStatus = connectToServer(pNetworkContext, pHost, API_ENDPOINT_TCP_PORT)) == STATUS_SUCCESS) {
            break;
        }
        THREAD_SLEEP(API_CALL_CONNECTING_RETRY_INTERVAL_IN_MS * HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }
    CHK_STATUS(retStatus);

    uBytesToSend = STRLEN((PCHAR) pNetworkContext->pHttpSendBuffer);
    DLOGD("pNetworkContext->pHttpSendBuffer:%s", pNetworkContext->pHttpSendBuffer);
    CHK(uBytesToSend == networkSend(pNetworkContext, pNetworkContext->pHttpSendBuffer, uBytesToSend), STATUS_SEND_DATA_FAILED);
    uBytesReceived = networkRecv(pNetworkContext, pNetworkContext->pHttpRecvBuffer, pNetworkContext->uHttpRecvBufferLen);
    DLOGD("pNetworkContext->pHttpRecvBuffer(%d):%s", uBytesReceived, pNetworkContext->pHttpRecvBuffer);
    CHK(uBytesReceived > 0, STATUS_RECV_DATA_FAILED);

    CHK(httpParserStart(&pHttpRspCtx, (CHAR*) pNetworkContext->pHttpRecvBuffer, (UINT32) uBytesReceived, NULL) == STATUS_SUCCESS,
        STATUS_HTTP_PARSER_ERROR);

    pResponseStr = httpParserGetHttpBodyLocation(pHttpRspCtx);
    resultLen = httpParserGetHttpBodyLength(pHttpRspCtx);
    uHttpStatusCode = httpParserGetHttpStatusCode(pHttpRspCtx);

    /* Check HTTP results */
    CHK(uHttpStatusCode == SERVICE_CALL_RESULT_OK && resultLen != 0 && pResponseStr != NULL, STATUS_HTTP_STATUS_CODE_ERROR);
    CHK(http_api_rsp_getIceConfig((const CHAR*) pResponseStr, resultLen, pSignalingClient) == STATUS_SUCCESS, STATUS_HTTP_RSP_ERROR);

    if (retStatus != STATUS_SUCCESS) {
        DLOGD("parse failed.");
    }

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pHttpStatusCode != NULL) {
        *pHttpStatusCode = uHttpStatusCode;
    }

    if (pNetworkContext != NULL) {
        disconnectFromServer(pNetworkContext);
        terminateNetworkContext(pNetworkContext);
        MEMFREE(pNetworkContext);
    }
    if (pHttpRspCtx != NULL) {
        retStatus = httpParserDetroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGE("destroying http parset failed. \n");
        }
    }
    SAFE_MEMFREE(pHttpBody);
    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    freeRequestInfo(&pRequestInfo);
    HTTP_API_EXIT();
    return retStatus;
}

STATUS http_api_deleteChannel(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode)
{
    HTTP_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PChannelInfo pChannelInfo = pSignalingClient->pChannelInfo;
    NetworkContext_t* pNetworkContext = NULL;
    SIZE_T uConnectionRetryCnt = 0;
    UINT32 uBytesToSend = 0, uBytesReceived = 0;
    PCHAR pUrl = NULL;
    PRequestInfo pRequestInfo = NULL;
    PCHAR pHttpBody = NULL;
    UINT32 httpBodyLen;
    PCHAR pHost = NULL;
    UINT32 uHttpStatusCode = 0;
    HttpResponseContext* pHttpRspCtx = NULL;

    CHK(NULL != (pHost = (PCHAR) MEMALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pUrl = (PCHAR) MEMALLOC(STRLEN(pSignalingClient->pChannelInfo->pControlPlaneUrl) + STRLEN(API_DELETE_SIGNALING_CHANNEL) + 1)),
        STATUS_HTTP_NOT_ENOUGH_MEMORY);

    // Create the API url
    STRCPY(pUrl, pSignalingClient->pChannelInfo->pControlPlaneUrl);
    STRCAT(pUrl, API_DELETE_SIGNALING_CHANNEL);
    httpBodyLen = SIZEOF(BODY_TEMPLATE_DELETE_CHANNEL) + STRLEN(pSignalingClient->channelDescription.channelArn) +
        STRLEN(pSignalingClient->channelDescription.updateVersion) + 1;
    CHK(NULL != (pHttpBody = (CHAR*) MEMALLOC(httpBodyLen)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    /* generate HTTP request body */
    SNPRINTF(pHttpBody, httpBodyLen, BODY_TEMPLATE_DELETE_CHANNEL, pSignalingClient->channelDescription.channelArn,
             pSignalingClient->channelDescription.updateVersion);
    // Create the request info with the body
    CHK_STATUS(createRequestInfo(pUrl, pHttpBody, pSignalingClient->pChannelInfo->pRegion, (PCHAR) pSignalingClient->pChannelInfo->pCertPath, NULL,
                                 NULL, SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, API_CALL_CONNECTION_TIMEOUT,
                                 API_CALL_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pSignalingClient->pAwsCredentials, &pRequestInfo));

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (pNetworkContext = (NetworkContext_t*) MEMALLOC(SIZEOF(NetworkContext_t))), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK_STATUS(initNetworkContext(pNetworkContext));

    httpPackSendBuf(pRequestInfo, HTTP_REQUEST_VERB_POST_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pNetworkContext->pHttpSendBuffer,
                    MAX_HTTP_SEND_BUFFER_LEN, FALSE, TRUE, NULL);

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

    CHK(httpParserStart(&pHttpRspCtx, (CHAR*) pNetworkContext->pHttpRecvBuffer, (UINT32) uBytesReceived, NULL) == STATUS_SUCCESS,
        STATUS_HTTP_PARSER_ERROR);
    uHttpStatusCode = httpParserGetHttpStatusCode(pHttpRspCtx);

    /* Check HTTP results */
    CHK(uHttpStatusCode == SERVICE_CALL_RESULT_OK || uHttpStatusCode == SERVICE_CALL_RESOURCE_NOT_FOUND, retStatus);
    ATOMIC_STORE_BOOL(&pSignalingClient->deleted, TRUE);

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pHttpStatusCode != NULL) {
        *pHttpStatusCode = uHttpStatusCode;
    }

    if (pNetworkContext != NULL) {
        disconnectFromServer(pNetworkContext);
        terminateNetworkContext(pNetworkContext);
        MEMFREE(pNetworkContext);
    }

    if (pHttpRspCtx != NULL) {
        retStatus = httpParserDetroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGE("destroying http parset failed. \n");
        }
    }
    SAFE_MEMFREE(pHttpBody);
    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    freeRequestInfo(&pRequestInfo);

    HTTP_API_EXIT();
    return retStatus;
}
// https://docs.aws.amazon.com/iot/latest/developerguide/authorizing-direct-aws.html
// STATUS http_api_getIotCredential(PSignalingClient pSignalingClient, UINT64 time, PIotCredentialProvider pIotCredentialProvider)
STATUS http_api_getIotCredential(PIotCredentialProvider pIotCredentialProvider)
{
    HTTP_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    // PChannelInfo pChannelInfo = pSignalingClient->pChannelInfo;

    /* Variables for network connection */
    NetworkContext_t* pNetworkContext = NULL;
    SIZE_T uConnectionRetryCnt = 0;
    UINT32 uBytesToSend = 0, uBytesReceived = 0;

    /* Variables for HTTP request */
    PCHAR pUrl = NULL;
    UINT32 formatLen = 0;

    PRequestInfo pRequestInfo = NULL;
    PCHAR pHttpBody = NULL;
    UINT32 httpBodyLen = 0;
    PCHAR pHost = NULL;
    // rsp
    UINT32 uHttpStatusCode = 0;
    HttpResponseContext* pHttpRspCtx = NULL;
    PCHAR pResponseStr;
    UINT32 resultLen;

    CHK(NULL != (pHost = (PCHAR) MEMALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    CHK(NULL !=
            (pUrl = (PCHAR) MEMALLOC(STRLEN(CONTROL_PLANE_URI_PREFIX) + STRLEN(pIotCredentialProvider->iotGetCredentialEndpoint) +
                                     STRLEN(ROLE_ALIASES_PATH) + STRLEN("/") + STRLEN(pIotCredentialProvider->roleAlias) +
                                     STRLEN(CREDENTIAL_SERVICE) + 1)),
        STATUS_HTTP_NOT_ENOUGH_MEMORY);

    // Create the API url

    formatLen = SNPRINTF(pUrl, MAX_URI_CHAR_LEN, "%s%s%s%c%s%s", CONTROL_PLANE_URI_PREFIX, pIotCredentialProvider->iotGetCredentialEndpoint,
                         ROLE_ALIASES_PATH, '/', pIotCredentialProvider->roleAlias, CREDENTIAL_SERVICE);
    CHK(formatLen > 0 && formatLen < MAX_URI_CHAR_LEN, STATUS_IOT_FAILED);

    // Create the request info with the body
    CHK_STATUS(createRequestInfo(pUrl, pHttpBody, DEFAULT_AWS_REGION, pIotCredentialProvider->caCertPath, pIotCredentialProvider->certPath,
                                 pIotCredentialProvider->privateKeyPath, SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, DEFAULT_USER_AGENT_NAME,
                                 API_CALL_CONNECTION_TIMEOUT, API_CALL_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pIotCredentialProvider->pAwsCredentials, &pRequestInfo));

    // CHK_STATUS(createRequestInfo(pUrl, pHttpBody, pSignalingClient->pChannelInfo->pRegion, pIotCredentialProvider->caCertPath,
    // pIotCredentialProvider->certPath,
    //                             pIotCredentialProvider->privateKeyPath,
    //                             SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, API_CALL_CONNECTION_TIMEOUT,
    //                             API_CALL_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
    //                             pIotCredentialProvider->pAwsCredentials, &pRequestInfo));

    CHK_STATUS(setRequestHeader(pRequestInfo, IOT_THING_NAME_HEADER, 0, pIotCredentialProvider->thingName, 0));
    CHK_STATUS(setRequestHeader(pRequestInfo, "accept", 0, "*/*", 0));

    /* Initialize and generate HTTP request, then send it. */

    CHK(NULL != (pNetworkContext = (NetworkContext_t*) MEMALLOC(SIZEOF(NetworkContext_t))), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK_STATUS(initNetworkContext(pNetworkContext) != STATUS_SUCCESS);

    httpPackSendBuf(pRequestInfo, HTTP_REQUEST_VERB_GET_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pNetworkContext->pHttpSendBuffer,
                    MAX_HTTP_SEND_BUFFER_LEN, FALSE, FALSE, NULL);

    for (uConnectionRetryCnt = 0; uConnectionRetryCnt < API_CALL_CONNECTING_RETRY; uConnectionRetryCnt++) {
        // connectToServerWithX509Cert(NetworkContext_t* pNetworkContext, const PCHAR pServerHost, const PCHAR pServerPort, const PCHAR pRootCA, const
        // PCHAR pCertificate, const PCHAR pPrivateKey)
        DLOGD("pHost:%s, pUrl:%s", pHost, pUrl);
        if ((retStatus = connectToServerWithX509Cert(pNetworkContext, pHost, API_ENDPOINT_TCP_PORT, pIotCredentialProvider->caCertPath,
                                                     pIotCredentialProvider->certPath, pIotCredentialProvider->privateKeyPath)) == STATUS_SUCCESS) {
            break;
        }
        // if ((retStatus = connectToServer(pNetworkContext, pHost, API_ENDPOINT_TCP_PORT)) == STATUS_SUCCESS) {
        //    break;
        //}
        THREAD_SLEEP(API_CALL_CONNECTING_RETRY_INTERVAL_IN_MS * HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }

    CHK_STATUS(retStatus);

    uBytesToSend = STRLEN((PCHAR) pNetworkContext->pHttpSendBuffer);

    DLOGD("pNetworkContext->pHttpSendBuffer:%s", pNetworkContext->pHttpSendBuffer);
    CHK(uBytesToSend == networkSend(pNetworkContext, pNetworkContext->pHttpSendBuffer, uBytesToSend), STATUS_SEND_DATA_FAILED);

    uBytesReceived = networkRecv(pNetworkContext, pNetworkContext->pHttpRecvBuffer, pNetworkContext->uHttpRecvBufferLen);

    CHK(uBytesReceived > 0, STATUS_RECV_DATA_FAILED);
    DLOGD("pNetworkContext->pHttpRecvBuffer:%s", pNetworkContext->pHttpRecvBuffer);
    pNetworkContext->pHttpRecvBuffer[uBytesReceived] = '\0';
    CHK(httpParserStart(&pHttpRspCtx, (CHAR*) pNetworkContext->pHttpRecvBuffer, (UINT32) uBytesReceived, NULL) == STATUS_SUCCESS,
        STATUS_HTTP_PARSER_ERROR);
    pResponseStr = httpParserGetHttpBodyLocation(pHttpRspCtx);
    resultLen = httpParserGetHttpBodyLength(pHttpRspCtx);
    uHttpStatusCode = httpParserGetHttpStatusCode(pHttpRspCtx);

    // ATOMIC_STORE(&pSignalingClient->result, (SIZE_T) uHttpStatusCode);
    /* Check HTTP results */
    // CHK((SERVICE_CALL_RESULT) ATOMIC_LOAD(&pSignalingClient->result) == SERVICE_CALL_RESULT_OK && resultLen != 0 && pResponseStr != NULL,
    // retStatus);

    // CHK_STATUS(http_api_rsp_getIoTCredential((const CHAR*) pResponseStr, resultLen, pSignalingClient));
    CHK_STATUS(http_api_rsp_getIoTCredential(pIotCredentialProvider, (const CHAR*) pResponseStr, resultLen));
    /* We got a success response here. */

CleanUp:
    CHK_LOG_ERR(retStatus);
    if (pNetworkContext != NULL) {
        disconnectFromServer(pNetworkContext);
        terminateNetworkContext(pNetworkContext);
        MEMFREE(pNetworkContext);
    }

    if (pHttpRspCtx != NULL) {
        retStatus = httpParserDetroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGE("destroying http parset failed. \n");
        }
    }
    SAFE_MEMFREE(pHttpBody);
    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    freeRequestInfo(&pRequestInfo);
    HTTP_API_EXIT();
    return retStatus;
}
