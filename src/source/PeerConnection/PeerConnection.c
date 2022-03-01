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
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#define LOG_CLASS "PeerConnection"

#include "dtls.h"
#include "connection_listener.h"
#include "ice_agent_fsm.h"
#include "network.h"
#include "RtcpPacket.h"
#include "JitterBuffer.h"
#include "PeerConnection.h"
#include "SessionDescription.h"
#include "Rtp.h"
#include "Rtcp.h"
#include "Sdp.h"
#include "DataChannel.h"
#include "RtpVP8Payloader.h"
#include "RtpH264Payloader.h"
#include "RtpOpusPayloader.h"
#include "RtpG711Payloader.h"
#include "timer_queue.h"
#include "time_port.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
#define PC_ENTER()  // ENTER()
#define PC_LEAVE()  // LEAVE()
#define PC_ENTERS() // ENTERS()
#define PC_LEAVES() // LEAVES()
static volatile ATOMIC_BOOL gKvsWebRtcInitialized = (SIZE_T) FALSE;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
#ifdef ENABLE_STREAMING
STATUS peer_connection_allocateSrtp(PKvsPeerConnection pKvsPeerConnection)
{
    PC_ENTER();
    PDtlsKeyingMaterial pDtlsKeyingMaterial = NULL;
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;

    CHK(NULL != (pDtlsKeyingMaterial = (PDtlsKeyingMaterial) MEMALLOC(SIZEOF(DtlsKeyingMaterial))), STATUS_PEER_CONN_NOT_ENOUGH_MEMORY);
    MEMSET(pDtlsKeyingMaterial, 0, SIZEOF(DtlsKeyingMaterial));

    CHK(pKvsPeerConnection != NULL, STATUS_SUCCESS);
    CHK_STATUS(dtls_session_verifyRemoteCertificateFingerprint(pKvsPeerConnection->pDtlsSession, pKvsPeerConnection->remoteCertificateFingerprint));
    CHK_STATUS(dtls_session_populateKeyingMaterial(pKvsPeerConnection->pDtlsSession, pDtlsKeyingMaterial));

    MUTEX_LOCK(pKvsPeerConnection->pSrtpSessionLock);
    locked = TRUE;

    CHK_STATUS(srtp_session_init(pKvsPeerConnection->dtlsIsServer ? pDtlsKeyingMaterial->clientWriteKey : pDtlsKeyingMaterial->serverWriteKey,
                                 pKvsPeerConnection->dtlsIsServer ? pDtlsKeyingMaterial->serverWriteKey : pDtlsKeyingMaterial->clientWriteKey,
                                 pDtlsKeyingMaterial->srtpProfile, &(pKvsPeerConnection->pSrtpSession)));

CleanUp:
    if (locked) {
        MUTEX_UNLOCK(pKvsPeerConnection->pSrtpSessionLock);
    }

    SAFE_MEMFREE(pDtlsKeyingMaterial);

    if (STATUS_FAILED(retStatus)) {
        DLOGW("dtls_session_populateKeyingMaterial failed with 0x%08x", retStatus);
    }
    PC_LEAVE();
    return retStatus;
}
#endif

#ifdef ENABLE_DATA_CHANNEL
STATUS allocateSctpSortDataChannelsDataCallback(UINT64 customData, PHashEntry pHashEntry)
{
    PC_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PAllocateSctpSortDataChannelsData data = (PAllocateSctpSortDataChannelsData) customData;
    PKvsDataChannel pKvsDataChannel = (PKvsDataChannel) pHashEntry->value;

    CHK(customData != 0, STATUS_PEER_CONN_NULL_ARG);

    pKvsDataChannel->channelId = data->currentDataChannelId;
    CHK_STATUS(hashTablePut(data->pKvsPeerConnection->pDataChannels, pKvsDataChannel->channelId, (UINT64) pKvsDataChannel));

    data->currentDataChannelId += 2;

CleanUp:
    PC_LEAVE();
    return retStatus;
}
/**
 * @brief handle the packets of Data Channel Establishment Protocol(DCEP).
 *
 * @param[in] pKvsPeerConnection the context of the peer connection.
 *
 * @return STATUS status of execution
 */
STATUS peer_connection_allocateSctp(PKvsPeerConnection pKvsPeerConnection)
{
    PC_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    SctpSessionCallbacks sctpSessionCallbacks;
    AllocateSctpSortDataChannelsData data;
    UINT32 currentDataChannelId = 0;
    UINT64 hashValue = 0;
    PKvsDataChannel pKvsDataChannel = NULL;

    CHK(pKvsPeerConnection != NULL, STATUS_PEER_CONN_NULL_ARG);
    currentDataChannelId = (pKvsPeerConnection->dtlsIsServer) ? 1 : 0;

    // Re-sort DataChannel hashmap using proper streamIds if we are offerer or answerer
    data.currentDataChannelId = currentDataChannelId;
    data.pKvsPeerConnection = pKvsPeerConnection;
    data.unkeyedDataChannels = pKvsPeerConnection->pDataChannels;
    CHK_STATUS(hashTableCreateWithParams(CODEC_HASH_TABLE_BUCKET_COUNT, CODEC_HASH_TABLE_BUCKET_LENGTH, &pKvsPeerConnection->pDataChannels));
    CHK_STATUS(hashTableIterateEntries(data.unkeyedDataChannels, (UINT64) &data, allocateSctpSortDataChannelsDataCallback));

    // Free unkeyed DataChannels
    CHK_LOG_ERR(hashTableClear(data.unkeyedDataChannels));
    CHK_LOG_ERR(hashTableFree(data.unkeyedDataChannels));

    // Create the SCTP Session
    // setup the sctp callback.
    sctpSessionCallbacks.outboundPacketFunc = onSctpSessionOutboundPacket;
    sctpSessionCallbacks.dataChannelMessageFunc = onSctpSessionDataChannelMessage;
    sctpSessionCallbacks.dataChannelOpenFunc = onSctpSessionDataChannelOpen;
    sctpSessionCallbacks.customData = (UINT64) pKvsPeerConnection;
    CHK_STATUS(sctp_session_create(&sctpSessionCallbacks, &(pKvsPeerConnection->pSctpSession)));

    for (; currentDataChannelId < data.currentDataChannelId; currentDataChannelId += 2) {
        pKvsDataChannel = NULL;
        retStatus = hashTableGet(pKvsPeerConnection->pDataChannels, currentDataChannelId, &hashValue);
        pKvsDataChannel = (PKvsDataChannel) hashValue;
        if (retStatus == STATUS_SUCCESS || retStatus == STATUS_HASH_KEY_NOT_PRESENT) {
            retStatus = STATUS_SUCCESS;
        } else {
            CHK(FALSE, retStatus);
        }
        CHK(pKvsDataChannel != NULL, STATUS_INTERNAL_ERROR);
        sctp_session_sendDcepMsg(pKvsPeerConnection->pSctpSession, currentDataChannelId, pKvsDataChannel->dataChannel.name,
                                 STRLEN(pKvsDataChannel->dataChannel.name), &pKvsDataChannel->rtcDataChannelInit);
        pKvsDataChannel->rtcDataChannelDiagnostics.state = RTC_DATA_CHANNEL_STATE_OPEN;

        if (STATUS_FAILED(hashTableUpsert(pKvsPeerConnection->pDataChannels, currentDataChannelId, (UINT64) pKvsDataChannel))) {
            DLOGW("Failed to update entry in hash table with recent changes to data channel");
        }
        if (pKvsDataChannel->onOpen != NULL) {
            pKvsDataChannel->onOpen(pKvsDataChannel->onOpenCustomData, &pKvsDataChannel->dataChannel);
        }
    }

CleanUp:
    PC_LEAVE();
    return retStatus;
}
#endif
/**
 * @brief the inbound callback for the socket layer of the ice agent for non-stun packets.
 *
 * @param[in] customData the context of peer connection.
 * @param[in] buff the address of the buffer.
 * @param[in] buffLen the length of the buffer.
 *
 * @return STATUS status of execution
 */
