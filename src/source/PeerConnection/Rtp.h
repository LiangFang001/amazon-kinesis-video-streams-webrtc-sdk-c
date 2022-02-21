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
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_PEERCONNECTION_RTP__
#define __KINESIS_VIDEO_WEBRTC_CLIENT_PEERCONNECTION_RTP__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "RtpPacket.h"
#include "RtpRollingBuffer.h"
#include "JitterBuffer.h"
#include "PeerConnection.h"
#include "Retransmitter.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
// Default MTU comes from libwebrtc
// https://groups.google.com/forum/#!topic/discuss-webrtc/gH5ysR3SoZI
#define DEFAULT_MTU_SIZE                           1200
#define DEFAULT_ROLLING_BUFFER_DURATION_IN_SECONDS 1                 // 3
#define HIGHEST_EXPECTED_BIT_RATE                  (2 * 1024 * 1024) //(10 * 1024 * 1024)
#define DEFAULT_SEQ_NUM_BUFFER_SIZE                1000
#define DEFAULT_VALID_INDEX_BUFFER_SIZE            1000
#define DEFAULT_PEER_FRAME_BUFFER_SIZE             (5 * 1024)
#define SRTP_AUTH_TAG_OVERHEAD                     10

// https://www.w3.org/TR/webrtc-stats/#dom-rtcoutboundrtpstreamstats-huge
// Huge frames, by definition, are frames that have an encoded size at least 2.5 times the average size of the frames.
#define HUGE_FRAME_MULTIPLIER 2.5

typedef struct {
    UINT8 payloadType;
    UINT8 rtxPayloadType;
    UINT16 sequenceNumber;
    UINT16 rtxSequenceNumber;
    UINT32 ssrc;
    UINT32 rtxSsrc;
    PayloadArray payloadArray;

    RtcMediaStreamTrack track;
    PRtpRollingBuffer packetBuffer;
    PRetransmitter retransmitter;

    UINT64 rtpTimeOffset;
    UINT64 firstFrameWallClockTime; // 100ns precision

    // used for fps calculation
    UINT64 lastKnownFrameCount;
    UINT64 lastKnownFrameCountTime; // 100ns precision

} RtcRtpSender, *PRtcRtpSender;

typedef struct {
    RtcRtpTransceiver transceiver;
    RtcRtpSender sender;

    PKvsPeerConnection pKvsPeerConnection;

    UINT32 jitterBufferSsrc;
    PJitterBuffer pJitterBuffer;

    UINT64 onFrameCustomData;
    RtcOnFrame onFrame;

    UINT64 onBandwidthEstimationCustomData;
    RtcOnBandwidthEstimation onBandwidthEstimation;
    UINT64 onPictureLossCustomData;
    RtcOnPictureLoss onPictureLoss;

    PBYTE peerFrameBuffer;
    UINT32 peerFrameBufferSize;

    UINT32 rtcpReportsTimerId;

    MUTEX statsLock;
    RtcOutboundRtpStreamStats outboundStats;
    RtcRemoteInboundRtpStreamStats remoteInboundStats;
    RtcInboundRtpStreamStats inboundStats;
} KvsRtpTransceiver, *PKvsRtpTransceiver;
/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS rtp_createTransceiver(RTC_RTP_TRANSCEIVER_DIRECTION, PKvsPeerConnection, UINT32, UINT32, PRtcMediaStreamTrack, PJitterBuffer, RTC_CODEC,
                             PKvsRtpTransceiver*);
STATUS rtp_transceiver_free(PKvsRtpTransceiver*);

STATUS rtp_transceiver_setJitterBuffer(PKvsRtpTransceiver, PJitterBuffer);

#define CONVERT_TIMESTAMP_TO_RTP(clockRate, pts) (pts * clockRate / HUNDREDS_OF_NANOS_IN_A_SECOND)

STATUS rtp_writePacket(PKvsPeerConnection pKvsPeerConnection, PRtpPacket pRtpPacket);

STATUS rtp_findTransceiverByssrc(PKvsPeerConnection pKvsPeerConnection, UINT32 ssrc);
STATUS rtp_transceiver_findBySsrc(PKvsPeerConnection pKvsPeerConnection, PKvsRtpTransceiver* ppTransceiver, UINT32 ssrc);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_CLIENT_PEERCONNECTION_RTP__ */
