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

#define LOG_CLASS "HttpApiRsp"
#include "../Include_i.h"
#include "http_api.h"
#include "channel_info.h"
#include "auth.h"

#define HTTP_RSP_ENTER() // DLOGD("enter")
#define HTTP_RSP_EXIT()  // DLOGD("exit")

/*-----------------------------------------------------------*/

/**
 *
    {
        "ChannelInfo": {
            "ChannelARN": "string",
            "ChannelName": "string",
            "ChannelStatus": "string",
            "ChannelType": "string",
            "CreationTime": number,
            "SingleMasterConfiguration": {
                "MessageTtlSeconds": number
            },
            "Version": "string"
        }
    }
 *
*/
STATUS http_api_rsp_createChannel(const CHAR* pResponseStr, UINT32 resultLen, PSignalingClient pSignalingClient)
{
    HTTP_RSP_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    jsmn_parser parser;
    jsmntok_t* pTokens = NULL;
    UINT32 tokenCount, i, strLen;
    UINT64 ttl;

    CHK(NULL != (pTokens = (jsmntok_t*) MEMALLOC(MAX_JSON_TOKEN_COUNT * SIZEOF(jsmntok_t))), STATUS_NOT_ENOUGH_MEMORY);

    // Parse out the ARN
    jsmn_init(&parser);
    tokenCount = jsmn_parse(&parser, pResponseStr, resultLen, pTokens, MAX_JSON_TOKEN_COUNT);
    CHK(tokenCount > 1, STATUS_INVALID_API_CALL_RETURN_JSON);
    CHK(pTokens[0].type == JSMN_OBJECT, STATUS_INVALID_API_CALL_RETURN_JSON);

    // Loop through the pTokens and extract the stream description
    for (i = 1; i < tokenCount; i++) {
        if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "ChannelARN")) {
            strLen = (UINT32)(pTokens[i + 1].end - pTokens[i + 1].start);
            CHK(strLen <= MAX_ARN_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
            STRNCPY(pSignalingClient->channelDescription.channelArn, pResponseStr + pTokens[i + 1].start, strLen);
            pSignalingClient->channelDescription.channelArn[MAX_ARN_LEN] = '\0';
            i++;
        }
    }

    // Perform some validation on the channel description
    CHK(pSignalingClient->channelDescription.channelArn[0] != '\0', STATUS_SIGNALING_NO_ARN_RETURNED_ON_CREATE);
CleanUp:
    MEMFREE(pTokens);
    HTTP_RSP_EXIT();
    return retStatus;
}