VOID peer_connection_onInboundPacket(UINT64 customData, PBYTE buff, UINT32 buffLen)
{
    PC_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) customData;
    BOOL isDtlsConnected = FALSE;
    INT32 signedBuffLen = buffLen;

    CHK(signedBuffLen > 2 && pKvsPeerConnection != NULL, STATUS_SUCCESS);

    /*
     demux each packet off of its first byte
     https://tools.ietf.org/html/rfc5764#section-5.1.2
                 +----------------+
                  | 127 < B < 192 -+--> forward to RTP
                  |                |
      packet -->  |  19 < B < 64  -+--> forward to DTLS
                  |                |
                  |       B < 2   -+--> forward to STUN
                  +----------------+
    */
    if (buff[0] > 19 && buff[0] < 64) {
        dtls_session_read(pKvsPeerConnection->pDtlsSession, buff, &signedBuffLen);

        if (signedBuffLen > 0) {
#ifdef ENABLE_DATA_CHANNEL
            CHK_STATUS(sctp_session_putInboundPacket(pKvsPeerConnection->pSctpSession, buff, signedBuffLen));
#endif
        }

        CHK_STATUS(dtls_session_isInitFinished(pKvsPeerConnection->pDtlsSession, &isDtlsConnected));
        if (isDtlsConnected) {
#ifdef ENABLE_STREAMING
            if (pKvsPeerConnection->pSrtpSession == NULL) {
                CHK_STATUS(peer_connection_allocateSrtp(pKvsPeerConnection));
            }
#endif

#ifdef ENABLE_DATA_CHANNEL
            if (pKvsPeerConnection->pSctpSession == NULL) {
                CHK_STATUS(peer_connection_allocateSctp(pKvsPeerConnection));
            }
#endif
            peer_connection_changeState(pKvsPeerConnection, RTC_PEER_CONNECTION_STATE_CONNECTED);
        } else {
            DLOGD("dtls session is not ready.");
        }

    }
#ifdef ENABLE_STREAMING
    else if ((buff[0] > 127 && buff[0] < 192) && (pKvsPeerConnection->pSrtpSession != NULL)) {

        if (buff[1] >= 192 && buff[1] <= 223) {
            // rtcp
            if (STATUS_FAILED(retStatus = srtp_session_decryptSrtcpPacket(pKvsPeerConnection->pSrtpSession, buff, &signedBuffLen))) {
                DLOGW("srtp_session_decryptSrtcpPacket failed with 0x%08x", retStatus);
                CHK(FALSE, STATUS_SUCCESS);
            }

            CHK_STATUS(rtcp_onPacket(pKvsPeerConnection, buff, signedBuffLen));
        } else {
            // rtp
            CHK_STATUS(peer_connection_sendPacketToRtpReceiver(pKvsPeerConnection, buff, signedBuffLen));
        }
    }
#endif
    else {
        DLOGW("unhandled pc inbound packet.");
    }
CleanUp:
    PC_LEAVE();
    CHK_LOG_ERR(retStatus);
}

#ifdef ENABLE_STREAMING
STATUS peer_connection_sendPacketToRtpReceiver(PKvsPeerConnection pKvsPeerConnection, PBYTE pBuffer, UINT32 bufferLen)
{
    PC_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PDoubleListNode pCurNode = NULL;
    PKvsRtpTransceiver pTransceiver;
    UINT64 item, now;
    UINT32 ssrc;
    PRtpPacket pRtpPacket = NULL;
    PBYTE pPayload = NULL;
    BOOL ownedByJitterBuffer = FALSE, discarded = FALSE;
    UINT64 packetsReceived = 0, packetsFailedDecryption = 0, lastPacketReceivedTimestamp = 0, headerBytesReceived = 0, bytesReceived = 0,
           packetsDiscarded = 0;
    INT64 arrival, r_ts, transit, delta;

    CHK(pKvsPeerConnection != NULL && pBuffer != NULL, STATUS_PEER_CONN_NULL_ARG);
    CHK(bufferLen >= MIN_HEADER_LENGTH, STATUS_INVALID_ARG);

    ssrc = getInt32(*(PUINT32)(pBuffer + SSRC_OFFSET));

    CHK_STATUS(doubleListGetHeadNode(pKvsPeerConnection->pTransceivers, &pCurNode));
    while (pCurNode != NULL) {
        CHK_STATUS(doubleListGetNodeData(pCurNode, &item));
        pTransceiver = (PKvsRtpTransceiver) item;

        if (pTransceiver->jitterBufferSsrc == ssrc) {
            packetsReceived++;
            if (STATUS_FAILED(retStatus = srtp_session_decryptSrtpPacket(pKvsPeerConnection->pSrtpSession, pBuffer, (PINT32) &bufferLen))) {
                DLOGW("srtp_session_decryptSrtpPacket failed with 0x%08x", retStatus);
                packetsFailedDecryption++;
                CHK(FALSE, STATUS_SUCCESS);
            }
            now = GETTIME();
            CHK(NULL != (pPayload = (PBYTE) MEMALLOC(bufferLen)), STATUS_PEER_CONN_NOT_ENOUGH_MEMORY);
            MEMCPY(pPayload, pBuffer, bufferLen);
            CHK_STATUS(rtp_packet_createFromBytes(pPayload, bufferLen, &pRtpPacket));
            pRtpPacket->receivedTime = now;

            // https://tools.ietf.org/html/rfc3550#section-6.4.1
            // https://tools.ietf.org/html/rfc3550#appendix-A.8
            // interarrival jitter
            // arrival, the current time in the same units.
            // r_ts, the timestamp from   the incoming packet
            arrival = KVS_CONVERT_TIMESCALE(now, HUNDREDS_OF_NANOS_IN_A_SECOND, pTransceiver->pJitterBuffer->clockRate);
            r_ts = pRtpPacket->header.timestamp;
            transit = arrival - r_ts;
            delta = transit - pTransceiver->pJitterBuffer->transit;
            pTransceiver->pJitterBuffer->transit = transit;
            pTransceiver->pJitterBuffer->jitter += (1. / 16.) * ((DOUBLE) ABS(delta) - pTransceiver->pJitterBuffer->jitter);
            CHK_STATUS(jitter_buffer_push(pTransceiver->pJitterBuffer, pRtpPacket, &discarded));
            if (discarded) {
                packetsDiscarded++;
            }
            lastPacketReceivedTimestamp = KVS_CONVERT_TIMESCALE(now, HUNDREDS_OF_NANOS_IN_A_SECOND, 1000);
            headerBytesReceived += RTP_HEADER_LEN(pRtpPacket);
            bytesReceived += pRtpPacket->rawPacketLength - RTP_HEADER_LEN(pRtpPacket);
            ownedByJitterBuffer = TRUE;
            CHK(FALSE, STATUS_SUCCESS);
        }
        pCurNode = pCurNode->pNext;
    }

    DLOGW("No transceiver to handle inbound ssrc %u", ssrc);

CleanUp:
    if (packetsReceived > 0) {
        MUTEX_LOCK(pTransceiver->statsLock);
        pTransceiver->inboundStats.received.packetsReceived += packetsReceived;
        pTransceiver->inboundStats.packetsFailedDecryption += packetsFailedDecryption;
        pTransceiver->inboundStats.lastPacketReceivedTimestamp = lastPacketReceivedTimestamp;
        pTransceiver->inboundStats.headerBytesReceived += headerBytesReceived;
        pTransceiver->inboundStats.bytesReceived += bytesReceived;
        pTransceiver->inboundStats.received.jitter = pTransceiver->pJitterBuffer->jitter / pTransceiver->pJitterBuffer->clockRate;
        pTransceiver->inboundStats.received.packetsDiscarded = packetsDiscarded;
        MUTEX_UNLOCK(pTransceiver->statsLock);
    }
    if (!ownedByJitterBuffer) {
        SAFE_MEMFREE(pPayload);
        rtp_packet_free(&pRtpPacket);
        CHK_LOG_ERR(retStatus);
    }
    PC_LEAVE();
    return retStatus;
}
#endif

STATUS peer_connection_changeState(PKvsPeerConnection pKvsPeerConnection, RTC_PEER_CONNECTION_STATE newState)
{
    PC_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;
    RtcOnConnectionStateChange onConnectionStateChange = NULL;
    UINT64 customData = 0;

    CHK(pKvsPeerConnection != NULL, STATUS_PEER_CONN_NULL_ARG);

    MUTEX_LOCK(pKvsPeerConnection->peerConnectionObjLock);
    locked = TRUE;

    /* new and closed state are terminal*/
    CHK(pKvsPeerConnection->connectionState != newState && pKvsPeerConnection->connectionState != RTC_PEER_CONNECTION_STATE_FAILED &&
            pKvsPeerConnection->connectionState != RTC_PEER_CONNECTION_STATE_CLOSED,
        retStatus);

    pKvsPeerConnection->connectionState = newState;
    onConnectionStateChange = pKvsPeerConnection->onConnectionStateChange;
    customData = pKvsPeerConnection->onConnectionStateChangeCustomData;

    MUTEX_UNLOCK(pKvsPeerConnection->peerConnectionObjLock);
    locked = FALSE;

    if (onConnectionStateChange != NULL) {
        onConnectionStateChange(customData, newState);
    }

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pKvsPeerConnection->peerConnectionObjLock);
    }

    CHK_LOG_ERR(retStatus);
    PC_LEAVE();
    return retStatus;
}

