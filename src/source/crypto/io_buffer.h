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
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_CRYPTO_IOBUFFER__
#define __KINESIS_VIDEO_WEBRTC_CLIENT_CRYPTO_IOBUFFER__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "kvs/error.h"
#include "kvs/common_defs.h"
#include "kvs/platform_utils.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
typedef struct __IOBuffer IOBuffer, *PIOBuffer;
struct __IOBuffer {
    UINT32 off, len, cap;
    PBYTE raw;
};

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS createIOBuffer(UINT32, PIOBuffer*);
STATUS freeIOBuffer(PIOBuffer*);
STATUS ioBufferReset(PIOBuffer);
STATUS ioBufferWrite(PIOBuffer, PBYTE, UINT32);
STATUS ioBufferRead(PIOBuffer, PBYTE, UINT32, PUINT32);

#ifdef __cplusplus
}
#endif
#endif //__KINESIS_VIDEO_WEBRTC_CLIENT_CRYPTO_IOBUFFER__
