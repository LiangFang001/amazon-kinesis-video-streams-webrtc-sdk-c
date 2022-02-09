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
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_SRTP__
#define __KINESIS_VIDEO_WEBRTC_CLIENT_SRTP__

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
#include "crypto.h"
#ifdef ENABLE_STREAMING
#ifdef KVS_PLAT_RTK_FREERTOS
#include <srtp.h>
#else
#include <srtp2/srtp.h>
#endif
#endif

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
#if defined(ENABLE_STREAMING)
typedef struct __SrtpSession {
    // holds the srtp context for transmit  operations
    srtp_t srtp_transmit_session;
    // holds the srtp context for receive  operations
    srtp_t srtp_receive_session;
} SrtpSession, *PSrtpSession;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
/**
 * @brief
 *
 * @param[in] receiveKey
 * @param[in] transmitKey
 * @param[in] profile
 * @param[in] ppSrtpSession
 *
 * @return STATUS status of execution.
 */
STATUS srtp_session_init(PBYTE receiveKey, PBYTE transmitKey, KVS_SRTP_PROFILE profile, PSrtpSession* ppSrtpSession);

STATUS srtp_session_decryptSrtpPacket(PSrtpSession pSrtpSession, PVOID encryptedMessage, PINT32 len);
STATUS srtp_session_decryptSrtcpPacket(PSrtpSession pSrtpSession, PVOID encryptedMessage, PINT32 len);

STATUS srtp_session_encryptRtpPacket(PSrtpSession pSrtpSession, PVOID message, PINT32 len);
STATUS srtp_session_encryptRtcpPacket(PSrtpSession pSrtpSession, PVOID message, PINT32 len);

STATUS srtp_session_free(PSrtpSession* ppSrtpSession);
#endif

#ifdef __cplusplus
}
#endif
#endif //__KINESIS_VIDEO_WEBRTC_CLIENT_SRTP__
