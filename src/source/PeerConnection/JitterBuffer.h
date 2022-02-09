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
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT__JITTERBUFFER_H
#define __KINESIS_VIDEO_WEBRTC_CLIENT__JITTERBUFFER_H

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
#include "kvs/WebRTCClient.h"
#include "hash_table.h"
#include "RtpPacket.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
typedef STATUS (*FrameReadyFunc)(UINT64, UINT16, UINT16, UINT32);
typedef STATUS (*FrameDroppedFunc)(UINT64, UINT16, UINT16, UINT32);
#define UINT16_DEC(a) ((UINT16)((a) -1))

#define JITTER_BUFFER_HASH_TABLE_BUCKET_COUNT  3000
#define JITTER_BUFFER_HASH_TABLE_BUCKET_LENGTH 2

typedef struct __JitterBuffer {
    FrameReadyFunc onFrameReadyFn;
    FrameDroppedFunc onFrameDroppedFn;
    DepayRtpPayloadFunc depayPayloadFn;

    // used for calculating interarrival jitter https://tools.ietf.org/html/rfc3550#section-6.4.1
    // https://tools.ietf.org/html/rfc3550#appendix-A.8
    // holds the relative transit time for the previous packet
    UINT64 transit;
    // holds estimated jitter, in clockRate units
    DOUBLE jitter;
    UINT32 lastPushTimestamp;
    UINT16 lastRemovedSequenceNumber;
    UINT16 lastPopSequenceNumber;
    UINT32 lastPopTimestamp;
    UINT64 maxLatency;
    UINT64 customData;
    UINT32 clockRate;
    BOOL started;
    PHashTable pPkgBufferHashTable;
} JitterBuffer, *PJitterBuffer;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS createJitterBuffer(FrameReadyFunc, FrameDroppedFunc, DepayRtpPayloadFunc, UINT32, UINT32, UINT64, PJitterBuffer*);
STATUS freeJitterBuffer(PJitterBuffer*);
STATUS jitterBufferPush(PJitterBuffer, PRtpPacket, PBOOL);
STATUS jitterBufferPop(PJitterBuffer, BOOL);
STATUS jitterBufferDropBufferData(PJitterBuffer, UINT16, UINT16, UINT32);
STATUS jitterBufferFillFrameData(PJitterBuffer, PBYTE, UINT32, PUINT32, UINT16, UINT16);

#ifdef __cplusplus
}
#endif
#endif //__KINESIS_VIDEO_WEBRTC_CLIENT__JITTERBUFFER_H