STATUS http_api_rsp_describeChannel(const CHAR* pResponseStr, UINT32 resultLen, PSignalingClient pSignalingClient)
{
    HTTP_RSP_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    jsmn_parser parser;
    jsmntok_t* pTokens = NULL;
    BOOL jsonInChannelDescription = FALSE, jsonInMvConfiguration = FALSE;
    UINT32 tokenCount, strLen, i;
    UINT64 messageTtl;

    CHK(NULL != (pTokens = (jsmntok_t*) MEMALLOC(MAX_JSON_TOKEN_COUNT * SIZEOF(jsmntok_t))), STATUS_NOT_ENOUGH_MEMORY);
    jsmn_init(&parser);
    tokenCount = jsmn_parse(&parser, pResponseStr, resultLen, pTokens, MAX_JSON_TOKEN_COUNT);

    CHK(tokenCount > 1, STATUS_INVALID_API_CALL_RETURN_JSON);
    CHK(pTokens[0].type == JSMN_OBJECT, STATUS_INVALID_API_CALL_RETURN_JSON);
    MEMSET(&pSignalingClient->channelDescription, 0x00, SIZEOF(SignalingChannelDescription));

    // Loop through the pTokens and extract the stream description
    for (i = 1; i < tokenCount; i++) {
        if (!jsonInChannelDescription) {
            if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "ChannelInfo")) {
                pSignalingClient->channelDescription.version = SIGNALING_CHANNEL_DESCRIPTION_CURRENT_VERSION;
                jsonInChannelDescription = TRUE;
                i++;
            }
        } else {
            if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "ChannelARN")) {
                strLen = (UINT32)(pTokens[i + 1].end - pTokens[i + 1].start);
                CHK(strLen <= MAX_ARN_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
                STRNCPY(pSignalingClient->channelDescription.channelArn, pResponseStr + pTokens[i + 1].start, strLen);
                pSignalingClient->channelDescription.channelArn[MAX_ARN_LEN] = '\0';
                DLOGD("channel arn: %s", pSignalingClient->channelDescription.channelArn);
                i++;
            } else if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "ChannelName")) {
                strLen = (UINT32)(pTokens[i + 1].end - pTokens[i + 1].start);
                CHK(strLen <= MAX_CHANNEL_NAME_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
                STRNCPY(pSignalingClient->channelDescription.channelName, pResponseStr + pTokens[i + 1].start, strLen);
                pSignalingClient->channelDescription.channelName[MAX_CHANNEL_NAME_LEN] = '\0';
                i++;
            } else if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "Version")) {
                strLen = (UINT32)(pTokens[i + 1].end - pTokens[i + 1].start);
                CHK(strLen <= MAX_UPDATE_VERSION_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
                STRNCPY(pSignalingClient->channelDescription.updateVersion, pResponseStr + pTokens[i + 1].start, strLen);
                pSignalingClient->channelDescription.updateVersion[MAX_UPDATE_VERSION_LEN] = '\0';
                i++;
            } else if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "ChannelStatus")) {
                strLen = (UINT32)(pTokens[i + 1].end - pTokens[i + 1].start);
                CHK(strLen <= MAX_DESCRIBE_CHANNEL_STATUS_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
                pSignalingClient->channelDescription.channelStatus = getChannelStatusFromString(pResponseStr + pTokens[i + 1].start, strLen);
                i++;
            } else if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "ChannelType")) {
                strLen = (UINT32)(pTokens[i + 1].end - pTokens[i + 1].start);
                CHK(strLen <= MAX_DESCRIBE_CHANNEL_TYPE_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
                pSignalingClient->channelDescription.channelType = getChannelTypeFromString(pResponseStr + pTokens[i + 1].start, strLen);
                i++;
            } else if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "CreationTime")) {
                // TODO: In the future parse out the creation time but currently we don't need it
                i++;
            } else {
                if (!jsonInMvConfiguration) {
                    if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "SingleMasterConfiguration")) {
                        jsonInMvConfiguration = TRUE;
                        i++;
                    }
                } else {
                    if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "MessageTtlSeconds")) {
                        CHK_STATUS(STRTOUI64(pResponseStr + pTokens[i + 1].start, pResponseStr + pTokens[i + 1].end, 10, &messageTtl));

                        // NOTE: Ttl value is in seconds
                        pSignalingClient->channelDescription.messageTtl = messageTtl * HUNDREDS_OF_NANOS_IN_A_SECOND;
                        i++;
                    }
                }
            }
        }
    }

CleanUp:
    MEMFREE(pTokens);
    HTTP_RSP_EXIT();
    return retStatus;
}

