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
#define LOG_CLASS "HttpApi"
#include "http_api.h"
#include "http_helper.h"
#include "channel_info.h"
#include "wss_api.h"
#include "netio.h"

/******************************************************************************
 * DEFINITION
 ******************************************************************************/
#define HTTP_API_ENTER() // DLOGD("%s(%d) enter", __func__, __LINE__)
#define HTTP_API_EXIT()  // DLOGD("%s(%d) exit", __func__, __LINE__)

#define HTTP_API_SECURE_PORT          "443"
#define HTTP_API_CONNECTION_TIMEOUT   (2 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define HTTP_API_COMPLETION_TIMEOUT   (5 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define HTTP_API_CHANNEL_PROTOCOL     "\"WSS\", \"HTTPS\""
#define HTTP_API_SEND_BUFFER_MAX_SIZE (2048)
#define HTTP_API_RECV_BUFFER_MAX_SIZE (2048)

/**
 * @brief API postfix definitions
 *  https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_CreateSignalingChannel.html
 *  https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_DescribeSignalingChannel.html
 *  https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_GetSignalingChannelEndpoint.html
 *  https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_DeleteSignalingChannel.html
 *  https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_AWSAcuitySignalingService_GetIceServerConfig.html
 *  https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_ListSignalingChannels.html
 *  https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_UpdateSignalingChannel.html
 */
#define HTTP_API_CREATE_SIGNALING_CHANNEL       "/createSignalingChannel"
#define HTTP_API_DESCRIBE_SIGNALING_CHANNEL     "/describeSignalingChannel"
#define HTTP_API_GET_SIGNALING_CHANNEL_ENDPOINT "/getSignalingChannelEndpoint"
#define HTTP_API_DELETE_SIGNALING_CHANNEL       "/deleteSignalingChannel"
#define HTTP_API_GET_ICE_CONFIG                 "/v1/get-ice-server-config"
#define HTTP_API_LIST_SIGNALING_CHANNEL         "/listSignalingChannels"
#define HTTP_API_UPDATE_SIGNALING_CHANNEL       "/updateSignalingChannel"
#define HTTP_API_ROLE_ALIASES                   "/role-aliases"
#define HTTP_API_CREDENTIALS                    "/credentials"
#define HTTP_API_IOT_THING_NAME_HEADER          "x-amzn-iot-thingname"
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
// #TBD, need to add the rest part.
#define HTTP_API_BODY_CREATE_CHANNEL                                                                                                                 \
    "{\n\t\"ChannelName\": \"%s\""                                                                                                                   \
    "\n}"

