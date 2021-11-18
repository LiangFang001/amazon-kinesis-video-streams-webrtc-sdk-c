/**
 * Main public include file
 */
#ifndef __MKV_GEN_INCLUDE__
#define __MKV_GEN_INCLUDE__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <com/amazonaws/kinesis/video/common/CommonDefs.h>
#include <com/amazonaws/kinesis/video/common/PlatformUtils.h>

// IMPORTANT! Some of the headers are not tightly packed!
////////////////////////////////////////////////////
// Public headers
////////////////////////////////////////////////////
#include <com/amazonaws/kinesis/video/utils/Include.h>
#include <com/amazonaws/kinesis/video/webrtcclient/Error.h>

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

////////////////////////////////////////////////////
// Callbacks definitions
////////////////////////////////////////////////////
/**
 * Gets the current time in 100ns from some timestamp.
 *
 * @param 1 UINT64 - Custom handle passed by the caller.
 *
 * @return Current time value in 100ns
 */
typedef UINT64 (*GetCurrentTimeFunc)(UINT64);

#ifdef __cplusplus
}
#endif
#endif /* __MKV_GEN_INCLUDE__ */