#ifdef ENABLE_STREAMING
STATUS peer_connection_onFrameReadyFunc(UINT64 customData, UINT16 startIndex, UINT16 endIndex, UINT32 frameSize)
{
    PC_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsRtpTransceiver pTransceiver = (PKvsRtpTransceiver) customData;
    PRtpPacket pPacket = NULL;
    Frame frame;
    UINT64 hashValue;
    UINT32 filledSize = 0, index;

    CHK(pTransceiver != NULL, STATUS_PEER_CONN_NULL_ARG);

    // TODO: handle multi-packet frames
    retStatus = hashTableGet(pTransceiver->pJitterBuffer->pPkgBufferHashTable, startIndex, &hashValue);
    pPacket = (PRtpPacket) hashValue;
    if (retStatus == STATUS_SUCCESS || retStatus == STATUS_HASH_KEY_NOT_PRESENT) {
        retStatus = STATUS_SUCCESS;
    } else {
        CHK(FALSE, retStatus);
    }
    CHK(pPacket != NULL, STATUS_PEER_CONN_NULL_ARG);
    MUTEX_LOCK(pTransceiver->statsLock);
    // https://www.w3.org/TR/webrtc-stats/#dom-rtcinboundrtpstreamstats-jitterbufferdelay
    pTransceiver->inboundStats.jitterBufferDelay += (DOUBLE)(GETTIME() - pPacket->receivedTime) / HUNDREDS_OF_NANOS_IN_A_SECOND;
    index = pTransceiver->inboundStats.jitterBufferEmittedCount;
    pTransceiver->inboundStats.jitterBufferEmittedCount++;
    if (MEDIA_STREAM_TRACK_KIND_VIDEO == pTransceiver->transceiver.receiver.track.kind) {
        pTransceiver->inboundStats.framesReceived++;
    }
    MUTEX_UNLOCK(pTransceiver->statsLock);

    if (frameSize > pTransceiver->peerFrameBufferSize) {
        MEMFREE(pTransceiver->peerFrameBuffer);
        pTransceiver->peerFrameBufferSize = (UINT32)(frameSize * PEER_FRAME_BUFFER_SIZE_INCREMENT_FACTOR);
        pTransceiver->peerFrameBuffer = (PBYTE) MEMALLOC(pTransceiver->peerFrameBufferSize);
        CHK(pTransceiver->peerFrameBuffer != NULL, STATUS_PEER_CONN_NOT_ENOUGH_MEMORY);
    }

    CHK_STATUS(jitter_buffer_fillFrameData(pTransceiver->pJitterBuffer, pTransceiver->peerFrameBuffer, frameSize, &filledSize, startIndex, endIndex));
    CHK(frameSize == filledSize, STATUS_INVALID_ARG_LEN);

    frame.version = FRAME_CURRENT_VERSION;
    frame.decodingTs = pPacket->header.timestamp * HUNDREDS_OF_NANOS_IN_A_MILLISECOND;
    frame.presentationTs = frame.decodingTs;
    frame.frameData = pTransceiver->peerFrameBuffer;
    frame.size = frameSize;
    frame.duration = 0;
    frame.index = index;
    // TODO: Fill frame flag and track id and index if we need to, currently those are not used by RtcRtpTransceiver
    if (pTransceiver->onFrame != NULL) {
        pTransceiver->onFrame(pTransceiver->onFrameCustomData, &frame);
    }

CleanUp:
    CHK_LOG_ERR(retStatus);
    PC_LEAVE();
    return retStatus;
}

STATUS peer_connection_onFrameDroppedFunc(UINT64 customData, UINT16 startIndex, UINT16 endIndex, UINT32 timestamp)
{
    PC_ENTER();
    UNUSED_PARAM(endIndex);
    STATUS retStatus = STATUS_SUCCESS;
    UINT64 hashValue = 0;
    PRtpPacket pPacket = NULL;
    PKvsRtpTransceiver pTransceiver = (PKvsRtpTransceiver) customData;

    DLOGW("Frame with timestamp %u is dropped!", timestamp);
    CHK(pTransceiver != NULL, STATUS_PEER_CONN_NULL_ARG);

    retStatus = hashTableGet(pTransceiver->pJitterBuffer->pPkgBufferHashTable, startIndex, &hashValue);
    pPacket = (PRtpPacket) hashValue;
    if (retStatus == STATUS_SUCCESS || retStatus == STATUS_HASH_KEY_NOT_PRESENT) {
        retStatus = STATUS_SUCCESS;
    } else {
        CHK(FALSE, retStatus);
    }

    // TODO: handle multi-packet frames
    CHK(pPacket != NULL, STATUS_PEER_CONN_NULL_ARG);
    MUTEX_LOCK(pTransceiver->statsLock);
    // https://www.w3.org/TR/webrtc-stats/#dom-rtcinboundrtpstreamstats-jitterbufferdelay
    pTransceiver->inboundStats.jitterBufferDelay += (DOUBLE)(GETTIME() - pPacket->receivedTime) / HUNDREDS_OF_NANOS_IN_A_SECOND;
    pTransceiver->inboundStats.jitterBufferEmittedCount++;
    pTransceiver->inboundStats.received.framesDropped++;
    pTransceiver->inboundStats.received.fullFramesLost++;
    MUTEX_UNLOCK(pTransceiver->statsLock);

CleanUp:
    PC_LEAVE();
    return retStatus;
}
#endif

static VOID onIceConnectionStateChange(UINT64 customData, UINT64 connectionState)
{
    PC_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) customData;
    RTC_PEER_CONNECTION_STATE newConnectionState = RTC_PEER_CONNECTION_STATE_NEW;
    BOOL startDtlsSession = FALSE, dtlsConnected;

    CHK(pKvsPeerConnection != NULL, STATUS_PEER_CONN_NULL_ARG);

    switch (connectionState) {
        case ICE_AGENT_STATE_NEW:
            newConnectionState = RTC_PEER_CONNECTION_STATE_NEW;
            break;

        case ICE_AGENT_STATE_CHECK_CONNECTION:
            newConnectionState = RTC_PEER_CONNECTION_STATE_CONNECTING;
            break;

        case ICE_AGENT_STATE_CONNECTED:
            /* explicit fall-through */
        case ICE_AGENT_STATE_NOMINATING:
            /* explicit fall-through */
        case ICE_AGENT_STATE_READY:
            /* start dtlsSession as soon as ice is connected */
            startDtlsSession = TRUE;
            break;

        case ICE_AGENT_STATE_DISCONNECTED:
            newConnectionState = RTC_PEER_CONNECTION_STATE_DISCONNECTED;
            break;

        case ICE_AGENT_STATE_FAILED:
            newConnectionState = RTC_PEER_CONNECTION_STATE_FAILED;
            break;

        default:
            DLOGW("Unknown ice agent state %" PRIu64, connectionState);
            break;
    }

    if (startDtlsSession) {
        CHK_STATUS(dtls_session_isInitFinished(pKvsPeerConnection->pDtlsSession, &dtlsConnected));

        if (dtlsConnected) {
            // In ICE restart scenario, DTLS handshake is not going to be reset. Therefore, we need to check
            // if the DTLS state has been connected.
            newConnectionState = RTC_PEER_CONNECTION_STATE_CONNECTED;
        } else {
            // PeerConnection's state changes to CONNECTED only when DTLS state is also connected. So, we need
            // wait until DTLS state changes to CONNECTED.
            //
            // Reference: https://w3c.github.io/webrtc-pc/#rtcpeerconnectionstate-enum
            CHK_STATUS(dtls_session_start(pKvsPeerConnection->pDtlsSession, pKvsPeerConnection->dtlsIsServer));
        }
    }

    CHK_STATUS(peer_connection_changeState(pKvsPeerConnection, newConnectionState));

CleanUp:

    CHK_LOG_ERR(retStatus);
    PC_LEAVE();
}

VOID onNewIceLocalCandidate(UINT64 customData, PCHAR candidateSdpStr)
{
    PC_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) customData;
    BOOL locked = FALSE;
    CHAR jsonStrBuffer[MAX_ICE_CANDIDATE_JSON_LEN];
    INT32 strCompleteLen = 0;
    PCHAR pIceCandidateStr = NULL;

    CHK(pKvsPeerConnection != NULL, STATUS_PEER_CONN_NULL_ARG);
    CHK(candidateSdpStr == NULL || STRLEN(candidateSdpStr) < MAX_SDP_ATTRIBUTE_VALUE_LENGTH, STATUS_INVALID_ARG);
    CHK(pKvsPeerConnection->onIceCandidate != NULL, retStatus); // do nothing if onIceCandidate is not implemented

    MUTEX_LOCK(pKvsPeerConnection->peerConnectionObjLock);
    locked = TRUE;

    if (candidateSdpStr != NULL) {
        strCompleteLen = SNPRINTF(jsonStrBuffer, ARRAY_SIZE(jsonStrBuffer), ICE_CANDIDATE_JSON_TEMPLATE, candidateSdpStr);
        CHK(strCompleteLen > 0, STATUS_INTERNAL_ERROR);
        CHK(strCompleteLen < (INT32) ARRAY_SIZE(jsonStrBuffer), STATUS_BUFFER_TOO_SMALL);
        pIceCandidateStr = jsonStrBuffer;
    }

    pKvsPeerConnection->onIceCandidate(pKvsPeerConnection->onIceCandidateCustomData, pIceCandidateStr);

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (locked) {
        MUTEX_UNLOCK(pKvsPeerConnection->peerConnectionObjLock);
    }
    PC_LEAVE();
}

