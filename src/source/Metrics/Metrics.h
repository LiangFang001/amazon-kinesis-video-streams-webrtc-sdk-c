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
#ifndef __KINESIS_VIDEO_WEBRTCCLIENT_METRICS_INCLUDE__
#define __KINESIS_VIDEO_WEBRTCCLIENT_METRICS_INCLUDE__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
/**
 * @brief Get specific ICE candidate pair stats
 * @param [in] PRtcPeerConnection Contains the Ice agent object with diagnostics object
 * @param [in/out] PRtcIceCandidatePairStats Fill up the ICE candidate pair stats for application consumption
 * @return Pass/Fail
 *
 */
STATUS metrics_getIceCandidatePairStats(PRtcPeerConnection, PRtcIceCandidatePairStats);

/**
 * @brief Get specific ICE candidate stats
 * @param [in] PRtcPeerConnection Contains the Ice agent object with diagnostics object
 * @param [in/out] PRtcIceCandidateStats Fill up the ICE candidate stats for application consumption
 * @param [in] BOOL If TRUE, remote candidate stats are extracted, else local candidate stats are extracted
 * @return Pass/Fail
 *
 */
STATUS metrics_getIceCandidateStats(PRtcPeerConnection, BOOL, PRtcIceCandidateStats);

/**
 * @brief Get specific ICE server stats
 * metrics_getIceServerStats will return stats for a specific server. In a multi server configuration, it is upto
 * to the application to get Stats for every server being supported / desired server. The application is
 * expected to pass in the specific iceServerIndex for which the stats are desired
 *
 * @param [in] PRtcPeerConnection Contains the Ice agent object with diagnostics object
 * @param [in/out] PRtcIceServerStats Fill up the ICE candidate stats for application consumption
 * @return Pass/Fail
 *
 */
STATUS metrics_getIceServerStats(PRtcPeerConnection, PRtcIceServerStats);

/**
 * @brief Get specific transport stats
 * @param [in] PRtcPeerConnection
 * @param [in/out] PRtcIceCandidateStats Fill up the transport stats for application consumption
 * @return Pass/Fail
 *
 */
STATUS metrics_getTransportStats(PRtcPeerConnection, PRtcTransportStats);

/**
 * @brief Get remote RTP inbound stats
 * @param [in] PRtcPeerConnection
 * @param [in] PRtcRtpTransceiver transceiver to get stats for, first transceiver if NULL
 * @param [in/out] PRtcOutboundRtpStreamStats Fill up the RTP inbound stats for application consumption
 * @return Pass/Fail
 *
 */
STATUS metrics_getRtpRemoteInboundStats(PRtcPeerConnection, PRtcRtpTransceiver, PRtcRemoteInboundRtpStreamStats);

/**
 * @brief Get RTP outbound stats
 * @param [in] PRtcPeerConnection
 * @param [in] PRtcRtpTransceiver transceiver to get stats for, first transceiver if NULL
 * @param [in/out] PRtcOutboundRtpStreamStats Fill up the RTP outbound stats for application consumption
 * @return Pass/Fail
 *
 */
STATUS metrics_getRtpOutboundStats(PRtcPeerConnection, PRtcRtpTransceiver, PRtcOutboundRtpStreamStats);

/**
 * @brief Get RTP inbound stats
 * @param [in] PRtcPeerConnection
 * @param [in] PRtcRtpTransceiver transceiver to get stats for, first transceiver if NULL
 * @param [in/out] PRtcInboundRtpStreamStats Fill up the RTP inbound stats for application consumption
 * @return Pass/Fail
 *
 */
STATUS metrics_getRtpInboundStats(PRtcPeerConnection, PRtcRtpTransceiver, PRtcInboundRtpStreamStats);
#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTCCLIENT_STATS_INCLUDE__ */
