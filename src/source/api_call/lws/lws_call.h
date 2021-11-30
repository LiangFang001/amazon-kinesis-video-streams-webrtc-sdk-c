
#ifndef __KINESIS_VIDEO_LWS_CALL_INCLUDE_I__
#define __KINESIS_VIDEO_LWS_CALL_INCLUDE_I__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <kvs/common.h>
#include "request_info.h"

// Max send buffer size for LWS
#define IOT_LWS_SEND_BUFFER_SIZE (LWS_PRE + MAX_URI_CHAR_LEN)

STATUS blockingLwsCall(PRequestInfo, PCallInfo);
INT32 lwsIotCallbackRoutine(struct lws*, enum lws_callback_reasons, PVOID, PVOID, size_t);
STATUS iotCurlHandler(PIotCredentialProvider);
STATUS parseIotResponse(PIotCredentialProvider, PCallInfo);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_LWS_CALL_INCLUDE_I__ */
