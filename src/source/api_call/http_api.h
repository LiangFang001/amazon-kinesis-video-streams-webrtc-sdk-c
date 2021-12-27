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
#ifndef __AWS_KVS_WEBRTC_HTTP_API_INCLUDE__
#define __AWS_KVS_WEBRTC_HTTP_API_INCLUDE__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "kvs/common_defs.h"
#include "kvs/error.h"
#include "signaling.h"
#include "iot_credential_provider.h"

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS http_api_createChannel(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode);
STATUS http_api_describeChannel(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode);
STATUS http_api_getChannelEndpoint(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode);
STATUS http_api_getIceConfig(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode);
STATUS http_api_deleteChannel(PSignalingClient pSignalingClient, PUINT32 pHttpStatusCode);
STATUS http_api_getIotCredential(PIotCredentialProvider pIotCredentialProvider);

STATUS http_api_rsp_createChannel(const CHAR* pResponseStr, UINT32 resultLen, PSignalingClient pSignalingClient);
STATUS http_api_rsp_deleteChannel(const CHAR* pResponseStr, UINT32 resultLen, PSignalingClient pSignalingClient);
STATUS http_api_rsp_describeChannel(const CHAR* pResponseStr, UINT32 resultLen, PSignalingClient pSignalingClient);
STATUS http_api_rsp_getChannelEndpoint(const CHAR* pResponseStr, UINT32 resultLen, PSignalingClient pSignalingClient);
STATUS http_api_rsp_getIceConfig(const CHAR* pResponseStr, UINT32 resultLen, PSignalingClient pSignalingClient);
// STATUS http_api_rsp_getIoTCredential(const CHAR* pResponseStr, UINT32 resultLen, PSignalingClient pSignalingClient);
STATUS http_api_rsp_getIoTCredential(PIotCredentialProvider pIotCredentialProvider, const CHAR* pResponseStr, UINT32 resultLen);
#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_HTTP_API_INCLUDE__ */
