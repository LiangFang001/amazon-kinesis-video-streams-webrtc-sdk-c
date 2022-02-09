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
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_PEERCONNECTION_PEERCONNECTION__
#define __KINESIS_VIDEO_WEBRTC_CLIENT_PEERCONNECTION_PEERCONNECTION__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "kvs/error.h"
#include "kvs/common_defs.h"
#include "hash_table.h"
#include "double_linked_list.h"
#include "dtls.h"
#include "ice_agent.h"
#include "network.h"
#include "srtp_session.h"
#include "sctp_session.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
#define LOCAL_ICE_UFRAG_LEN 4
#define LOCAL_ICE_PWD_LEN   24
#define LOCAL_CNAME_LEN     16

// https://tools.ietf.org/html/rfc5245#section-15.4
#define MAX_ICE_UFRAG_LEN 256
#define MAX_ICE_PWD_LEN   256

#define PEER_FRAME_BUFFER_SIZE_INCREMENT_FACTOR 1.5

// A non-comprehensive list of valid JSON characters
#define VALID_CHAR_SET_FOR_JSON "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/"

#define ICE_CANDIDATE_JSON_TEMPLATE (PCHAR) "{\"candidate\":\"candidate:%s\",\"sdpMid\":\"0\",\"sdpMLineIndex\":0}"

#define MAX_ICE_CANDIDATE_JSON_LEN (MAX_SDP_ATTRIBUTE_VALUE_LENGTH + SIZEOF(ICE_CANDIDATE_JSON_TEMPLATE) + 1)

#define CODEC_HASH_TABLE_BUCKET_COUNT  50
#define CODEC_HASH_TABLE_BUCKET_LENGTH 2
#define RTX_HASH_TABLE_BUCKET_COUNT    50
#define RTX_HASH_TABLE_BUCKET_LENGTH   2

#define DATA_CHANNEL_HASH_TABLE_BUCKET_COUNT  200
#define DATA_CHANNEL_HASH_TABLE_BUCKET_LENGTH 2

// Environment variable to display SDPs
#define DEBUG_LOG_SDP ((PCHAR) "DEBUG_LOG_SDP")

typedef enum {
    RTC_RTX_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE = 1,
    RTC_RTX_CODEC_VP8 = 2,
} RTX_CODEC;
/**
 * @brief internal structure for peer connection.
 */
typedef struct {
    RtcPeerConnection peerConnection;
    PIceAgent pIceAgent;
    PDtlsSession pDtlsSession;
    BOOL dtlsIsServer; //!< indicate the role of dtls session.
#ifdef ENABLE_STREAMING
    MUTEX pSrtpSessionLock; //!< the lock for srtp session.
    PSrtpSession pSrtpSession;
#endif
#ifdef ENABLE_DATA_CHANNEL
    PSctpSession pSctpSession;
#endif
    SessionDescription remoteSessionDescription; //!< the session desciption of the remote peer.
    PDoubleList pTransceivers;                   //!< the transceivers.
    BOOL sctpIsEnabled;                          //!< enable the data channel or not. indicate that support sctp or not.

    CHAR localIceUfrag[LOCAL_ICE_UFRAG_LEN + 1];
    CHAR localIcePwd[LOCAL_ICE_PWD_LEN + 1];

    CHAR remoteIceUfrag[MAX_ICE_UFRAG_LEN + 1];
    CHAR remoteIcePwd[MAX_ICE_PWD_LEN + 1];

    CHAR localCNAME[LOCAL_CNAME_LEN + 1];

    CHAR remoteCertificateFingerprint[CERTIFICATE_FINGERPRINT_LENGTH + 1];

    MUTEX peerConnectionObjLock;

    BOOL isOffer; //!< the one creates the offer.

    TIMER_QUEUE_HANDLE timerQueueHandle;

    // Codecs that we support and their payloadTypes
    // When offering, we generate values starting from 96
    // When answering, this is populated from the remote offer
    PHashTable pCodecTable;

    // Payload types that we use to retransmit data
    // When answering this is populated from the remote offer
    PHashTable pRtxTable;

    // DataChannels keyed by streamId
    PHashTable pDataChannels;

    UINT64 onDataChannelCustomData;
#ifdef ENABLE_DATA_CHANNEL
    RtcOnDataChannel onDataChannel;
#endif
    UINT64 onIceCandidateCustomData;
    RtcOnIceCandidate onIceCandidate;

    UINT64 onConnectionStateChangeCustomData;
    RtcOnConnectionStateChange onConnectionStateChange; //!< the callback of peer connection change.
    RTC_PEER_CONNECTION_STATE connectionState;

    UINT16 MTU;

    NullableBool canTrickleIce; //!< indicate the behavior of ice, trickle ice or non-trickle ice.
                                ///!< https://tools.ietf.org/html/rfc8838
} KvsPeerConnection, *PKvsPeerConnection;
#ifdef ENABLE_DATA_CHANNEL
typedef struct {
    UINT32 currentDataChannelId;
    PKvsPeerConnection pKvsPeerConnection;
    PHashTable unkeyedDataChannels;
} AllocateSctpSortDataChannelsData, *PAllocateSctpSortDataChannelsData;
#endif

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS onFrameReadyFunc(UINT64, UINT16, UINT16, UINT32);
STATUS onFrameDroppedFunc(UINT64, UINT16, UINT16, UINT32);
/**
 * @brief the callback for dtls socket layer.
 *
 * @param[in] customData the user context.
 * @param[in] pPacket the address of packet.
 * @param[in] packetLen the length of packet.
 *
 * @return STATUS status of execution
 */
VOID onSctpSessionOutboundPacket(UINT64, PBYTE, UINT32);
VOID onSctpSessionDataChannelMessage(UINT64, UINT32, BOOL, PBYTE, UINT32);
VOID onSctpSessionDataChannelOpen(UINT64, UINT32, PBYTE, UINT32);

STATUS peer_connection_sendPacketToRtpReceiver(PKvsPeerConnection, PBYTE, UINT32);
STATUS peer_connection_changeState(PKvsPeerConnection, RTC_PEER_CONNECTION_STATE);

STATUS json_generateSafeString(PCHAR, UINT32);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_CLIENT_PEERCONNECTION_PEERCONNECTION__ */
