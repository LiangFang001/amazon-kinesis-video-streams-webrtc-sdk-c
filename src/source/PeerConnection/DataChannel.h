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
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_PEERCONNECTION_DATACHANNEL__
#define __KINESIS_VIDEO_WEBRTC_CLIENT_PEERCONNECTION_DATACHANNEL__

#ifdef ENABLE_DATA_CHANNEL
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#define DATA_CHANNEL_PROTOCOL_STR (PCHAR) "SCTP"

typedef struct {
    RtcDataChannel dataChannel;

    PRtcPeerConnection pRtcPeerConnection;
    RtcDataChannelInit rtcDataChannelInit;
    UINT32 channelId;
    UINT64 onMessageCustomData;
    RtcOnMessage onMessage;
    RtcDataChannelStats rtcDataChannelDiagnostics;

    UINT64 onOpenCustomData;
    RtcOnOpen onOpen;
} KvsDataChannel, *PKvsDataChannel;

#ifdef __cplusplus
}
#endif
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_CLIENT_PEERCONNECTION_DATACHANNEL__ */