#ifdef ENABLE_DATA_CHANNEL
VOID onSctpSessionOutboundPacket(UINT64 customData, PBYTE pPacket, UINT32 packetLen)
{
    PC_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = NULL;
    if (customData == 0) {
        return;
    }

    pKvsPeerConnection = (PKvsPeerConnection) customData;
    CHK_STATUS(dtls_session_send(pKvsPeerConnection->pDtlsSession, pPacket, packetLen));

CleanUp:
    if (STATUS_FAILED(retStatus)) {
        DLOGW("onSctpSessionOutboundPacket failed with 0x%08x", retStatus);
    }
    PC_LEAVE();
}

VOID onSctpSessionDataChannelMessage(UINT64 customData, UINT32 channelId, BOOL isBinary, PBYTE pMessage, UINT32 pMessageLen)
{
    PC_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) customData;
    PKvsDataChannel pKvsDataChannel = NULL;
    UINT64 hashValue = 0;

    CHK(pKvsPeerConnection != NULL, STATUS_PEER_CONN_NO_CONNECTION);

    retStatus = hashTableGet(pKvsPeerConnection->pDataChannels, channelId, &hashValue);
    pKvsDataChannel = (PKvsDataChannel) hashValue;

    if (retStatus == STATUS_SUCCESS || retStatus == STATUS_HASH_KEY_NOT_PRESENT) {
        retStatus = STATUS_SUCCESS;
    } else {
        CHK(FALSE, retStatus);
    }

    CHK(pKvsDataChannel != NULL && pKvsDataChannel->onMessage != NULL, STATUS_PEER_CONN_NO_ON_MESSAGE);

    pKvsDataChannel->rtcDataChannelDiagnostics.messagesReceived++;
    pKvsDataChannel->rtcDataChannelDiagnostics.bytesReceived += pMessageLen;

    if (STATUS_FAILED(hashTableUpsert(pKvsPeerConnection->pDataChannels, channelId, (UINT64) pKvsDataChannel))) {
        DLOGW("Failed to update entry in hash table with recent changes to data channel");
    }

    pKvsDataChannel->onMessage(pKvsDataChannel->onMessageCustomData, &pKvsDataChannel->dataChannel, isBinary, pMessage, pMessageLen);

CleanUp:
    if (STATUS_FAILED(retStatus)) {
        DLOGW("onSctpSessionDataChannelMessage failed with 0x%08x", retStatus);
    }
    PC_LEAVE();
}

VOID onSctpSessionDataChannelOpen(UINT64 customData, UINT32 channelId, PBYTE pName, UINT32 nameLen)
{
    PC_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) customData;
    PKvsDataChannel pKvsDataChannel = NULL;

    CHK(pKvsPeerConnection != NULL && pKvsPeerConnection->onDataChannel != NULL, STATUS_PEER_CONN_NULL_ARG);

    pKvsDataChannel = (PKvsDataChannel) MEMCALLOC(1, SIZEOF(KvsDataChannel));
    CHK(pKvsDataChannel != NULL, STATUS_PEER_CONN_NOT_ENOUGH_MEMORY);

    STRNCPY(pKvsDataChannel->dataChannel.name, (PCHAR) pName, nameLen);
    pKvsDataChannel->dataChannel.id = channelId;
    pKvsDataChannel->pRtcPeerConnection = (PRtcPeerConnection) pKvsPeerConnection;
    pKvsDataChannel->channelId = channelId;

    // Set the data channel parameters when data channel is created by peer
    pKvsDataChannel->rtcDataChannelDiagnostics.dataChannelIdentifier = channelId;
    pKvsDataChannel->rtcDataChannelDiagnostics.state = RTC_DATA_CHANNEL_STATE_OPEN;
    STRNCPY(pKvsDataChannel->rtcDataChannelDiagnostics.label, (PCHAR) pName, nameLen);
    CHK_STATUS(hashTablePut(pKvsPeerConnection->pDataChannels, channelId, (UINT64) pKvsDataChannel));
    pKvsPeerConnection->onDataChannel(pKvsPeerConnection->onDataChannelCustomData, &(pKvsDataChannel->dataChannel));

CleanUp:

    CHK_LOG_ERR(retStatus);
    PC_LEAVE();
}
#endif

VOID onDtlsOutboundPacket(UINT64 customData, PBYTE pBuffer, UINT32 bufferLen)
{
    PC_ENTER();
    PKvsPeerConnection pKvsPeerConnection = NULL;
    if (customData == 0) {
        PC_LEAVE();
        return;
    }

    pKvsPeerConnection = (PKvsPeerConnection) customData;
    ice_agent_send(pKvsPeerConnection->pIceAgent, pBuffer, bufferLen);
    PC_LEAVE();
}

VOID onDtlsStateChange(UINT64 customData, RTC_DTLS_TRANSPORT_STATE newDtlsState)
{
    PC_ENTER();
    PKvsPeerConnection pKvsPeerConnection = NULL;
    if (customData == 0) {
        PC_LEAVE();
        return;
    }

    pKvsPeerConnection = (PKvsPeerConnection) customData;

    switch (newDtlsState) {
        case RTC_DTLS_TRANSPORT_STATE_CLOSED:
            peer_connection_changeState(pKvsPeerConnection, RTC_PEER_CONNECTION_STATE_CLOSED);
            break;
        default:
            /* explicit ignore */
            break;
    }
    PC_LEAVE();
}

/* Generate a printable string that does not
 * need to be escaped when encoding in JSON
 */
STATUS json_generateSafeString(PCHAR pDst, UINT32 len)
{
    PC_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 i = 0;

    CHK(pDst != NULL, STATUS_PEER_CONN_NULL_ARG);

    for (i = 0; i < len; i++) {
        pDst[i] = VALID_CHAR_SET_FOR_JSON[RAND() % (ARRAY_SIZE(VALID_CHAR_SET_FOR_JSON) - 1)];
    }

CleanUp:

    PC_LEAVES();
    return retStatus;
}
//#TBD
//#ifdef ENABLE_STREAMING
#if 1
/**
 * @brief
 *
 * @param[in] timerId
 * @param[in] currentTime
 * @param[in] customData
 *
 * @return STATUS status of execution.
 */