STATUS http_api_rsp_getChannelEndpoint(const CHAR* pResponseStr, UINT32 resultLen, PSignalingClient pSignalingClient)
{
    HTTP_RSP_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    jsmn_parser parser;
    jsmntok_t* pTokens = NULL;
    UINT32 tokenCount, i, protocolLen = 0, endpointLen = 0, strLen;
    PCHAR pProtocol = NULL, pEndpoint = NULL;
    BOOL jsonInResourceEndpointList = FALSE, protocol = FALSE, endpoint = FALSE, inEndpointArray = FALSE;

    CHK(NULL != (pTokens = (jsmntok_t*) MEMALLOC(MAX_JSON_TOKEN_COUNT * SIZEOF(jsmntok_t))), STATUS_NOT_ENOUGH_MEMORY);

    // Parse and extract the endpoints
    jsmn_init(&parser);
    tokenCount = jsmn_parse(&parser, pResponseStr, resultLen, pTokens, MAX_JSON_TOKEN_COUNT);
    CHK(tokenCount > 1, STATUS_INVALID_API_CALL_RETURN_JSON);
    CHK(pTokens[0].type == JSMN_OBJECT, STATUS_INVALID_API_CALL_RETURN_JSON);

    pSignalingClient->channelEndpointWss[0] = '\0';
    pSignalingClient->channelEndpointHttps[0] = '\0';

    // Loop through the pTokens and extract the stream description
    for (i = 1; i < tokenCount; i++) {
        if (!jsonInResourceEndpointList) {
            if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "ResourceEndpointList")) {
                jsonInResourceEndpointList = TRUE;
                i++;
            }
        } else {
            if (!inEndpointArray && pTokens[i].type == JSMN_ARRAY) {
                inEndpointArray = TRUE;
            } else {
                if (pTokens[i].type == JSMN_OBJECT) {
                    // Process if both are set
                    if (protocol && endpoint) {
                        if (0 == STRNCMPI(pProtocol, WSS_SCHEME_NAME, protocolLen)) {
                            STRNCPY(pSignalingClient->channelEndpointWss, pEndpoint, MIN(endpointLen, MAX_SIGNALING_ENDPOINT_URI_LEN));
                            pSignalingClient->channelEndpointWss[MAX_SIGNALING_ENDPOINT_URI_LEN] = '\0';
                        } else if (0 == STRNCMPI(pProtocol, HTTPS_SCHEME_NAME, protocolLen)) {
                            STRNCPY(pSignalingClient->channelEndpointHttps, pEndpoint, MIN(endpointLen, MAX_SIGNALING_ENDPOINT_URI_LEN));
                            pSignalingClient->channelEndpointHttps[MAX_SIGNALING_ENDPOINT_URI_LEN] = '\0';
                        }
                    }

                    protocol = FALSE;
                    endpoint = FALSE;
                    protocolLen = 0;
                    endpointLen = 0;
                    pProtocol = NULL;
                    pEndpoint = NULL;
                } else if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "Protocol")) {
                    strLen = (UINT32)(pTokens[i + 1].end - pTokens[i + 1].start);
                    pProtocol = pResponseStr + pTokens[i + 1].start;
                    protocolLen = strLen;
                    protocol = TRUE;
                    i++;
                } else if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "ResourceEndpoint")) {
                    strLen = (UINT32)(pTokens[i + 1].end - pTokens[i + 1].start);
                    CHK(strLen <= MAX_CHANNEL_NAME_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
                    pEndpoint = pResponseStr + pTokens[i + 1].start;
                    endpointLen = strLen;
                    endpoint = TRUE;
                    i++;
                }
            }
        }
    }

    // Check if we have unprocessed protocol
    if (protocol && endpoint) {
        if (0 == STRNCMPI(pProtocol, WSS_SCHEME_NAME, protocolLen)) {
            STRNCPY(pSignalingClient->channelEndpointWss, pEndpoint, MIN(endpointLen, MAX_SIGNALING_ENDPOINT_URI_LEN));
            pSignalingClient->channelEndpointWss[MAX_SIGNALING_ENDPOINT_URI_LEN] = '\0';
        } else if (0 == STRNCMPI(pProtocol, HTTPS_SCHEME_NAME, protocolLen)) {
            STRNCPY(pSignalingClient->channelEndpointHttps, pEndpoint, MIN(endpointLen, MAX_SIGNALING_ENDPOINT_URI_LEN));
            pSignalingClient->channelEndpointHttps[MAX_SIGNALING_ENDPOINT_URI_LEN] = '\0';
        }
    }

    // Perform some validation on the channel description
    CHK(pSignalingClient->channelEndpointHttps[0] != '\0' && pSignalingClient->channelEndpointWss[0] != '\0',
        STATUS_SIGNALING_MISSING_ENDPOINTS_IN_GET_ENDPOINT);

CleanUp:
    MEMFREE(pTokens);
    HTTP_RSP_EXIT();
    return retStatus;
}