// Parameterized string for TagStream API - we should have at least one tag
#define HTTP_API_BODY_TAGS ",\n\t\"Tags\": [%s\n\t]"
#define HTTP_API_BODY_TAG  "\n\t\t{\"Key\": \"%s\", \"Value\": \"%s\"},"
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
#define HTTP_API_BODY_DESCRIBE_CHANNEL "{\n\t\"ChannelName\": \"%s\"\n}"
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
#define HTTP_API_BODY_GET_CHANNEL_ENDPOINT                                                                                                           \
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
#define HTTP_API_BODY_DELETE_CHANNEL                                                                                                                 \
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
#define HTTP_API_BODY_GET_ICE_CONFIG                                                                                                                 \
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
//#define HTTP_API_BODY_LIST_CHANNELS

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
//#define HTTP_API_BODY_UPDATE_CHANNEL

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS http_api_createChannel(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode)
{
    HTTP_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PChannelInfo pChannelInfo = pSignalingClient->pChannelInfo;

    /* Variables for network connection */
    UINT32 uBytesReceived = 0;

    /* Variables for HTTP request */
    PCHAR pUrl = NULL;
    UINT32 urlLen;
    PCHAR pHttpBody = NULL;
    UINT32 httpBodyLen;
    PRequestInfo pRequestInfo = NULL;
    PCHAR pHost = NULL;
    // rsp
    UINT32 uHttpStatusCode = HTTP_STATUS_NONE;
    HttpResponseContext* pHttpRspCtx = NULL;
    PCHAR pResponseStr;
    UINT32 resultLen;
    // new net io.
    NetIoHandle xNetIoHandle = NULL;
    uint8_t* pHttpSendBuffer = NULL;
    uint8_t* pHttpRecvBuffer = NULL;

    CHK(NULL != (pHost = (PCHAR) MEMCALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    urlLen = STRLEN(pSignalingClient->pChannelInfo->pControlPlaneUrl) + STRLEN(HTTP_API_CREATE_SIGNALING_CHANNEL) + 1;
    CHK(NULL != (pUrl = (PCHAR) MEMCALLOC(urlLen, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    httpBodyLen = SIZEOF(HTTP_API_BODY_CREATE_CHANNEL) + STRLEN(pChannelInfo->pChannelName) + 1;
    CHK(NULL != (pHttpBody = (CHAR*) MEMCALLOC(httpBodyLen, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpSendBuffer = (uint8_t*) MEMCALLOC(HTTP_API_SEND_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpRecvBuffer = (uint8_t*) MEMCALLOC(HTTP_API_RECV_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    // Create the API url
    CHK(SNPRINTF(pUrl, urlLen, "%s%s", pSignalingClient->pChannelInfo->pControlPlaneUrl, HTTP_API_CREATE_SIGNALING_CHANNEL) > 0,
        STATUS_HTTP_BUF_OVERFLOW);
    /* generate HTTP request body */
    CHK(SNPRINTF(pHttpBody, httpBodyLen, HTTP_API_BODY_CREATE_CHANNEL, pChannelInfo->pChannelName) > 0, STATUS_HTTP_BUF_OVERFLOW);
    // Create the request info with the body
    CHK_STATUS(createRequestInfo(pUrl, pHttpBody, pSignalingClient->pChannelInfo->pRegion, (PCHAR) pSignalingClient->pChannelInfo->pCertPath, NULL,
                                 NULL, SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, HTTP_API_CONNECTION_TIMEOUT,
                                 HTTP_API_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pSignalingClient->pAwsCredentials, &pRequestInfo));

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (xNetIoHandle = NetIo_create()), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK_STATUS(NetIo_setRecvTimeout(xNetIoHandle, HTTP_API_COMPLETION_TIMEOUT));
    CHK_STATUS(NetIo_setSendTimeout(xNetIoHandle, HTTP_API_COMPLETION_TIMEOUT));

    CHK_STATUS(http_req_pack(pRequestInfo, HTTP_REQUEST_VERB_POST_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pHttpSendBuffer,
                             HTTP_API_SEND_BUFFER_MAX_SIZE, FALSE, TRUE, NULL));

    CHK_STATUS(NetIo_connect(xNetIoHandle, pHost, HTTP_API_SECURE_PORT));

    CHK(NetIo_send(xNetIoHandle, (unsigned char*) pHttpSendBuffer, STRLEN((PCHAR) pHttpSendBuffer)) == STATUS_SUCCESS, STATUS_NET_SEND_DATA_FAILED);
    CHK_STATUS(NetIo_recv(xNetIoHandle, (unsigned char*) pHttpRecvBuffer, HTTP_API_RECV_BUFFER_MAX_SIZE, &uBytesReceived));

    CHK(uBytesReceived > 0, STATUS_NET_RECV_DATA_FAILED);

    CHK(http_parser_start(&pHttpRspCtx, (CHAR*) pHttpRecvBuffer, (UINT32) uBytesReceived, NULL) == STATUS_SUCCESS, STATUS_HTTP_PARSER_ERROR);

    pResponseStr = http_parser_getHttpBodyLocation(pHttpRspCtx);
    resultLen = http_parser_getHttpBodyLength(pHttpRspCtx);
    uHttpStatusCode = http_parser_getHttpStatusCode(pHttpRspCtx);

    /* Check HTTP results */
    CHK(uHttpStatusCode == HTTP_STATUS_OK && resultLen != 0 && pResponseStr != NULL, STATUS_HTTP_STATUS_CODE_ERROR);
    CHK(http_api_rsp_createChannel((const CHAR*) pResponseStr, resultLen, pSignalingClient) == STATUS_SUCCESS, STATUS_HTTP_RSP_ERROR);

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pHttpStatusCode != NULL) {
        *pHttpStatusCode = uHttpStatusCode;
    }

    if (xNetIoHandle != NULL) {
        NetIo_disconnect(xNetIoHandle);
        NetIo_terminate(xNetIoHandle);
    }

    if (pHttpRspCtx != NULL) {
        retStatus = http_parser_detroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGE("destroying http parset failed. \n");
        }
    }
    SAFE_MEMFREE(pHttpBody);
    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    SAFE_MEMFREE(pHttpSendBuffer);
    SAFE_MEMFREE(pHttpRecvBuffer);
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
    UINT32 uBytesReceived = 0;
    /* Variables for HTTP request */
    // http req.
    PCHAR pUrl = NULL;
    UINT32 urlLen;
    PCHAR pHttpBody = NULL;
    UINT32 httpBodyLen;
    PRequestInfo pRequestInfo = NULL;
    PCHAR pHost = NULL;
    // rsp
    UINT32 uHttpStatusCode = 0;
    HttpResponseContext* pHttpRspCtx = NULL;
    PCHAR pResponseStr;
    UINT32 resultLen;
    // new net io.
    NetIoHandle xNetIoHandle = NULL;
    uint8_t* pHttpSendBuffer = NULL;
    uint8_t* pHttpRecvBuffer = NULL;

    CHK(NULL != (pHost = (PCHAR) MEMCALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    urlLen = STRLEN(pSignalingClient->pChannelInfo->pControlPlaneUrl) + STRLEN(HTTP_API_DESCRIBE_SIGNALING_CHANNEL) + 1;
    CHK(NULL != (pUrl = (PCHAR) MEMCALLOC(urlLen, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    httpBodyLen = STRLEN(HTTP_API_BODY_DESCRIBE_CHANNEL) + STRLEN(pSignalingClient->pChannelInfo->pChannelName) + 1;
    CHK(NULL != (pHttpBody = (PCHAR) MEMALLOC(httpBodyLen)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpSendBuffer = (uint8_t*) MEMCALLOC(HTTP_API_SEND_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpRecvBuffer = (uint8_t*) MEMCALLOC(HTTP_API_RECV_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    // Create the http url
    CHK(SNPRINTF(pUrl, urlLen, "%s%s", pSignalingClient->pChannelInfo->pControlPlaneUrl, HTTP_API_DESCRIBE_SIGNALING_CHANNEL) > 0,
        STATUS_HTTP_BUF_OVERFLOW);
    // create the http body
    CHK(SNPRINTF(pHttpBody, httpBodyLen, HTTP_API_BODY_DESCRIBE_CHANNEL, pSignalingClient->pChannelInfo->pChannelName) > 0, STATUS_HTTP_BUF_OVERFLOW);

    // Create the request info with the body
    CHK_STATUS(createRequestInfo(pUrl, pHttpBody, pSignalingClient->pChannelInfo->pRegion, pSignalingClient->pChannelInfo->pCertPath, NULL, NULL,
                                 SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, HTTP_API_CONNECTION_TIMEOUT,
                                 HTTP_API_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pSignalingClient->pAwsCredentials, &pRequestInfo));

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (xNetIoHandle = NetIo_create()), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK_STATUS(NetIo_setRecvTimeout(xNetIoHandle, HTTP_API_COMPLETION_TIMEOUT));
    CHK_STATUS(NetIo_setSendTimeout(xNetIoHandle, HTTP_API_COMPLETION_TIMEOUT));

    CHK_STATUS(http_req_pack(pRequestInfo, HTTP_REQUEST_VERB_POST_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pHttpSendBuffer,
                             HTTP_API_SEND_BUFFER_MAX_SIZE, FALSE, TRUE, NULL));

    CHK_STATUS(NetIo_connect(xNetIoHandle, pHost, HTTP_API_SECURE_PORT));

    CHK(NetIo_send(xNetIoHandle, (unsigned char*) pHttpSendBuffer, STRLEN((PCHAR) pHttpSendBuffer)) == STATUS_SUCCESS, STATUS_NET_SEND_DATA_FAILED);
    CHK_STATUS(NetIo_recv(xNetIoHandle, (unsigned char*) pHttpRecvBuffer, HTTP_API_RECV_BUFFER_MAX_SIZE, &uBytesReceived));

    CHK(uBytesReceived > 0, STATUS_NET_RECV_DATA_FAILED);

    CHK(http_parser_start(&pHttpRspCtx, (CHAR*) pHttpRecvBuffer, (UINT32) uBytesReceived, NULL) == STATUS_SUCCESS, STATUS_HTTP_PARSER_ERROR);
    pResponseStr = http_parser_getHttpBodyLocation(pHttpRspCtx);
    resultLen = http_parser_getHttpBodyLength(pHttpRspCtx);
    uHttpStatusCode = http_parser_getHttpStatusCode(pHttpRspCtx);

    /* Check HTTP results */
    CHK(uHttpStatusCode == HTTP_STATUS_OK && resultLen != 0 && pResponseStr != NULL, STATUS_HTTP_STATUS_CODE_ERROR);
    CHK(http_api_rsp_describeChannel((const CHAR*) pResponseStr, resultLen, pSignalingClient) == STATUS_SUCCESS, STATUS_HTTP_RSP_ERROR);

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pHttpStatusCode != NULL) {
        *pHttpStatusCode = uHttpStatusCode;
    }

    if (xNetIoHandle != NULL) {
        NetIo_disconnect(xNetIoHandle);
        NetIo_terminate(xNetIoHandle);
    }

    if (pHttpRspCtx != NULL) {
        retStatus = http_parser_detroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGE("destroying http parset failed. \n");
        }
    }
    SAFE_MEMFREE(pHttpBody);
    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    SAFE_MEMFREE(pHttpSendBuffer);
    SAFE_MEMFREE(pHttpRecvBuffer);
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
    UINT32 uBytesReceived = 0;

    /* Variables for HTTP request */
    PCHAR pUrl = NULL;
    UINT32 urlLen;
    PRequestInfo pRequestInfo = NULL;
    PCHAR pHttpBody = NULL;
    UINT32 httpBodyLen;
    PCHAR pHost = NULL;
    // rsp
    UINT32 uHttpStatusCode = 0;
    HttpResponseContext* pHttpRspCtx = NULL;
    PCHAR pResponseStr;
    UINT32 resultLen;
    // new net io.
    NetIoHandle xNetIoHandle = NULL;
    uint8_t* pHttpSendBuffer = NULL;
    uint8_t* pHttpRecvBuffer = NULL;

    CHK(NULL != (pHost = (PCHAR) MEMCALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    urlLen = STRLEN(pSignalingClient->pChannelInfo->pControlPlaneUrl) + STRLEN(HTTP_API_GET_SIGNALING_CHANNEL_ENDPOINT) + 1;
    CHK(NULL != (pUrl = (PCHAR) MEMCALLOC(urlLen, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpSendBuffer = (uint8_t*) MEMCALLOC(HTTP_API_SEND_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpRecvBuffer = (uint8_t*) MEMCALLOC(HTTP_API_RECV_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    httpBodyLen = SIZEOF(HTTP_API_BODY_GET_CHANNEL_ENDPOINT) + STRLEN(pSignalingClient->channelDescription.channelArn) +
        STRLEN(HTTP_API_CHANNEL_PROTOCOL) + STRLEN(getStringFromChannelRoleType(pChannelInfo->channelRoleType)) + 1;
    CHK(NULL != (pHttpBody = (PCHAR) MEMALLOC(httpBodyLen)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    // Create the API url
    CHK(SNPRINTF(pUrl, urlLen, "%s%s", pSignalingClient->pChannelInfo->pControlPlaneUrl, HTTP_API_GET_SIGNALING_CHANNEL_ENDPOINT) > 0,
        STATUS_HTTP_BUF_OVERFLOW);
    /* generate HTTP request body */
    CHK(SNPRINTF(pHttpBody, httpBodyLen, HTTP_API_BODY_GET_CHANNEL_ENDPOINT, pSignalingClient->channelDescription.channelArn,
                 HTTP_API_CHANNEL_PROTOCOL, getStringFromChannelRoleType(pChannelInfo->channelRoleType)) > 0,
        STATUS_HTTP_BUF_OVERFLOW);

    // Create the request info with the body
    CHK_STATUS(createRequestInfo(pUrl, pHttpBody, pSignalingClient->pChannelInfo->pRegion, pSignalingClient->pChannelInfo->pCertPath, NULL, NULL,
                                 SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, HTTP_API_CONNECTION_TIMEOUT,
                                 HTTP_API_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pSignalingClient->pAwsCredentials, &pRequestInfo));

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (xNetIoHandle = NetIo_create()), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK_STATUS(NetIo_setRecvTimeout(xNetIoHandle, HTTP_API_COMPLETION_TIMEOUT));
    CHK_STATUS(NetIo_setSendTimeout(xNetIoHandle, HTTP_API_COMPLETION_TIMEOUT));

    CHK_STATUS(http_req_pack(pRequestInfo, HTTP_REQUEST_VERB_POST_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pHttpSendBuffer,
                             HTTP_API_SEND_BUFFER_MAX_SIZE, FALSE, TRUE, NULL));

    CHK_STATUS(NetIo_connect(xNetIoHandle, pHost, HTTP_API_SECURE_PORT));

    CHK(NetIo_send(xNetIoHandle, (unsigned char*) pHttpSendBuffer, STRLEN((PCHAR) pHttpSendBuffer)) == STATUS_SUCCESS, STATUS_NET_SEND_DATA_FAILED);
    CHK_STATUS(NetIo_recv(xNetIoHandle, (unsigned char*) pHttpRecvBuffer, HTTP_API_RECV_BUFFER_MAX_SIZE, &uBytesReceived));

    CHK(uBytesReceived > 0, STATUS_NET_RECV_DATA_FAILED);

    CHK(http_parser_start(&pHttpRspCtx, (CHAR*) pHttpRecvBuffer, (UINT32) uBytesReceived, NULL) == STATUS_SUCCESS, STATUS_HTTP_PARSER_ERROR);
    pResponseStr = http_parser_getHttpBodyLocation(pHttpRspCtx);
    resultLen = http_parser_getHttpBodyLength(pHttpRspCtx);
    uHttpStatusCode = http_parser_getHttpStatusCode(pHttpRspCtx);

    /* Check HTTP results */
    CHK(uHttpStatusCode == HTTP_STATUS_OK && resultLen != 0 && pResponseStr != NULL, STATUS_HTTP_STATUS_CODE_ERROR);
    CHK(http_api_rsp_getChannelEndpoint((const CHAR*) pResponseStr, resultLen, pSignalingClient) == STATUS_SUCCESS, STATUS_HTTP_RSP_ERROR);
    /* We got a success response here. */

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pHttpStatusCode != NULL) {
        *pHttpStatusCode = uHttpStatusCode;
    }

    if (xNetIoHandle != NULL) {
        NetIo_disconnect(xNetIoHandle);
        NetIo_terminate(xNetIoHandle);
    }

    if (pHttpRspCtx != NULL) {
        retStatus = http_parser_detroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGE("destroying http parset failed. \n");
        }
    }
    SAFE_MEMFREE(pHttpBody);
    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    SAFE_MEMFREE(pHttpSendBuffer);
    SAFE_MEMFREE(pHttpRecvBuffer);
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
    UINT32 uBytesReceived = 0;

    /* Variables for HTTP request */
    // http req.
    PCHAR pUrl = NULL;
    UINT32 urlLen;
    PRequestInfo pRequestInfo = NULL;
    PCHAR pHttpBody = NULL;
    UINT32 httpBodyLen;
    PCHAR pHost = NULL;
    // rsp
    UINT32 uHttpStatusCode = 0;
    HttpResponseContext* pHttpRspCtx = NULL;
    PCHAR pResponseStr;
    UINT32 resultLen;
    // new net io.
    NetIoHandle xNetIoHandle = NULL;
    uint8_t* pHttpSendBuffer = NULL;
    uint8_t* pHttpRecvBuffer = NULL;

    CHK(NULL != (pHost = (PCHAR) MEMCALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    urlLen = STRLEN(pSignalingClient->channelDescription.channelEndpointHttps) + STRLEN(HTTP_API_GET_ICE_CONFIG) + 1;
    CHK(NULL != (pUrl = (PCHAR) MEMCALLOC(urlLen, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    httpBodyLen = SIZEOF(HTTP_API_BODY_GET_ICE_CONFIG) + STRLEN(pSignalingClient->channelDescription.channelArn) +
        STRLEN(pSignalingClient->clientInfo.signalingClientInfo.clientId) + 1;
    CHK(NULL != (pHttpBody = (PCHAR) MEMALLOC(httpBodyLen)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpSendBuffer = (uint8_t*) MEMCALLOC(HTTP_API_SEND_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpRecvBuffer = (uint8_t*) MEMCALLOC(HTTP_API_RECV_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    CHK(SNPRINTF(pUrl, urlLen, "%s%s", pSignalingClient->channelDescription.channelEndpointHttps, HTTP_API_GET_ICE_CONFIG) > 0,
        STATUS_HTTP_BUF_OVERFLOW);
    /* generate HTTP request body */
    CHK(SNPRINTF(pHttpBody, httpBodyLen, HTTP_API_BODY_GET_ICE_CONFIG, pSignalingClient->channelDescription.channelArn,
                 pSignalingClient->clientInfo.signalingClientInfo.clientId) > 0,
        STATUS_HTTP_BUF_OVERFLOW);

    // Create the request info with the body
    CHK_STATUS(createRequestInfo(pUrl, pHttpBody, pSignalingClient->pChannelInfo->pRegion, pSignalingClient->pChannelInfo->pCertPath, NULL, NULL,
                                 SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, HTTP_API_CONNECTION_TIMEOUT,
                                 HTTP_API_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pSignalingClient->pAwsCredentials, &pRequestInfo));

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (xNetIoHandle = NetIo_create()), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK_STATUS(NetIo_setRecvTimeout(xNetIoHandle, HTTP_API_COMPLETION_TIMEOUT));
    CHK_STATUS(NetIo_setSendTimeout(xNetIoHandle, HTTP_API_COMPLETION_TIMEOUT));

    CHK_STATUS(http_req_pack(pRequestInfo, HTTP_REQUEST_VERB_POST_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pHttpSendBuffer,
                             HTTP_API_SEND_BUFFER_MAX_SIZE, FALSE, TRUE, NULL));

    CHK_STATUS(NetIo_connect(xNetIoHandle, pHost, HTTP_API_SECURE_PORT));

    CHK(NetIo_send(xNetIoHandle, (unsigned char*) pHttpSendBuffer, STRLEN((PCHAR) pHttpSendBuffer)) == STATUS_SUCCESS, STATUS_NET_SEND_DATA_FAILED);
    CHK_STATUS(NetIo_recv(xNetIoHandle, (unsigned char*) pHttpRecvBuffer, HTTP_API_RECV_BUFFER_MAX_SIZE, &uBytesReceived));

    CHK(uBytesReceived > 0, STATUS_NET_RECV_DATA_FAILED);

    CHK(http_parser_start(&pHttpRspCtx, (CHAR*) pHttpRecvBuffer, (UINT32) uBytesReceived, NULL) == STATUS_SUCCESS, STATUS_HTTP_PARSER_ERROR);

    pResponseStr = http_parser_getHttpBodyLocation(pHttpRspCtx);
    resultLen = http_parser_getHttpBodyLength(pHttpRspCtx);
    uHttpStatusCode = http_parser_getHttpStatusCode(pHttpRspCtx);

    /* Check HTTP results */
    CHK(uHttpStatusCode == HTTP_STATUS_OK && resultLen != 0 && pResponseStr != NULL, STATUS_HTTP_STATUS_CODE_ERROR);
    CHK(http_api_rsp_getIceConfig((const CHAR*) pResponseStr, resultLen, pSignalingClient) == STATUS_SUCCESS, STATUS_HTTP_RSP_ERROR);

    if (retStatus != STATUS_SUCCESS) {
        DLOGD("parse failed.");
    }

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pHttpStatusCode != NULL) {
        *pHttpStatusCode = uHttpStatusCode;
    }

    if (xNetIoHandle != NULL) {
        NetIo_disconnect(xNetIoHandle);
        NetIo_terminate(xNetIoHandle);
    }

    if (pHttpRspCtx != NULL) {
        retStatus = http_parser_detroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGE("destroying http parset failed. \n");
        }
    }
    SAFE_MEMFREE(pHttpBody);
    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    SAFE_MEMFREE(pHttpSendBuffer);
    SAFE_MEMFREE(pHttpRecvBuffer);
    freeRequestInfo(&pRequestInfo);
    HTTP_API_EXIT();
    return retStatus;
}

STATUS http_api_deleteChannel(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode)
{
    HTTP_API_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PChannelInfo pChannelInfo = pSignalingClient->pChannelInfo;
    UINT32 uBytesReceived = 0;
    PCHAR pUrl = NULL;
    UINT32 urlLen;
    PRequestInfo pRequestInfo = NULL;
    PCHAR pHttpBody = NULL;
    UINT32 httpBodyLen;
    PCHAR pHost = NULL;
    UINT32 uHttpStatusCode = 0;
    HttpResponseContext* pHttpRspCtx = NULL;
    // new net io.
    NetIoHandle xNetIoHandle = NULL;
    uint8_t* pHttpSendBuffer = NULL;
    uint8_t* pHttpRecvBuffer = NULL;

    CHK(NULL != (pHost = (PCHAR) MEMCALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    urlLen = STRLEN(pSignalingClient->pChannelInfo->pControlPlaneUrl) + STRLEN(HTTP_API_DELETE_SIGNALING_CHANNEL) + 1;
    CHK(NULL != (pUrl = (PCHAR) MEMCALLOC(urlLen, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpSendBuffer = (uint8_t*) MEMCALLOC(HTTP_API_SEND_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpRecvBuffer = (uint8_t*) MEMCALLOC(HTTP_API_RECV_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    httpBodyLen = SIZEOF(HTTP_API_BODY_DELETE_CHANNEL) + STRLEN(pSignalingClient->channelDescription.channelArn) +
        STRLEN(pSignalingClient->channelDescription.updateVersion) + 1;
    CHK(NULL != (pHttpBody = (CHAR*) MEMALLOC(httpBodyLen)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    // Create the API url
    CHK(SNPRINTF(pUrl, urlLen, "%s%s", pSignalingClient->pChannelInfo->pControlPlaneUrl, HTTP_API_DELETE_SIGNALING_CHANNEL) > 0,
        STATUS_HTTP_BUF_OVERFLOW);
    /* generate HTTP request body */
    CHK(SNPRINTF(pHttpBody, httpBodyLen, HTTP_API_BODY_DELETE_CHANNEL, pSignalingClient->channelDescription.channelArn,
                 pSignalingClient->channelDescription.updateVersion) > 0,
        STATUS_HTTP_BUF_OVERFLOW);

    // Create the request info with the body
    CHK_STATUS(createRequestInfo(pUrl, pHttpBody, pSignalingClient->pChannelInfo->pRegion, (PCHAR) pSignalingClient->pChannelInfo->pCertPath, NULL,
                                 NULL, SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, HTTP_API_CONNECTION_TIMEOUT,
                                 HTTP_API_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pSignalingClient->pAwsCredentials, &pRequestInfo));

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (xNetIoHandle = NetIo_create()), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK_STATUS(NetIo_setRecvTimeout(xNetIoHandle, HTTP_API_COMPLETION_TIMEOUT));
    CHK_STATUS(NetIo_setSendTimeout(xNetIoHandle, HTTP_API_COMPLETION_TIMEOUT));

    CHK_STATUS(http_req_pack(pRequestInfo, HTTP_REQUEST_VERB_POST_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pHttpSendBuffer,
                             HTTP_API_SEND_BUFFER_MAX_SIZE, FALSE, TRUE, NULL));

    CHK_STATUS(NetIo_connect(xNetIoHandle, pHost, HTTP_API_SECURE_PORT));

    CHK(NetIo_send(xNetIoHandle, (unsigned char*) pHttpSendBuffer, STRLEN((PCHAR) pHttpSendBuffer)) == STATUS_SUCCESS, STATUS_NET_SEND_DATA_FAILED);
    CHK_STATUS(NetIo_recv(xNetIoHandle, (unsigned char*) pHttpRecvBuffer, HTTP_API_RECV_BUFFER_MAX_SIZE, &uBytesReceived));

    CHK(uBytesReceived > 0, STATUS_NET_RECV_DATA_FAILED);

    CHK(http_parser_start(&pHttpRspCtx, (CHAR*) pHttpRecvBuffer, (UINT32) uBytesReceived, NULL) == STATUS_SUCCESS, STATUS_HTTP_PARSER_ERROR);
    uHttpStatusCode = http_parser_getHttpStatusCode(pHttpRspCtx);

    /* Check HTTP results */
    CHK(uHttpStatusCode == HTTP_STATUS_OK || uHttpStatusCode == HTTP_STATUS_NOT_FOUND, retStatus);

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pHttpStatusCode != NULL) {
        *pHttpStatusCode = uHttpStatusCode;
    }

    if (xNetIoHandle != NULL) {
        NetIo_disconnect(xNetIoHandle);
        NetIo_terminate(xNetIoHandle);
    }

    if (pHttpRspCtx != NULL) {
        retStatus = http_parser_detroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGE("destroying http parset failed. \n");
        }
    }
    SAFE_MEMFREE(pHttpBody);
    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    SAFE_MEMFREE(pHttpSendBuffer);
    SAFE_MEMFREE(pHttpRecvBuffer);
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
    UINT32 uBytesReceived = 0;
    /* Variables for HTTP request */
    PCHAR pUrl = NULL;
    UINT32 urlLen;

    PRequestInfo pRequestInfo = NULL;
    PCHAR pHttpBody = NULL;
    UINT32 httpBodyLen = 0;
    PCHAR pHost = NULL;
    // rsp
    UINT32 uHttpStatusCode = 0;
    HttpResponseContext* pHttpRspCtx = NULL;
    PCHAR pResponseStr;
    UINT32 resultLen;
    // new net io.
    NetIoHandle xNetIoHandle = NULL;
    uint8_t* pHttpSendBuffer = NULL;
    uint8_t* pHttpRecvBuffer = NULL;

    CHK(NULL != (pHost = (PCHAR) MEMCALLOC(MAX_CONTROL_PLANE_URI_CHAR_LEN, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    urlLen = STRLEN(CONTROL_PLANE_URI_PREFIX) + STRLEN(pIotCredentialProvider->iotGetCredentialEndpoint) + STRLEN(HTTP_API_ROLE_ALIASES) +
        STRLEN("/") + STRLEN(pIotCredentialProvider->roleAlias) + STRLEN(HTTP_API_CREDENTIALS) + 1;
    CHK(NULL != (pUrl = (PCHAR) MEMCALLOC(urlLen, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpSendBuffer = (uint8_t*) MEMCALLOC(HTTP_API_SEND_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pHttpRecvBuffer = (uint8_t*) MEMCALLOC(HTTP_API_RECV_BUFFER_MAX_SIZE, 1)), STATUS_HTTP_NOT_ENOUGH_MEMORY);

    // Create the API url
    CHK(SNPRINTF(pUrl, urlLen, "%s%s%s%c%s%s", CONTROL_PLANE_URI_PREFIX, pIotCredentialProvider->iotGetCredentialEndpoint, HTTP_API_ROLE_ALIASES, '/',
                 pIotCredentialProvider->roleAlias, HTTP_API_CREDENTIALS) > 0,
        STATUS_HTTP_IOT_FAILED);

    // Create the request info with the body
    CHK_STATUS(createRequestInfo(pUrl, pHttpBody, DEFAULT_AWS_REGION, pIotCredentialProvider->caCertPath, pIotCredentialProvider->certPath,
                                 pIotCredentialProvider->privateKeyPath, SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, DEFAULT_USER_AGENT_NAME,
                                 HTTP_API_CONNECTION_TIMEOUT, HTTP_API_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
                                 pIotCredentialProvider->pAwsCredentials, &pRequestInfo));

    // CHK_STATUS(createRequestInfo(pUrl, pHttpBody, pSignalingClient->pChannelInfo->pRegion, pIotCredentialProvider->caCertPath,
    // pIotCredentialProvider->certPath,
    //                             pIotCredentialProvider->privateKeyPath,
    //                             SSL_CERTIFICATE_TYPE_NOT_SPECIFIED, pSignalingClient->pChannelInfo->pUserAgent, HTTP_API_CONNECTION_TIMEOUT,
    //                             HTTP_API_COMPLETION_TIMEOUT, DEFAULT_LOW_SPEED_LIMIT, DEFAULT_LOW_SPEED_TIME_LIMIT,
    //                             pIotCredentialProvider->pAwsCredentials, &pRequestInfo));

    CHK_STATUS(setRequestHeader(pRequestInfo, HTTP_API_IOT_THING_NAME_HEADER, 0, pIotCredentialProvider->thingName, 0));
    CHK_STATUS(setRequestHeader(pRequestInfo, "accept", 0, "*/*", 0));

    /* Initialize and generate HTTP request, then send it. */
    CHK(NULL != (xNetIoHandle = NetIo_create()), STATUS_HTTP_NOT_ENOUGH_MEMORY);
    CHK_STATUS(NetIo_setRecvTimeout(xNetIoHandle, HTTP_API_COMPLETION_TIMEOUT));
    CHK_STATUS(NetIo_setSendTimeout(xNetIoHandle, HTTP_API_COMPLETION_TIMEOUT));

    CHK_STATUS(http_req_pack(pRequestInfo, HTTP_REQUEST_VERB_GET_STRING, pHost, MAX_CONTROL_PLANE_URI_CHAR_LEN, (PCHAR) pHttpSendBuffer,
                             HTTP_API_SEND_BUFFER_MAX_SIZE, FALSE, FALSE, NULL));

    CHK_STATUS(NetIo_connectWithX509Path(xNetIoHandle, pHost, HTTP_API_SECURE_PORT, pIotCredentialProvider->caCertPath,
                                         pIotCredentialProvider->certPath, pIotCredentialProvider->privateKeyPath));

    CHK(NetIo_send(xNetIoHandle, (unsigned char*) pHttpSendBuffer, STRLEN((PCHAR) pHttpSendBuffer)) == STATUS_SUCCESS, STATUS_NET_SEND_DATA_FAILED);

    CHK_STATUS(NetIo_recv(xNetIoHandle, (unsigned char*) pHttpRecvBuffer, HTTP_API_RECV_BUFFER_MAX_SIZE, &uBytesReceived));

    CHK(uBytesReceived > 0, STATUS_NET_RECV_DATA_FAILED);

    CHK(http_parser_start(&pHttpRspCtx, (CHAR*) pHttpRecvBuffer, (UINT32) uBytesReceived, NULL) == STATUS_SUCCESS, STATUS_HTTP_PARSER_ERROR);
    pResponseStr = http_parser_getHttpBodyLocation(pHttpRspCtx);
    resultLen = http_parser_getHttpBodyLength(pHttpRspCtx);
    uHttpStatusCode = http_parser_getHttpStatusCode(pHttpRspCtx);

    // ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) uHttpStatusCode);
    /* Check HTTP results */
    // CHK((HTTP_STATUS_CODE) ATOMIC_LOAD(&pSignalingClient->apiCallStatus) == HTTP_STATUS_OK && resultLen != 0 && pResponseStr != NULL,
    // retStatus);

    // CHK_STATUS(http_api_rsp_getIoTCredential((const CHAR*) pResponseStr, resultLen, pSignalingClient));
    CHK_STATUS(http_api_rsp_getIoTCredential(pIotCredentialProvider, (const CHAR*) pResponseStr, resultLen));
    /* We got a success response here. */

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (xNetIoHandle != NULL) {
        NetIo_disconnect(xNetIoHandle);
        NetIo_terminate(xNetIoHandle);
    }

    if (pHttpRspCtx != NULL) {
        retStatus = http_parser_detroy(pHttpRspCtx);
        if (retStatus != STATUS_SUCCESS) {
            DLOGE("destroying http parset failed. \n");
        }
    }
    SAFE_MEMFREE(pHttpBody);
    SAFE_MEMFREE(pHost);
    SAFE_MEMFREE(pUrl);
    SAFE_MEMFREE(pHttpSendBuffer);
    SAFE_MEMFREE(pHttpRecvBuffer);
    freeRequestInfo(&pRequestInfo);
    HTTP_API_EXIT();
    return retStatus;
}