STATUS peer_connection_rtcpReportsCallback(UINT32 timerId, UINT64 currentTime, UINT64 customData)
{
    UNUSED_PARAM(timerId);
    STATUS retStatus = STATUS_SUCCESS;
    BOOL ready = FALSE;
    UINT64 ntpTime, rtpTime, delay;
    UINT32 packetCount, octetCount, packetLen, allocSize, ssrc;
    PBYTE rawPacket = NULL;
    PKvsPeerConnection pKvsPeerConnection = NULL;

    PKvsRtpTransceiver pKvsRtpTransceiver = (PKvsRtpTransceiver) customData;
    CHK(pKvsRtpTransceiver != NULL && pKvsRtpTransceiver->pJitterBuffer != NULL && pKvsRtpTransceiver->pKvsPeerConnection != NULL,
        STATUS_PEER_CONN_NULL_ARG);
    pKvsPeerConnection = pKvsRtpTransceiver->pKvsPeerConnection;

    ssrc = pKvsRtpTransceiver->sender.ssrc;
    DLOGS("peer_connection_rtcpReportsCallback %" PRIu64 " ssrc: %u rtxssrc: %u", currentTime, ssrc, pKvsRtpTransceiver->sender.rtxSsrc);

    // check if ice agent is connected, reschedule in 200msec if not
    ready = pKvsPeerConnection->pSrtpSession != NULL &&
        currentTime - pKvsRtpTransceiver->sender.firstFrameWallClockTime >= 2500 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND;
    if (!ready) {
        DLOGV("sender report no frames sent %u", ssrc);
    } else {
        // create rtcp sender report packet
        // https://tools.ietf.org/html/rfc3550#section-6.4.1
        ntpTime = rtcp_packet_convertTimestampToNTP(currentTime);
        rtpTime = pKvsRtpTransceiver->sender.rtpTimeOffset +
            CONVERT_TIMESTAMP_TO_RTP(pKvsRtpTransceiver->pJitterBuffer->clockRate, currentTime - pKvsRtpTransceiver->sender.firstFrameWallClockTime);
        MUTEX_LOCK(pKvsRtpTransceiver->statsLock);
        packetCount = pKvsRtpTransceiver->outboundStats.sent.packetsSent;
        octetCount = pKvsRtpTransceiver->outboundStats.sent.bytesSent;
        MUTEX_UNLOCK(pKvsRtpTransceiver->statsLock);
        DLOGV("sender report %u %" PRIu64 " %" PRIu64 " : %u packets %u bytes", ssrc, ntpTime, rtpTime, packetCount, octetCount);
        packetLen = RTCP_PACKET_HEADER_LEN + 24;

        // srtp_protect_rtcp() in srtp_session_encryptRtcpPacket() assumes memory availability to write 10 bytes of authentication tag and
        // SRTP_MAX_TRAILER_LEN + 4 following the actual rtcp Packet payload
        allocSize = packetLen + SRTP_AUTH_TAG_OVERHEAD + SRTP_MAX_TRAILER_LEN + 4;
        CHK(NULL != (rawPacket = (PBYTE) MEMALLOC(allocSize)), STATUS_PEER_CONN_NOT_ENOUGH_MEMORY);
        rawPacket[0] = RTCP_PACKET_VERSION_VAL << 6;
        rawPacket[RTCP_PACKET_TYPE_OFFSET] = RTCP_PACKET_TYPE_SENDER_REPORT;
        putUnalignedInt16BigEndian(rawPacket + RTCP_PACKET_LEN_OFFSET,
                                   (packetLen / RTCP_PACKET_LEN_WORD_SIZE) - 1); // The length of this RTCP packet in 32-bit words minus one
        putUnalignedInt32BigEndian(rawPacket + 4, ssrc);
        putUnalignedInt64BigEndian(rawPacket + 8, ntpTime);
        putUnalignedInt32BigEndian(rawPacket + 16, rtpTime);
        putUnalignedInt32BigEndian(rawPacket + 20, packetCount);
        putUnalignedInt32BigEndian(rawPacket + 24, octetCount);
        CHK_STATUS(srtp_session_encryptRtcpPacket(pKvsPeerConnection->pSrtpSession, rawPacket, (PINT32) &packetLen));
        CHK_STATUS(ice_agent_send(pKvsPeerConnection->pIceAgent, rawPacket, packetLen));
    }

    delay = 100 + (RAND() % 200);
    DLOGS("next sender report %u in %" PRIu64 " msec", ssrc, delay);
    // reschedule timer with 200msec +- 100ms
    CHK_STATUS(timer_queue_addTimer(pKvsPeerConnection->timerQueueHandle, delay * HUNDREDS_OF_NANOS_IN_A_MILLISECOND,
                                    TIMER_QUEUE_SINGLE_INVOCATION_PERIOD, peer_connection_rtcpReportsCallback, (UINT64) pKvsRtpTransceiver,
                                    &pKvsRtpTransceiver->rtcpReportsTimerId));

CleanUp:
    CHK_LOG_ERR(retStatus);
    SAFE_MEMFREE(rawPacket);
    return retStatus;
}
#endif

STATUS peer_connection_create(PRtcConfiguration pConfiguration, PRtcPeerConnection* ppPeerConnection)
{
    PC_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = NULL;
    IceAgentCallbacks iceAgentCallbacks;
    DtlsSessionCallbacks dtlsSessionCallbacks;
    PConnectionListener pConnectionListener = NULL;

    CHK(pConfiguration != NULL && ppPeerConnection != NULL, STATUS_PEER_CONN_NULL_ARG);

    MEMSET(&iceAgentCallbacks, 0, SIZEOF(IceAgentCallbacks));
    MEMSET(&dtlsSessionCallbacks, 0, SIZEOF(DtlsSessionCallbacks));

    pKvsPeerConnection = (PKvsPeerConnection) MEMCALLOC(1, SIZEOF(KvsPeerConnection));
    CHK(pKvsPeerConnection != NULL, STATUS_PEER_CONN_NOT_ENOUGH_MEMORY);

    CHK_STATUS(timerQueueCreateEx(&pKvsPeerConnection->timerQueueHandle, PEER_TIMER_NAME, PEER_TIMER_SIZE));

    pKvsPeerConnection->peerConnection.version = PEER_CONNECTION_CURRENT_VERSION;
    CHK_STATUS(json_generateSafeString(pKvsPeerConnection->localIceUfrag, LOCAL_ICE_UFRAG_LEN));
    CHK_STATUS(json_generateSafeString(pKvsPeerConnection->localIcePwd, LOCAL_ICE_PWD_LEN));
    CHK_STATUS(json_generateSafeString(pKvsPeerConnection->localCNAME, LOCAL_CNAME_LEN));

    CHK_STATUS(dtls_session_create(
        &dtlsSessionCallbacks, pKvsPeerConnection->timerQueueHandle, pConfiguration->kvsRtcConfiguration.generatedCertificateBits,
        pConfiguration->kvsRtcConfiguration.generateRSACertificate, pConfiguration->certificates, &pKvsPeerConnection->pDtlsSession));
    CHK_STATUS(dtls_session_onOutBoundData(pKvsPeerConnection->pDtlsSession, (UINT64) pKvsPeerConnection, onDtlsOutboundPacket));
    CHK_STATUS(dtls_session_onStateChange(pKvsPeerConnection->pDtlsSession, (UINT64) pKvsPeerConnection, onDtlsStateChange));
    // #codec.
    CHK_STATUS(hashTableCreateWithParams(CODEC_HASH_TABLE_BUCKET_COUNT, CODEC_HASH_TABLE_BUCKET_LENGTH, &pKvsPeerConnection->pCodecTable));
    // #datachannel
    CHK_STATUS(hashTableCreateWithParams(CODEC_HASH_TABLE_BUCKET_COUNT, CODEC_HASH_TABLE_BUCKET_LENGTH, &pKvsPeerConnection->pDataChannels));
    CHK_STATUS(hashTableCreateWithParams(RTX_HASH_TABLE_BUCKET_COUNT, RTX_HASH_TABLE_BUCKET_LENGTH, &pKvsPeerConnection->pRtxTable));
    CHK_STATUS(doubleListCreate(&(pKvsPeerConnection->pTransceivers)));
#ifdef ENABLE_STREAMING
    pKvsPeerConnection->pSrtpSessionLock = MUTEX_CREATE(TRUE);
#endif
    pKvsPeerConnection->peerConnectionObjLock = MUTEX_CREATE(FALSE);
    pKvsPeerConnection->connectionState = RTC_PEER_CONNECTION_STATE_NONE;
    pKvsPeerConnection->MTU = pConfiguration->kvsRtcConfiguration.maximumTransmissionUnit == 0
        ? DEFAULT_MTU_SIZE
        : pConfiguration->kvsRtcConfiguration.maximumTransmissionUnit;
    pKvsPeerConnection->sctpIsEnabled = FALSE;

    iceAgentCallbacks.customData = (UINT64) pKvsPeerConnection;
    iceAgentCallbacks.inboundPacketFn = peer_connection_onInboundPacket;
    iceAgentCallbacks.connectionStateChangedFn = onIceConnectionStateChange;
    iceAgentCallbacks.newLocalCandidateFn = onNewIceLocalCandidate;
    CHK_STATUS(connection_listener_create(&pConnectionListener));
    // IceAgent will own the lifecycle of pConnectionListener;
    CHK_STATUS(ice_agent_create(pKvsPeerConnection->localIceUfrag, pKvsPeerConnection->localIcePwd, &iceAgentCallbacks, pConfiguration,
                                pKvsPeerConnection->timerQueueHandle, pConnectionListener, &pKvsPeerConnection->pIceAgent));

    NULLABLE_SET_EMPTY(pKvsPeerConnection->canTrickleIce);

    *ppPeerConnection = (PRtcPeerConnection) pKvsPeerConnection;

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (STATUS_FAILED(retStatus)) {
        peer_connection_free((PRtcPeerConnection*) &pKvsPeerConnection);
    }

    PC_LEAVES();
    return retStatus;
}

STATUS freeHashEntry(UINT64 customData, PHashEntry pHashEntry)
{
    UNUSED_PARAM(customData);
    MEMFREE((PVOID) pHashEntry->value);
    return STATUS_SUCCESS;
}

STATUS peer_connection_free(PRtcPeerConnection* ppPeerConnection)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection;
    PDoubleListNode pCurNode = NULL;
    UINT64 item = 0;
    CHK(ppPeerConnection != NULL, STATUS_PEER_CONN_NULL_ARG);

    pKvsPeerConnection = (PKvsPeerConnection) *ppPeerConnection;

    CHK(pKvsPeerConnection != NULL, retStatus);

    /* Shutdown IceAgent first so there is no more incoming packets which can cause
     * SCTP to be allocated again after SCTP is freed. */
    CHK_LOG_ERR(ice_agent_shutdown(pKvsPeerConnection->pIceAgent));

    // free timer queue first to remove liveness provided by timer
    if (IS_VALID_TIMER_QUEUE_HANDLE(pKvsPeerConnection->timerQueueHandle)) {
        timer_queue_shutdown(pKvsPeerConnection->timerQueueHandle);
    }

/* Free structs that have their own thread. SCTP has threads created by SCTP library. IceAgent has the
 * connectionListener thread. Free SCTP first so it wont try to send anything through ICE. */