STATUS http_api_rsp_getIceConfig(const CHAR* pResponseStr, UINT32 resultLen, PSignalingClient pSignalingClient)
{
    HTTP_RSP_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    jsmn_parser parser;
    jsmntok_t* pTokens = NULL;
    jsmntok_t* pToken;
    UINT32 tokenCount, i, j, configCount = 0, strLen;
    UINT64 ttl;
    BOOL jsonInIceServerList = FALSE;

    CHK(NULL != (pTokens = (jsmntok_t*) MEMALLOC(MAX_JSON_TOKEN_COUNT * SIZEOF(jsmntok_t))), STATUS_NOT_ENOUGH_MEMORY);

    // Parse and extract the endpoints
    jsmn_init(&parser);
    tokenCount = jsmn_parse(&parser, pResponseStr, resultLen, pTokens, MAX_JSON_TOKEN_COUNT);
    CHK(tokenCount > 1, STATUS_INVALID_API_CALL_RETURN_JSON);
    CHK(pTokens[0].type == JSMN_OBJECT, STATUS_INVALID_API_CALL_RETURN_JSON);

    MEMSET(&pSignalingClient->iceConfigs, 0x00, MAX_ICE_CONFIG_COUNT * SIZEOF(IceConfigInfo));
    pSignalingClient->iceConfigCount = 0;

    // Loop through the tokens and extract the ice configuration
    for (i = 0; i < tokenCount; i++) {
        if (!jsonInIceServerList) {
            if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "IceServerList")) {
                jsonInIceServerList = TRUE;

                CHK(pTokens[i + 1].type == JSMN_ARRAY, STATUS_INVALID_API_CALL_RETURN_JSON);
                CHK(pTokens[i + 1].size <= MAX_ICE_CONFIG_COUNT, STATUS_SIGNALING_MAX_ICE_CONFIG_COUNT);
            }
        } else {
            pToken = &pTokens[i];
            if (pToken->type == JSMN_OBJECT) {
                configCount++;
            } else if (compareJsonString(pResponseStr, pToken, JSMN_STRING, (PCHAR) "Username")) {
                strLen = (UINT32)(pToken[1].end - pToken[1].start);
                CHK(strLen <= MAX_ICE_CONFIG_USER_NAME_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
                STRNCPY(pSignalingClient->iceConfigs[configCount - 1].userName, pResponseStr + pToken[1].start, strLen);
                pSignalingClient->iceConfigs[configCount - 1].userName[MAX_ICE_CONFIG_USER_NAME_LEN] = '\0';
                i++;
            } else if (compareJsonString(pResponseStr, pToken, JSMN_STRING, (PCHAR) "Password")) {
                strLen = (UINT32)(pToken[1].end - pToken[1].start);
                CHK(strLen <= MAX_ICE_CONFIG_CREDENTIAL_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
                STRNCPY(pSignalingClient->iceConfigs[configCount - 1].password, pResponseStr + pToken[1].start, strLen);
                pSignalingClient->iceConfigs[configCount - 1].userName[MAX_ICE_CONFIG_CREDENTIAL_LEN] = '\0';
                i++;
            } else if (compareJsonString(pResponseStr, pToken, JSMN_STRING, (PCHAR) "Ttl")) {
                CHK_STATUS(STRTOUI64(pResponseStr + pToken[1].start, pResponseStr + pToken[1].end, 10, &ttl));

                // NOTE: Ttl value is in seconds
                pSignalingClient->iceConfigs[configCount - 1].ttl = ttl * HUNDREDS_OF_NANOS_IN_A_SECOND;
                i++;
            } else if (compareJsonString(pResponseStr, pToken, JSMN_STRING, (PCHAR) "Uris")) {
                // Expect an array of elements
                CHK(pToken[1].type == JSMN_ARRAY, STATUS_INVALID_API_CALL_RETURN_JSON);
                CHK(pToken[1].size <= MAX_ICE_CONFIG_URI_COUNT, STATUS_SIGNALING_MAX_ICE_URI_COUNT);
                for (j = 0; j < pToken[1].size; j++) {
                    strLen = (UINT32)(pToken[j + 2].end - pToken[j + 2].start);
                    CHK(strLen <= MAX_ICE_CONFIG_URI_LEN, STATUS_SIGNALING_MAX_ICE_URI_LEN);
                    STRNCPY(pSignalingClient->iceConfigs[configCount - 1].uris[j], pResponseStr + pToken[j + 2].start, strLen);
                    pSignalingClient->iceConfigs[configCount - 1].uris[j][MAX_ICE_CONFIG_URI_LEN] = '\0';
                    pSignalingClient->iceConfigs[configCount - 1].uriCount++;
                }

                i += pToken[1].size + 1;
            }
        }
    }

    // Perform some validation on the ice configuration
    pSignalingClient->iceConfigCount = configCount;
    CHK_STATUS(signalingValidateIceConfiguration(pSignalingClient));

CleanUp:
    MEMFREE(pTokens);
    HTTP_RSP_EXIT();
    return retStatus;
}

