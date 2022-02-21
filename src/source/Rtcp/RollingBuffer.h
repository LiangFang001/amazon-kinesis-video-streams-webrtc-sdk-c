/*******************************************
RTCP Rolling Buffer include file
*******************************************/
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_RTCP_ROLLING_BUFFER_H
#define __KINESIS_VIDEO_WEBRTC_CLIENT_RTCP_ROLLING_BUFFER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "kvs/error.h"
#include "kvs/common_defs.h"
#include "kvs/platform_utils.h"

typedef STATUS (*FreeDataFunc)(PUINT64);

typedef struct {
    // Lock guarding the rolling buffer
    MUTEX lock;
    // Max size of data buffer array
    UINT32 capacity;
    // Head index point to next empty slot to put data
    UINT64 headIndex;
    // Tail index point to oldest slot with data inside
    UINT64 tailIndex;
    // Buffer storing pointers, each pointer point to actual data
    PUINT64 dataBuffer;
    // Function being called when data pointer is removed from buffer
    FreeDataFunc freeDataFn;
} RollingBuffer, *PRollingBuffer;

#define ROLLING_BUFFER_MAP_INDEX(pRollingBuffer, index) ((index) % (pRollingBuffer)->capacity)

STATUS rolling_buffer_create(UINT32, FreeDataFunc, PRollingBuffer*);
STATUS rolling_buffer_free(PRollingBuffer*);
STATUS rolling_buffer_appendData(PRollingBuffer, UINT64, PUINT64);
STATUS rolling_buffer_insertData(PRollingBuffer, UINT64, UINT64);
STATUS rolling_buffer_extractData(PRollingBuffer, UINT64, PUINT64);
STATUS rolling_buffer_getSize(PRollingBuffer, PUINT32);
STATUS rolling_buffer_isEmpty(PRollingBuffer, PBOOL);

#ifdef __cplusplus
}
#endif
#endif //__KINESIS_VIDEO_WEBRTC_CLIENT_RTCP_ROLLING_BUFFER_H