#ifdef ENABLE_DATA_CHANNEL
    CHK_LOG_ERR(sctp_session_free(&pKvsPeerConnection->pSctpSession));
#endif
    CHK_LOG_ERR(ice_agent_free(&pKvsPeerConnection->pIceAgent));

#ifdef ENABLE_STREAMING
    // free transceivers
    CHK_LOG_ERR(doubleListGetHeadNode(pKvsPeerConnection->pTransceivers, &pCurNode));
    while (pCurNode != NULL) {
        CHK_LOG_ERR(doubleListGetNodeData(pCurNode, &item));
        CHK_LOG_ERR(rtp_transceiver_free((PKvsRtpTransceiver*) &item));

        pCurNode = pCurNode->pNext;
    }
#endif

#ifdef ENABLE_DATA_CHANNEL
    // Free DataChannels
    CHK_LOG_ERR(hashTableIterateEntries(pKvsPeerConnection->pDataChannels, 0, freeHashEntry));
    CHK_LOG_ERR(hashTableFree(pKvsPeerConnection->pDataChannels));
#endif

// free rest of structs
#ifdef ENABLE_STREAMING
    CHK_LOG_ERR(srtp_session_free(&pKvsPeerConnection->pSrtpSession));
#endif

    CHK_LOG_ERR(dtls_session_free(&pKvsPeerConnection->pDtlsSession));

#ifdef ENABLE_STREAMING
    CHK_LOG_ERR(doubleListFree(pKvsPeerConnection->pTransceivers));
    CHK_LOG_ERR(hashTableFree(pKvsPeerConnection->pCodecTable));
    CHK_LOG_ERR(hashTableFree(pKvsPeerConnection->pRtxTable));
    if (IS_VALID_MUTEX_VALUE(pKvsPeerConnection->pSrtpSessionLock)) {
        MUTEX_FREE(pKvsPeerConnection->pSrtpSessionLock);
    }
#endif

    if (IS_VALID_MUTEX_VALUE(pKvsPeerConnection->peerConnectionObjLock)) {
        MUTEX_FREE(pKvsPeerConnection->peerConnectionObjLock);
    }

    if (IS_VALID_TIMER_QUEUE_HANDLE(pKvsPeerConnection->timerQueueHandle)) {
        timer_queue_free(&pKvsPeerConnection->timerQueueHandle);
    }

    SAFE_MEMFREE(pKvsPeerConnection);

    *ppPeerConnection = NULL;

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS peer_connection_onIceCandidate(PRtcPeerConnection pRtcPeerConnection, UINT64 customData, RtcOnIceCandidate rtcOnIceCandidate)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pRtcPeerConnection;
    BOOL locked = FALSE;

    CHK(pKvsPeerConnection != NULL && rtcOnIceCandidate != NULL, STATUS_PEER_CONN_NULL_ARG);

    MUTEX_LOCK(pKvsPeerConnection->peerConnectionObjLock);
    locked = TRUE;

    pKvsPeerConnection->onIceCandidate = rtcOnIceCandidate;
    pKvsPeerConnection->onIceCandidateCustomData = customData;

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pKvsPeerConnection->peerConnectionObjLock);
    }

    LEAVES();
    return retStatus;
}
#ifdef ENABLE_DATA_CHANNEL
STATUS peer_connection_onDataChannel(PRtcPeerConnection pRtcPeerConnection, UINT64 customData, RtcOnDataChannel rtcOnDataChannel)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pRtcPeerConnection;
    BOOL locked = FALSE;

    CHK(pKvsPeerConnection != NULL && rtcOnDataChannel != NULL, STATUS_PEER_CONN_NULL_ARG);

    MUTEX_LOCK(pKvsPeerConnection->peerConnectionObjLock);
    locked = TRUE;

    pKvsPeerConnection->onDataChannel = rtcOnDataChannel;
    pKvsPeerConnection->onDataChannelCustomData = customData;

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pKvsPeerConnection->peerConnectionObjLock);
    }

    LEAVES();
    return retStatus;
}
#endif

STATUS peer_connection_onConnectionStateChange(PRtcPeerConnection pRtcPeerConnection, UINT64 customData,
                                               RtcOnConnectionStateChange rtcOnConnectionStateChange)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pRtcPeerConnection;
    BOOL locked = FALSE;

    CHK(pKvsPeerConnection != NULL && rtcOnConnectionStateChange != NULL, STATUS_PEER_CONN_NULL_ARG);

    MUTEX_LOCK(pKvsPeerConnection->peerConnectionObjLock);
    locked = TRUE;

    pKvsPeerConnection->onConnectionStateChange = rtcOnConnectionStateChange;
    pKvsPeerConnection->onConnectionStateChangeCustomData = customData;

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pKvsPeerConnection->peerConnectionObjLock);
    }

    LEAVES();
    return retStatus;
}

STATUS peerConnectionGetLocalDescription(PRtcPeerConnection pRtcPeerConnection, PRtcSessionDescriptionInit pRtcSessionDescriptionInit)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSessionDescription pSessionDescription = NULL;
    UINT32 serializeLen = 0;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pRtcPeerConnection;

    CHK(pRtcPeerConnection != NULL && pRtcSessionDescriptionInit != NULL, STATUS_PEER_CONN_NULL_ARG);

    CHK(NULL != (pSessionDescription = (PSessionDescription) MEMCALLOC(1, SIZEOF(SessionDescription))), STATUS_PEER_CONN_NOT_ENOUGH_MEMORY);

    if (pKvsPeerConnection->isOffer) {
        pRtcSessionDescriptionInit->type = SDP_TYPE_OFFER;
    } else {
        pRtcSessionDescriptionInit->type = SDP_TYPE_ANSWER;
    }

    CHK_STATUS(populateSessionDescription(pKvsPeerConnection, &(pKvsPeerConnection->remoteSessionDescription), pSessionDescription));
    CHK_STATUS(serializeSessionDescription(pSessionDescription, NULL, &serializeLen));
    CHK(serializeLen <= MAX_SESSION_DESCRIPTION_INIT_SDP_LEN, STATUS_PEER_CONN_NOT_ENOUGH_MEMORY);

    CHK_STATUS(serializeSessionDescription(pSessionDescription, pRtcSessionDescriptionInit->sdp, &serializeLen));

CleanUp:

    SAFE_MEMFREE(pSessionDescription);

    LEAVES();
    return retStatus;
}

STATUS peerConnectionGetCurrentLocalDescription(PRtcPeerConnection pRtcPeerConnection, PRtcSessionDescriptionInit pRtcSessionDescriptionInit)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSessionDescription pSessionDescription = NULL;
    UINT32 serializeLen = 0;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pRtcPeerConnection;

    CHK(pRtcPeerConnection != NULL && pRtcSessionDescriptionInit != NULL, STATUS_PEER_CONN_NULL_ARG);
    // do nothing if remote session description hasn't been received
    CHK(pKvsPeerConnection->remoteSessionDescription.sessionName[0] != '\0', retStatus);

    CHK(NULL != (pSessionDescription = (PSessionDescription) MEMCALLOC(1, SIZEOF(SessionDescription))), STATUS_PEER_CONN_NOT_ENOUGH_MEMORY);

    CHK_STATUS(populateSessionDescription(pKvsPeerConnection, &(pKvsPeerConnection->remoteSessionDescription), pSessionDescription));

    CHK_STATUS(serializeSessionDescription(pSessionDescription, NULL, &serializeLen));
    CHK(serializeLen <= MAX_SESSION_DESCRIPTION_INIT_SDP_LEN, STATUS_PEER_CONN_NOT_ENOUGH_MEMORY);

    CHK_STATUS(serializeSessionDescription(pSessionDescription, pRtcSessionDescriptionInit->sdp, &serializeLen));

CleanUp:

    SAFE_MEMFREE(pSessionDescription);

    LEAVES();
    return retStatus;
}