// STATUS http_api_rsp_getIoTCredential(const CHAR* pResponseStr, UINT32 resultLen, PSignalingClient pSignalingClient)
STATUS http_api_rsp_getIoTCredential(PIotCredentialProvider pIotCredentialProvider, const CHAR* pResponseStr, UINT32 resultLen)
{
    HTTP_RSP_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    jsmn_parser parser;
    jsmntok_t* pTokens = NULL;

    UINT32 i, accessKeyIdLen = 0, secretKeyLen = 0, sessionTokenLen = 0, expirationTimestampLen = 0;
    INT32 tokenCount;

    PCHAR accessKeyId = NULL, secretKey = NULL, sessionToken = NULL, expirationTimestamp = NULL;
    UINT64 expiration, currentTime;
    CHAR expirationTimestampStr[MAX_EXPIRATION_LEN + 1];

    // CHK(pIotCredentialProvider != NULL && pCallInfo != NULL, STATUS_NULL_ARG);
    CHK(NULL != (pTokens = (jsmntok_t*) MEMALLOC(MAX_JSON_TOKEN_COUNT * SIZEOF(jsmntok_t))), STATUS_NOT_ENOUGH_MEMORY);
    CHK(resultLen > 0, STATUS_IOT_FAILED);

    jsmn_init(&parser);
    tokenCount = jsmn_parse(&parser, pResponseStr, resultLen, pTokens, MAX_JSON_TOKEN_COUNT);
    CHK(tokenCount > 1, STATUS_INVALID_API_CALL_RETURN_JSON);
    CHK(pTokens[0].type == JSMN_OBJECT, STATUS_INVALID_API_CALL_RETURN_JSON);

    for (i = 1; i < (UINT32) tokenCount; i++) {
        if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "accessKeyId")) {
            accessKeyIdLen = (UINT32)(pTokens[i + 1].end - pTokens[i + 1].start);
            CHK(accessKeyIdLen <= MAX_ACCESS_KEY_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
            accessKeyId = pResponseStr + pTokens[i + 1].start;
            i++;
        } else if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "secretAccessKey")) {
            secretKeyLen = (UINT32)(pTokens[i + 1].end - pTokens[i + 1].start);
            CHK(secretKeyLen <= MAX_SECRET_KEY_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
            secretKey = pResponseStr + pTokens[i + 1].start;
            i++;
        } else if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, (PCHAR) "sessionToken")) {
            sessionTokenLen = (UINT32)(pTokens[i + 1].end - pTokens[i + 1].start);
            CHK(sessionTokenLen <= MAX_SESSION_TOKEN_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
            sessionToken = pResponseStr + pTokens[i + 1].start;
            i++;
        } else if (compareJsonString(pResponseStr, &pTokens[i], JSMN_STRING, "expiration")) {
            expirationTimestampLen = (UINT32)(pTokens[i + 1].end - pTokens[i + 1].start);
            CHK(expirationTimestampLen <= MAX_EXPIRATION_LEN, STATUS_INVALID_API_CALL_RETURN_JSON);
            expirationTimestamp = pResponseStr + pTokens[i + 1].start;
            MEMCPY(expirationTimestampStr, expirationTimestamp, expirationTimestampLen);
            expirationTimestampStr[expirationTimestampLen] = '\0';
            i++;
        }
    }

    CHK(accessKeyId != NULL && secretKey != NULL && sessionToken != NULL, STATUS_IOT_FAILED);
    DLOGD("accessKeyId:%s, secretKey:%s, sessionToken:%s", accessKeyId, secretKey, sessionToken);
//#TBD
#if 1
    currentTime = pIotCredentialProvider->getCurrentTimeFn(pIotCredentialProvider->customData);
    CHK_STATUS(convertTimestampToEpoch(expirationTimestampStr, currentTime / HUNDREDS_OF_NANOS_IN_A_SECOND, &expiration));
    DLOGD("Iot credential expiration time %" PRIu64, expiration / HUNDREDS_OF_NANOS_IN_A_SECOND);

    if (pIotCredentialProvider->pAwsCredentials != NULL) {
        freeAwsCredentials(&pIotCredentialProvider->pAwsCredentials);
        pIotCredentialProvider->pAwsCredentials = NULL;
    }

    // Fix-up the expiration to be no more than max enforced token rotation to avoid extra token rotations
    // as we are caching the returned value which is likely to be an hour but we are enforcing max
    // rotation to be more frequent.
    expiration = MIN(expiration, currentTime + MAX_ENFORCED_TOKEN_EXPIRATION_DURATION);

    CHK_STATUS(createAwsCredentials(accessKeyId, accessKeyIdLen, secretKey, secretKeyLen, sessionToken, sessionTokenLen, expiration,
                                    &pIotCredentialProvider->pAwsCredentials));
#endif
CleanUp:

    MEMFREE(pTokens);
    HTTP_RSP_EXIT();
    return retStatus;
}
