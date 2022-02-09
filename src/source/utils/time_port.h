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
#ifndef __AWS_KVS_WEBRTC_TIME_INCLUDE__
#define __AWS_KVS_WEBRTC_TIME_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "kvs/common_defs.h"
#include "kvs/error.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
// yyyy-mm-dd HH:MM:SS
#define MAX_TIMESTAMP_FORMAT_STR_LEN                             19
#define KVS_CONVERT_TIMESCALE(pts, from_timescale, to_timescale) (pts * to_timescale / from_timescale)

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
/**
 * @UINT64  - IN - timestamp in 100ns to be converted to string
 * @PCHAR   - IN - timestamp format string
 * @PCHAR   - IN - buffer to hold the resulting string
 * @UINT32  - IN - buffer size
 * @PUINT32 - OUT - actual number of characters in the result string not including null terminator
 * @return  - STATUS code of the execution
 */
STATUS generateTimestampStr(UINT64, PCHAR, PCHAR, UINT32, PUINT32);

#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_TIME_INCLUDE__ */