STATUS peer_connection_setRemoteDescription(PRtcPeerConnection pPeerConnection, PRtcSessionDescriptionInit pSessionDescriptionInit)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PCHAR remoteIceUfrag = NULL, remoteIcePwd = NULL;
    UINT32 i, j;

    CHK(pPeerConnection != NULL, STATUS_PEER_CONN_NULL_ARG);
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pPeerConnection;
    PSessionDescription pSessionDescription = &pKvsPeerConnection->remoteSessionDescription;

    CHK(pSessionDescriptionInit != NULL, STATUS_PEER_CONN_NULL_ARG);

    MEMSET(pSessionDescription, 0x00, SIZEOF(SessionDescription));
    pKvsPeerConnection->dtlsIsServer = FALSE;
    /* Assume cant trickle at first */
    NULLABLE_SET_VALUE(pKvsPeerConnection->canTrickleIce, FALSE);

    CHK_STATUS(deserializeSessionDescription(pSessionDescription, pSessionDescriptionInit->sdp));

    for (i = 0; i < pSessionDescription->sessionAttributesCount; i++) {
        if (STRCMP(pSessionDescription->sdpAttributes[i].attributeName, "fingerprint") == 0) {
            STRNCPY(pKvsPeerConnection->remoteCertificateFingerprint, pSessionDescription->sdpAttributes[i].attributeValue + 8,
                    CERTIFICATE_FINGERPRINT_LENGTH);
        } else if (pKvsPeerConnection->isOffer && STRCMP(pSessionDescription->sdpAttributes[i].attributeName, "setup") == 0) {
            pKvsPeerConnection->dtlsIsServer = STRCMP(pSessionDescription->sdpAttributes[i].attributeValue, "active") == 0;
        } else if (STRCMP(pSessionDescription->sdpAttributes[i].attributeName, "ice-options") == 0 &&
                   STRSTR(pSessionDescription->sdpAttributes[i].attributeValue, "trickle") != NULL) {
            NULLABLE_SET_VALUE(pKvsPeerConnection->canTrickleIce, TRUE);
        }
    }

    for (i = 0; i < pSessionDescription->mediaCount; i++) {
#ifdef ENABLE_DATA_CHANNEL
        if (STRNCMP(pSessionDescription->mediaDescriptions[i].mediaName, "application", SIZEOF("application") - 1) == 0) {
            pKvsPeerConnection->sctpIsEnabled = TRUE;
        }
#endif

        for (j = 0; j < pSessionDescription->mediaDescriptions[i].mediaAttributesCount; j++) {
            if (STRCMP(pSessionDescription->mediaDescriptions[i].sdpAttributes[j].attributeName, "ice-ufrag") == 0) {
                remoteIceUfrag = pSessionDescription->mediaDescriptions[i].sdpAttributes[j].attributeValue;
            } else if (STRCMP(pSessionDescription->mediaDescriptions[i].sdpAttributes[j].attributeName, "ice-pwd") == 0) {
                remoteIcePwd = pSessionDescription->mediaDescriptions[i].sdpAttributes[j].attributeValue;
            } else if (STRCMP(pSessionDescription->mediaDescriptions[i].sdpAttributes[j].attributeName, "candidate") == 0) {
                // Ignore the return value, we have candidates we don't support yet like TURN
                ice_agent_addRemoteCandidate(pKvsPeerConnection->pIceAgent,
                                             pSessionDescription->mediaDescriptions[i].sdpAttributes[j].attributeValue);
            } else if (STRCMP(pSessionDescription->mediaDescriptions[i].sdpAttributes[j].attributeName, "fingerprint") == 0) {
                STRNCPY(pKvsPeerConnection->remoteCertificateFingerprint,
                        pSessionDescription->mediaDescriptions[i].sdpAttributes[j].attributeValue + 8, CERTIFICATE_FINGERPRINT_LENGTH);
            } else if (pKvsPeerConnection->isOffer &&
                       STRCMP(pSessionDescription->mediaDescriptions[i].sdpAttributes[j].attributeName, "setup") == 0) {
                pKvsPeerConnection->dtlsIsServer = STRCMP(pSessionDescription->mediaDescriptions[i].sdpAttributes[j].attributeValue, "active") == 0;
            } else if (STRCMP(pSessionDescription->mediaDescriptions[i].sdpAttributes[j].attributeName, "ice-options") == 0 &&
                       STRSTR(pSessionDescription->mediaDescriptions[i].sdpAttributes[j].attributeValue, "trickle") != NULL) {
                NULLABLE_SET_VALUE(pKvsPeerConnection->canTrickleIce, TRUE);
                // This code is only here because Chrome does NOT adhere to the standard and adds ice-options as a media level attribute
                // The standard dictates clearly that it should be a session level attribute:  https://tools.ietf.org/html/rfc5245#page-76
            }
        }
    }

    CHK(remoteIceUfrag != NULL && remoteIcePwd != NULL, STATUS_SDP_MISSING_ICE_VALUES);
    CHK(pKvsPeerConnection->remoteCertificateFingerprint[0] != '\0', STATUS_SDP_MISSING_CERTIFICATE_FINGERPRINT);

    if (!IS_EMPTY_STRING(pKvsPeerConnection->remoteIceUfrag) && !IS_EMPTY_STRING(pKvsPeerConnection->remoteIcePwd) &&
        STRNCMP(pKvsPeerConnection->remoteIceUfrag, remoteIceUfrag, MAX_ICE_UFRAG_LEN) != 0 &&
        STRNCMP(pKvsPeerConnection->remoteIcePwd, remoteIcePwd, MAX_ICE_PWD_LEN) != 0) {
        CHK_STATUS(json_generateSafeString(pKvsPeerConnection->localIceUfrag, LOCAL_ICE_UFRAG_LEN));
        CHK_STATUS(json_generateSafeString(pKvsPeerConnection->localIcePwd, LOCAL_ICE_PWD_LEN));
        // setup the ice agent.
        CHK_STATUS(ice_agent_restart(pKvsPeerConnection->pIceAgent, pKvsPeerConnection->localIceUfrag, pKvsPeerConnection->localIcePwd));
        CHK_STATUS(ice_agent_gather(pKvsPeerConnection->pIceAgent));
    }

    STRNCPY(pKvsPeerConnection->remoteIceUfrag, remoteIceUfrag, MAX_ICE_UFRAG_LEN);
    STRNCPY(pKvsPeerConnection->remoteIcePwd, remoteIcePwd, MAX_ICE_PWD_LEN);

    CHK_STATUS(ice_agent_start(pKvsPeerConnection->pIceAgent, pKvsPeerConnection->remoteIceUfrag, pKvsPeerConnection->remoteIcePwd,
                               pKvsPeerConnection->isOffer));
#ifdef ENABLE_STREAMING
    if (!pKvsPeerConnection->isOffer) {
        CHK_STATUS(setPayloadTypesFromOffer(pKvsPeerConnection->pCodecTable, pKvsPeerConnection->pRtxTable, pSessionDescription));
    }
    CHK_STATUS(setTransceiverPayloadTypes(pKvsPeerConnection->pCodecTable, pKvsPeerConnection->pRtxTable, pKvsPeerConnection->pTransceivers));
    CHK_STATUS(setReceiversSsrc(pSessionDescription, pKvsPeerConnection->pTransceivers));
#endif
#ifdef KVSWEBRTC_HAVE_GETENV
    if (NULL != GETENV(DEBUG_LOG_SDP)) {
        DLOGD("REMOTE_SDP:%s\n", pSessionDescriptionInit->sdp);
    }
#endif
CleanUp:
    CHK_LOG_ERR(retStatus);
    LEAVES();
    return retStatus;
}

STATUS createOffer(PRtcPeerConnection pPeerConnection, PRtcSessionDescriptionInit pSessionDescriptionInit)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSessionDescription pSessionDescription = NULL;
    UINT32 serializeLen = 0;

    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pPeerConnection;

    CHK(pKvsPeerConnection != NULL && pSessionDescriptionInit != NULL, STATUS_PEER_CONN_NULL_ARG);

    // SessionDescription is large enough structure to not define on the stack and use heap memory
    CHK(NULL != (pSessionDescription = (PSessionDescription) MEMCALLOC(1, SIZEOF(SessionDescription))), STATUS_PEER_CONN_NOT_ENOUGH_MEMORY);
    pSessionDescriptionInit->type = SDP_TYPE_OFFER;
    pKvsPeerConnection->isOffer = TRUE;

#ifdef ENABLE_DATA_CHANNEL
    pKvsPeerConnection->sctpIsEnabled = TRUE;
#endif
#ifdef ENABLE_STREAMING
    CHK_STATUS(setPayloadTypesForOffer(pKvsPeerConnection->pCodecTable));
#endif

    CHK_STATUS(populateSessionDescription(pKvsPeerConnection, &(pKvsPeerConnection->remoteSessionDescription), pSessionDescription));
    CHK_STATUS(serializeSessionDescription(pSessionDescription, NULL, &serializeLen));
    CHK(serializeLen <= MAX_SESSION_DESCRIPTION_INIT_SDP_LEN, STATUS_PEER_CONN_NOT_ENOUGH_MEMORY);

    CHK_STATUS(serializeSessionDescription(pSessionDescription, pSessionDescriptionInit->sdp, &serializeLen));

CleanUp:

    SAFE_MEMFREE(pSessionDescription);

    LEAVES();
    return retStatus;
}

