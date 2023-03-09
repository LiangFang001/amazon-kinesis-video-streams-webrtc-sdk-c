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
#ifndef __PLATFORM_UTILS_DEFINES_H__
#define __PLATFORM_UTILS_DEFINES_H__

#ifdef __cplusplus
extern "C" {
#endif

#pragma once
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "kvs/common_defs.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
#define CONN_LISTENER_THREAD_NAME "connListener"
#define CONN_LISTENER_THREAD_SIZE 8192
#define WSS_CLIENT_THREAD_NAME    "wss_client" //!< the parameters of wss listener.
#define WSS_CLIENT_THREAD_SIZE    10240
#define WSS_DISPATCH_THREAD_NAME  "wssDispatch" //!< the parameters of wss dispatcher
#define WSS_DISPATCH_THREAD_SIZE  10240
#define PEER_TIMER_NAME           "peerTimer"
#define PEER_TIMER_SIZE           10240

// Tag for the logging
#ifndef LOG_CLASS
#define LOG_CLASS "platform-utils"
#endif

// Max path characters as defined in linux/limits.h
#define MAX_PATH_LEN 256
/**
 * EMA (Exponential Moving Average) alpha value and 1-alpha value - over appx 20 samples
 */
#define EMA_ALPHA_VALUE           ((DOUBLE) 0.05)
#define ONE_MINUS_EMA_ALPHA_VALUE ((DOUBLE) (1 - EMA_ALPHA_VALUE))

/**
 * Calculates the EMA (Exponential Moving Average) accumulator value
 *
 * a - Accumulator value
 * v - Next sample point
 *
 * @return the new Accumulator value
 */
#define EMA_ACCUMULATOR_GET_NEXT(a, v) (DOUBLE)(EMA_ALPHA_VALUE * (v) + ONE_MINUS_EMA_ALPHA_VALUE * (a))

// Max timestamp string length including null terminator
#define MAX_TIMESTAMP_STR_LEN 17

// (thread-0x7000076b3000)
#define MAX_THREAD_ID_STR_LEN 23

// Log print function definition
typedef VOID (*logPrintFunc)(UINT32, PCHAR, PCHAR, ...);

//
// Default logger function
//
PUBLIC_API INLINE VOID defaultLogPrint(UINT32 level, PCHAR tag, PCHAR fmt, ...);

extern logPrintFunc globalCustomLogPrintFn;

#ifdef ANDROID_BUILD
// Compiling with NDK
#include <android/log.h>
#define __LOG(p1, p2, p3, ...)    __android_log_print(p1, p2, p3, ##__VA_ARGS__)
#define __ASSERT(p1, p2, p3, ...) __android_log_assert(p1, p2, p3, ##__VA_ARGS__)
#else
// Compiling under non-NDK
#include <stddef.h>
#include <stdlib.h>
#include <assert.h>
#define __ASSERT(p1, p2, p3, ...) assert(p1)
#define __LOG                     globalCustomLogPrintFn
#endif // ANDROID_BUILD

#define LOG_LEVEL_VERBOSE 1
#define LOG_LEVEL_DEBUG   2
#define LOG_LEVEL_BETA    3
#define LOG_LEVEL_INFO    4
#define LOG_LEVEL_WARN    5
#define LOG_LEVEL_ERROR   6
#define LOG_LEVEL_FATAL   7
#define LOG_LEVEL_SILENT  8

#define LOG_LEVEL_VERBOSE_STR (PCHAR) "VERBOSE"
#define LOG_LEVEL_DEBUG_STR   (PCHAR) "DEBUG"
#define LOG_LEVEL_BETA_STR    (PCHAR) "BETA"
#define LOG_LEVEL_INFO_STR    (PCHAR) "INFO"
#define LOG_LEVEL_WARN_STR    (PCHAR) "WARN"
#define LOG_LEVEL_ERROR_STR   (PCHAR) "ERROR"
#define LOG_LEVEL_FATAL_STR   (PCHAR) "FATAL"
#define LOG_LEVEL_SILENT_STR  (PCHAR) "SILENT"

// LOG_LEVEL_VERBOSE_STR string lenth
#define MAX_LOG_LEVEL_STRLEN 7

// Extra logging macros
#ifndef DLOGE
//#define DLOGE(fmt, ...) __LOG(LOG_LEVEL_ERROR, (PCHAR) LOG_CLASS, (PCHAR) "%s(): " fmt, __FUNCTION__, ##__VA_ARGS__)
#define DLOGE(fmt, ...) __LOG(LOG_LEVEL_ERROR, (PCHAR) LOG_CLASS, (PCHAR) fmt, ##__VA_ARGS__)
#endif
#ifndef DLOGW
#define DLOGW(fmt, ...) __LOG(LOG_LEVEL_WARN, (PCHAR) LOG_CLASS, (PCHAR) fmt, ##__VA_ARGS__)
#endif
#ifndef DLOGI
#define DLOGI(fmt, ...) __LOG(LOG_LEVEL_INFO, (PCHAR) LOG_CLASS, (PCHAR) fmt, ##__VA_ARGS__)
#endif
#ifndef DLOGB
#define DLOGB(fmt, ...) __LOG(LOG_LEVEL_BETA, (PCHAR) LOG_CLASS, (PCHAR) fmt, ##__VA_ARGS__)
#endif
#ifndef DLOGD
#define DLOGD(fmt, ...) __LOG(LOG_LEVEL_DEBUG, (PCHAR) LOG_CLASS, (PCHAR) fmt, ##__VA_ARGS__)
#endif
#ifndef DLOGV
#define DLOGV(fmt, ...) __LOG(LOG_LEVEL_VERBOSE, (PCHAR) LOG_CLASS, (PCHAR) fmt, ##__VA_ARGS__)
#endif
#ifndef ENTER
#define ENTER() DLOGV("Enter")
#endif
#ifndef LEAVE
#define LEAVE() DLOGV("Leave")
#endif
#ifndef ENTERS
#define ENTERS() DLOGS("Enter")
#endif
#ifndef LEAVES
#define LEAVES() DLOGS("Leave")
#endif

#define DLOGD_LINE() DLOGD("%s(%d)", __func__, __LINE__)

// Optionally log very verbose data (>200 msgs/second) about the streaming process
#ifndef DLOGS
#ifdef LOG_STREAMING
#define DLOGS DLOGV
#else
#define DLOGS(...)                                                                                                                                   \
    do {                                                                                                                                             \
    } while (0)
#endif
#endif

// Assertions
#ifndef CONDITION
#ifdef __GNUC__
#define CONDITION(cond) (__builtin_expect((cond) != 0, 0))
#else
#define CONDITION(cond) ((cond) == TRUE)
#endif
#endif
#ifndef LOG_ALWAYS_FATAL_IF
#define LOG_ALWAYS_FATAL_IF(cond, ...) ((CONDITION(cond)) ? ((void) __ASSERT(FALSE, (PCHAR) LOG_CLASS, ##__VA_ARGS__)) : (void) 0)
#endif

#ifndef LOG_ALWAYS_FATAL
#define LOG_ALWAYS_FATAL(...) (((void) __ASSERT(FALSE, (PCHAR) LOG_CLASS, ##__VA_ARGS__)))
#endif

#ifndef SANITIZED_FILE
#define SANITIZED_FILE (STRRCHR(__FILE__, '/') ? STRRCHR(__FILE__, '/') + 1 : __FILE__)
#endif
#ifndef CRASH
#define CRASH(fmt, ...) LOG_ALWAYS_FATAL("%s::%s: " fmt, (PCHAR) LOG_CLASS, __FUNCTION__, ##__VA_ARGS__)
#endif
#ifndef CHECK
#define CHECK(x) LOG_ALWAYS_FATAL_IF(!(x), "%s::%s: ASSERTION FAILED at %s:%d: " #x, (PCHAR) LOG_CLASS, __FUNCTION__, SANITIZED_FILE, __LINE__)
#endif
#ifndef CHECK_EXT
#define CHECK_EXT(x, fmt, ...)                                                                                                                       \
    LOG_ALWAYS_FATAL_IF(!(x), "%s::%s: ASSERTION FAILED at %s:%d: " fmt, (PCHAR) LOG_CLASS, __FUNCTION__, SANITIZED_FILE, __LINE__, ##__VA_ARGS__)
#endif

////////////////////////////////////////////////////
// Conditional checks
////////////////////////////////////////////////////
#define CHK(condition, errRet)                                                                                                                       \
    do {                                                                                                                                             \
        if (!(condition)) {                                                                                                                          \
            retStatus = (errRet);                                                                                                                    \
            goto CleanUp;                                                                                                                            \
        }                                                                                                                                            \
    } while (FALSE)

#define CHK_ERR(condition, errRet, errorMessage, ...)                                                                                                \
    do {                                                                                                                                             \
        if (!(condition)) {                                                                                                                          \
            retStatus = (errRet);                                                                                                                    \
            DLOGE(errorMessage, ##__VA_ARGS__);                                                                                                      \
            goto CleanUp;                                                                                                                            \
        }                                                                                                                                            \
    } while (FALSE)

#define CHK_WARN(condition, errRet, errorMessage, ...)                                                                                               \
    do {                                                                                                                                             \
        if (!(condition)) {                                                                                                                          \
            retStatus = (errRet);                                                                                                                    \
            DLOGW(errorMessage, ##__VA_ARGS__);                                                                                                      \
            goto CleanUp;                                                                                                                            \
        }                                                                                                                                            \
    } while (FALSE)

#define CHK_STATUS_ERR(condition, errRet, errorMessage, ...)                                                                                         \
    do {                                                                                                                                             \
        STATUS __status = condition;                                                                                                                 \
        if (STATUS_FAILED(__status)) {                                                                                                               \
            retStatus = (__status);                                                                                                                  \
            DLOGE(errorMessage, ##__VA_ARGS__);                                                                                                      \
            goto CleanUp;                                                                                                                            \
        }                                                                                                                                            \
    } while (FALSE)

#define CHK_STATUS(condition)                                                                                                                        \
    do {                                                                                                                                             \
        STATUS __status = condition;                                                                                                                 \
        if (STATUS_FAILED(__status)) {                                                                                                               \
            retStatus = (__status);                                                                                                                  \
            goto CleanUp;                                                                                                                            \
        }                                                                                                                                            \
    } while (FALSE)

#define CHK_STATUS_CONTINUE(condition)                                                                                                               \
    do {                                                                                                                                             \
        STATUS __status = condition;                                                                                                                 \
        if (STATUS_FAILED(__status)) {                                                                                                               \
            retStatus = __status;                                                                                                                    \
        }                                                                                                                                            \
    } while (FALSE)

#define CHK_HANDLE(handle)                                                                                                                           \
    do {                                                                                                                                             \
        if (IS_VALID_HANDLE(handle)) {                                                                                                               \
            retStatus = (STATUS_INVALID_HANDLE_ERROR);                                                                                               \
            goto CleanUp;                                                                                                                            \
        }                                                                                                                                            \
    } while (FALSE)

#define CHK_LOG(condition, logMessage)                                                                                                               \
    do {                                                                                                                                             \
        STATUS __status = condition;                                                                                                                 \
        if (STATUS_FAILED(__status)) {                                                                                                               \
            DLOGS("%s Returned status code: 0x%08x", logMessage, __status);                                                                          \
        }                                                                                                                                            \
    } while (FALSE)

#define CHK_LOG_ERR(condition)                                                                                                                       \
    do {                                                                                                                                             \
        STATUS __status = condition;                                                                                                                 \
        if (STATUS_FAILED(__status)) {                                                                                                               \
            DLOGE("operation returned status code: 0x%08x from %s(%d)", __status, __func__, __LINE__);                                               \
        }                                                                                                                                            \
    } while (FALSE)

////////////////////////////////////////////////////
// Callbacks definitions
////////////////////////////////////////////////////

////////////////////////////////////////////////////
// Main structure declarations
////////////////////////////////////////////////////

/**
 * Current versions of the public facing structures
 */
#define FRAME_CURRENT_VERSION 0

/**
 * Frame types enum
 */
typedef enum {
    /**
     * No flags are set
     */
    FRAME_FLAG_NONE = 0,

    /**
     * The frame is a key frame - I or IDR
     */
    FRAME_FLAG_KEY_FRAME = (1 << 0),

    /**
     * The frame is discardable - no other frames depend on it
     */
    FRAME_FLAG_DISCARDABLE_FRAME = (1 << 1),

    /**
     * The frame is invisible for rendering
     */
    FRAME_FLAG_INVISIBLE_FRAME = (1 << 2),

    /**
     * The frame is an explicit marker for the end-of-fragment
     */
    FRAME_FLAG_END_OF_FRAGMENT = (1 << 3),
} FRAME_FLAGS;

/**
 * The representation of the Frame
 */
typedef struct {
    UINT32 version;

    // Id of the frame
    UINT32 index;

    // Flags associated with the frame (ex. IFrame for frames)
    FRAME_FLAGS flags;

    // The decoding timestamp of the frame in 100ns precision
    UINT64 decodingTs;

    // The presentation timestamp of the frame in 100ns precision
    UINT64 presentationTs;

    // The duration of the frame in 100ns precision. Can be 0.
    UINT64 duration;

    // Size of the frame data in bytes
    UINT32 size;

    // The frame bits
    PBYTE frameData;

    // Id of the track this frame belong to
    UINT64 trackId;
} Frame, *PFrame;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

#ifdef __cplusplus
}
#endif

#endif // __PLATFORM_UTILS_DEFINES_H__
