#ifndef __KINESIS_VIDEO_WEBRTC_HTTP_API_H_
#define __KINESIS_VIDEO_WEBRTC_HTTP_API_H_

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "signaling.h"
#include "iot_credential_provider.h"
#include "request_info.h"

STATUS http_api_createChannel(PSignalingClient pSignalingClient, UINT64 time);
STATUS http_api_describeChannel(PSignalingClient pSignalingClient, UINT64 time);
STATUS http_api_getChannelEndpoint(PSignalingClient pSignalingClient, UINT64 time);
STATUS http_api_getIceConfig(PSignalingClient pSignalingClient, UINT64 time);
STATUS http_api_deleteChannel(PSignalingClient pSignalingClient, UINT64 time);
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
#endif /* __KINESIS_VIDEO_WEBRTC_HTTP_API_H_ */