STATUS peer_connection_createAnswer(PRtcPeerConnection pPeerConnection, PRtcSessionDescriptionInit pSessionDescriptionInit)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pPeerConnection;

    CHK(pKvsPeerConnection != NULL && pSessionDescriptionInit != NULL, STATUS_PEER_CONN_NULL_ARG);
    CHK(pKvsPeerConnection->remoteSessionDescription.sessionName[0] != '\0', STATUS_PEER_CONN_CREATE_ANSWER_WITHOUT_REMOTE_DESCRIPTION);

    pSessionDescriptionInit->type = SDP_TYPE_ANSWER;

    CHK_STATUS(peerConnectionGetCurrentLocalDescription(pPeerConnection, pSessionDescriptionInit));

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS peer_connection_setLocalDescription(PRtcPeerConnection pPeerConnection, PRtcSessionDescriptionInit pSessionDescriptionInit)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pPeerConnection;

    CHK(pKvsPeerConnection != NULL && pSessionDescriptionInit != NULL, STATUS_PEER_CONN_NULL_ARG);

    CHK_STATUS(ice_agent_gather(pKvsPeerConnection->pIceAgent));
#ifdef KVSWEBRTC_HAVE_GETENV
    if (NULL != GETENV(DEBUG_LOG_SDP)) {
        DLOGD("LOCAL_SDP:%s", pSessionDescriptionInit->sdp);
    }
#endif
CleanUp:

    LEAVES();
    return retStatus;
}
//#TBD
//#ifdef ENABLE_STREAMING
#if 1
STATUS peer_connection_addTransceiver(PRtcPeerConnection pPeerConnection, PRtcMediaStreamTrack pRtcMediaStreamTrack,
                                      PRtcRtpTransceiverInit pRtcRtpTransceiverInit, PRtcRtpTransceiver* ppRtcRtpTransceiver)
{
    UNUSED_PARAM(pRtcRtpTransceiverInit);
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsRtpTransceiver pKvsRtpTransceiver = NULL;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pPeerConnection;
    PJitterBuffer pJitterBuffer = NULL;
    DepayRtpPayloadFunc depayFunc;
    UINT32 clockRate = 0;
    UINT32 ssrc = (UINT32) RAND(), rtxSsrc = (UINT32) RAND();
    RTC_RTP_TRANSCEIVER_DIRECTION direction = RTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV;

    if (pRtcRtpTransceiverInit != NULL) {
        direction = pRtcRtpTransceiverInit->direction;
    }

    CHK(pKvsPeerConnection != NULL, STATUS_PEER_CONN_NULL_ARG);

    switch (pRtcMediaStreamTrack->codec) {
        case RTC_CODEC_OPUS:
            depayFunc = depayOpusFromRtpPayload;
            clockRate = OPUS_CLOCKRATE;
            break;

        case RTC_CODEC_MULAW:
        case RTC_CODEC_ALAW:
            depayFunc = depayG711FromRtpPayload;
            clockRate = PCM_CLOCKRATE;
            break;

        case RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE:
            depayFunc = depayH264FromRtpPayload;
            clockRate = VIDEO_CLOCKRATE;
            break;

        case RTC_CODEC_VP8:
            depayFunc = depayVP8FromRtpPayload;
            clockRate = VIDEO_CLOCKRATE;
            break;

        default:
            CHK(FALSE, STATUS_NOT_IMPLEMENTED);
    }

    // TODO: Add ssrc duplicate detection here not only relying on RAND()
    CHK_STATUS(rtp_createTransceiver(direction, pKvsPeerConnection, ssrc, rtxSsrc, pRtcMediaStreamTrack, NULL, pRtcMediaStreamTrack->codec,
                                     &pKvsRtpTransceiver));
    CHK_STATUS(jitter_buffer_create(peer_connection_onFrameReadyFunc, peer_connection_onFrameDroppedFunc, depayFunc,
                                    DEFAULT_JITTER_BUFFER_MAX_LATENCY, clockRate, (UINT64) pKvsRtpTransceiver, &pJitterBuffer));
    CHK_STATUS(rtp_transceiver_setJitterBuffer(pKvsRtpTransceiver, pJitterBuffer));

    // after pKvsRtpTransceiver is successfully created, jitterBuffer will be freed by pKvsRtpTransceiver.
    pJitterBuffer = NULL;

    CHK_STATUS(doubleListInsertItemHead(pKvsPeerConnection->pTransceivers, (UINT64) pKvsRtpTransceiver));
    *ppRtcRtpTransceiver = (PRtcRtpTransceiver) pKvsRtpTransceiver;

    CHK_STATUS(timer_queue_addTimer(pKvsPeerConnection->timerQueueHandle, RTCP_FIRST_REPORT_DELAY, TIMER_QUEUE_SINGLE_INVOCATION_PERIOD,
                                    peer_connection_rtcpReportsCallback, (UINT64) pKvsRtpTransceiver, &pKvsRtpTransceiver->rtcpReportsTimerId));

    pKvsRtpTransceiver = NULL;

CleanUp:

    if (pJitterBuffer != NULL) {
        jitter_buffer_free(&pJitterBuffer);
    }

    if (pKvsRtpTransceiver != NULL) {
        rtp_transceiver_free(&pKvsRtpTransceiver);
    }

    LEAVES();
    return retStatus;
}
/**
 * @brief Adds to the list of codecs we support receiving.
 *
 * NOTE: The remote MUST only send codecs we declare
 *
 * @param[in] PRtcPeerConnection Initialized RtcPeerConnection
 * @param[in] RTC_CODEC Codec that we support receiving.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success
 */
STATUS peer_connection_addSupportedCodec(PRtcPeerConnection pPeerConnection, RTC_CODEC rtcCodec)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pPeerConnection;

    CHK(pKvsPeerConnection != NULL, STATUS_PEER_CONN_NULL_ARG);

    CHK_STATUS(hashTablePut(pKvsPeerConnection->pCodecTable, rtcCodec, 0));

CleanUp:

    LEAVES();
    return retStatus;
}
#endif
STATUS peer_connection_addIceCandidate(PRtcPeerConnection pPeerConnection, PCHAR pIceCandidate)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pPeerConnection;

    CHK(pKvsPeerConnection != NULL && pIceCandidate != NULL, STATUS_PEER_CONN_NULL_ARG);

    ice_agent_addRemoteCandidate(pKvsPeerConnection->pIceAgent, pIceCandidate);

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS peer_connection_restartIce(PRtcPeerConnection pPeerConnection)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pPeerConnection;

    CHK(pKvsPeerConnection != NULL, STATUS_PEER_CONN_NULL_ARG);

    /* generate new local uFrag and uPwd and clear out remote uFrag and uPwd */
    CHK_STATUS(json_generateSafeString(pKvsPeerConnection->localIceUfrag, LOCAL_ICE_UFRAG_LEN));
    CHK_STATUS(json_generateSafeString(pKvsPeerConnection->localIcePwd, LOCAL_ICE_PWD_LEN));
    pKvsPeerConnection->remoteIceUfrag[0] = '\0';
    pKvsPeerConnection->remoteIcePwd[0] = '\0';
    CHK_STATUS(ice_agent_restart(pKvsPeerConnection->pIceAgent, pKvsPeerConnection->localIceUfrag, pKvsPeerConnection->localIcePwd));

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS peer_connection_close(PRtcPeerConnection pPeerConnection)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pPeerConnection;
    CHK(pKvsPeerConnection != NULL, STATUS_PEER_CONN_NULL_ARG);
    CHK_LOG_ERR(dtls_session_shutdown(pKvsPeerConnection->pDtlsSession));
    CHK_LOG_ERR(ice_agent_shutdown(pKvsPeerConnection->pIceAgent));

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

PUBLIC_API NullableBool canTrickleIceCandidates(PRtcPeerConnection pPeerConnection)
{
    NullableBool canTrickle = {FALSE, FALSE};
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pPeerConnection;
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pKvsPeerConnection != NULL, STATUS_PEER_CONN_NULL_ARG);
    if (pKvsPeerConnection != NULL) {
        canTrickle = pKvsPeerConnection->canTrickleIce;
    }

CleanUp:

    CHK_LOG_ERR(retStatus);
    return canTrickle;
}

STATUS initKvsWebRtc(VOID)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    CHK(!ATOMIC_LOAD_BOOL(&gKvsWebRtcInitialized), retStatus);

    SRAND(GETTIME());

#ifdef ENABLE_STREAMING
    //#TBD.
    CHK(srtp_init() == srtp_err_status_ok, STATUS_SRTP_INIT_FAILED);
#endif

    // init endianness handling
    initializeEndianness();

    KVS_CRYPTO_INIT();

#ifdef ENABLE_DATA_CHANNEL
    CHK_STATUS(sctp_session_init());
#endif

    ATOMIC_STORE_BOOL(&gKvsWebRtcInitialized, TRUE);

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS deinitKvsWebRtc(VOID)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    CHK(ATOMIC_LOAD_BOOL(&gKvsWebRtcInitialized), retStatus);

#ifdef ENABLE_DATA_CHANNEL
    sctp_session_deinit();
#endif
#ifdef ENABLE_STREAMING
    //#TBD.
    srtp_shutdown();
#endif

    ATOMIC_STORE_BOOL(&gKvsWebRtcInitialized, FALSE);

CleanUp:

    LEAVES();
    return retStatus;
}
