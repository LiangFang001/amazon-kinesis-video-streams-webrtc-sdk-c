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
//#TBD
//#ifdef ENABLE_STREAMING
#if 1
#define LOG_CLASS "RtcRtp"

#include "SessionDescription.h"
#include "Rtp.h"
#include "RtpVP8Payloader.h"
#include "RtpH264Payloader.h"
#include "RtpOpusPayloader.h"
#include "RtpG711Payloader.h"
#include "time_port.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
typedef STATUS (*RtpPayloadFunc)(UINT32, PBYTE, UINT32, PBYTE, PUINT32, PUINT32, PUINT32);
/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS rtp_createTransceiver(RTC_RTP_TRANSCEIVER_DIRECTION direction, PKvsPeerConnection pKvsPeerConnection, UINT32 ssrc, UINT32 rtxSsrc,
                             PRtcMediaStreamTrack pRtcMediaStreamTrack, PJitterBuffer pJitterBuffer, RTC_CODEC rtcCodec,
                             PKvsRtpTransceiver* ppKvsRtpTransceiver)
{
    STATUS retStatus = STATUS_SUCCESS;
    PKvsRtpTransceiver pKvsRtpTransceiver = NULL;

    CHK(ppKvsRtpTransceiver != NULL && pKvsPeerConnection != NULL && pRtcMediaStreamTrack != NULL, STATUS_RTP_NULL_ARG);

    pKvsRtpTransceiver = (PKvsRtpTransceiver) MEMCALLOC(1, SIZEOF(KvsRtpTransceiver));
    CHK(pKvsRtpTransceiver != NULL, STATUS_NOT_ENOUGH_MEMORY);

    pKvsRtpTransceiver->peerFrameBufferSize = DEFAULT_PEER_FRAME_BUFFER_SIZE;
    pKvsRtpTransceiver->peerFrameBuffer = (PBYTE) MEMALLOC(pKvsRtpTransceiver->peerFrameBufferSize);
    CHK(pKvsRtpTransceiver->peerFrameBuffer != NULL, STATUS_NOT_ENOUGH_MEMORY);
    pKvsRtpTransceiver->pKvsPeerConnection = pKvsPeerConnection;
    pKvsRtpTransceiver->statsLock = MUTEX_CREATE(FALSE);
    pKvsRtpTransceiver->sender.ssrc = ssrc;
    pKvsRtpTransceiver->sender.rtxSsrc = rtxSsrc;
    pKvsRtpTransceiver->sender.track = *pRtcMediaStreamTrack;
    pKvsRtpTransceiver->sender.packetBuffer = NULL;
    pKvsRtpTransceiver->sender.retransmitter = NULL;
    pKvsRtpTransceiver->pJitterBuffer = pJitterBuffer;
    pKvsRtpTransceiver->transceiver.receiver.track.codec = rtcCodec;
    pKvsRtpTransceiver->transceiver.receiver.track.kind = pRtcMediaStreamTrack->kind;
    pKvsRtpTransceiver->transceiver.direction = direction;

    pKvsRtpTransceiver->outboundStats.sent.rtpStream.ssrc = ssrc;
    STRNCPY(pKvsRtpTransceiver->outboundStats.sent.rtpStream.kind, pRtcMediaStreamTrack->kind == MEDIA_STREAM_TRACK_KIND_AUDIO ? "audio" : "video",
            MAX_STATS_STRING_LENGTH);
    STRNCPY(pKvsRtpTransceiver->outboundStats.trackId, pRtcMediaStreamTrack->trackId, MAX_STATS_STRING_LENGTH);

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        rtp_transceiver_free(&pKvsRtpTransceiver);
    }

    if (ppKvsRtpTransceiver != NULL) {
        *ppKvsRtpTransceiver = pKvsRtpTransceiver;
    }

    return retStatus;
}

STATUS rtp_freeTransceiver(PRtcRtpTransceiver* pRtcRtpTransceiver)
{
    UNUSED_PARAM(pRtcRtpTransceiver);
    return STATUS_NOT_IMPLEMENTED;
}

STATUS rtp_transceiver_free(PKvsRtpTransceiver* ppKvsRtpTransceiver)
{
    STATUS retStatus = STATUS_SUCCESS;
    PKvsRtpTransceiver pKvsRtpTransceiver = NULL;

    CHK(ppKvsRtpTransceiver != NULL, STATUS_RTP_NULL_ARG);
    pKvsRtpTransceiver = *ppKvsRtpTransceiver;
    // free is idempotent
    CHK(pKvsRtpTransceiver != NULL, retStatus);

    if (pKvsRtpTransceiver->pJitterBuffer != NULL) {
        jitter_buffer_free(&pKvsRtpTransceiver->pJitterBuffer);
    }

    if (pKvsRtpTransceiver->sender.packetBuffer != NULL) {
        rtp_rolling_buffer_free(&pKvsRtpTransceiver->sender.packetBuffer);
    }

    if (pKvsRtpTransceiver->sender.retransmitter != NULL) {
        retransmitter_free(&pKvsRtpTransceiver->sender.retransmitter);
    }
    MUTEX_FREE(pKvsRtpTransceiver->statsLock);

    SAFE_MEMFREE(pKvsRtpTransceiver->peerFrameBuffer);
    SAFE_MEMFREE(pKvsRtpTransceiver->sender.payloadArray.payloadBuffer);
    SAFE_MEMFREE(pKvsRtpTransceiver->sender.payloadArray.payloadSubLength);

    SAFE_MEMFREE(pKvsRtpTransceiver);

    *ppKvsRtpTransceiver = NULL;

CleanUp:

    return retStatus;
}

STATUS rtp_transceiver_setJitterBuffer(PKvsRtpTransceiver pKvsRtpTransceiver, PJitterBuffer pJitterBuffer)
{
    STATUS retStatus = STATUS_SUCCESS;
    CHK(pKvsRtpTransceiver != NULL && pJitterBuffer != NULL, STATUS_RTP_NULL_ARG);

    pKvsRtpTransceiver->pJitterBuffer = pJitterBuffer;

CleanUp:

    return retStatus;
}

STATUS rtp_transceiver_onFrame(PRtcRtpTransceiver pRtcRtpTransceiver, UINT64 customData, RtcOnFrame rtcOnFrame)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsRtpTransceiver pKvsRtpTransceiver = (PKvsRtpTransceiver) pRtcRtpTransceiver;

    CHK(pKvsRtpTransceiver != NULL && rtcOnFrame != NULL, STATUS_RTP_NULL_ARG);

    pKvsRtpTransceiver->onFrame = rtcOnFrame;
    pKvsRtpTransceiver->onFrameCustomData = customData;

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS rtp_transceiver_onBandwidthEstimation(PRtcRtpTransceiver pRtcRtpTransceiver, UINT64 customData,
                                             RtcOnBandwidthEstimation rtcOnBandwidthEstimation)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsRtpTransceiver pKvsRtpTransceiver = (PKvsRtpTransceiver) pRtcRtpTransceiver;

    CHK(pKvsRtpTransceiver != NULL && rtcOnBandwidthEstimation != NULL, STATUS_RTP_NULL_ARG);

    pKvsRtpTransceiver->onBandwidthEstimation = rtcOnBandwidthEstimation;
    pKvsRtpTransceiver->onBandwidthEstimationCustomData = customData;

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS rtp_transceiver_onPictureLoss(PRtcRtpTransceiver pRtcRtpTransceiver, UINT64 customData, RtcOnPictureLoss onPictureLoss)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PKvsRtpTransceiver pKvsRtpTransceiver = (PKvsRtpTransceiver) pRtcRtpTransceiver;

    CHK(pKvsRtpTransceiver != NULL && onPictureLoss != NULL, STATUS_RTP_NULL_ARG);

    pKvsRtpTransceiver->onPictureLoss = onPictureLoss;
    pKvsRtpTransceiver->onPictureLossCustomData = customData;

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS rtp_transceiver_updateEncoderStats(PRtcRtpTransceiver pRtcRtpTransceiver, PRtcEncoderStats encoderStats)
{
    STATUS retStatus = STATUS_SUCCESS;
    PKvsRtpTransceiver pKvsRtpTransceiver = (PKvsRtpTransceiver) pRtcRtpTransceiver;
    CHK(pKvsRtpTransceiver != NULL && encoderStats != NULL, STATUS_RTP_NULL_ARG);
    MUTEX_LOCK(pKvsRtpTransceiver->statsLock);
    pKvsRtpTransceiver->outboundStats.totalEncodeTime += encoderStats->encodeTimeMsec;
    pKvsRtpTransceiver->outboundStats.targetBitrate = encoderStats->targetBitrate;
    if (encoderStats->width < pKvsRtpTransceiver->outboundStats.frameWidth || encoderStats->height < pKvsRtpTransceiver->outboundStats.frameHeight) {
        pKvsRtpTransceiver->outboundStats.qualityLimitationResolutionChanges++;
    }

    pKvsRtpTransceiver->outboundStats.frameWidth = encoderStats->width;
    pKvsRtpTransceiver->outboundStats.frameHeight = encoderStats->height;
    pKvsRtpTransceiver->outboundStats.frameBitDepth = encoderStats->bitDepth;
    pKvsRtpTransceiver->outboundStats.voiceActivityFlag = encoderStats->voiceActivity;
    if (encoderStats->encoderImplementation[0] != '\0')
        STRNCPY(pKvsRtpTransceiver->outboundStats.encoderImplementation, encoderStats->encoderImplementation, MAX_STATS_STRING_LENGTH);

    MUTEX_UNLOCK(pKvsRtpTransceiver->statsLock);

CleanUp:
    CHK_LOG_ERR(retStatus);

    return retStatus;
}

STATUS rtp_writeFrame(PRtcRtpTransceiver pRtcRtpTransceiver, PFrame pFrame)
{
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = NULL;
    PKvsRtpTransceiver pKvsRtpTransceiver = (PKvsRtpTransceiver) pRtcRtpTransceiver;
    BOOL locked = FALSE, bufferAfterEncrypt = FALSE;
    PRtpPacket pPacketList = NULL, pRtpPacket = NULL;
    UINT32 i = 0, packetLen = 0, headerLen = 0, allocSize;
    PBYTE rawPacket = NULL;
    PPayloadArray pPayloadArray = NULL;
    RtpPayloadFunc rtpPayloadFunc = NULL;
    UINT64 randomRtpTimeoffset = 0; // TODO: spec requires random rtp time offset
    UINT64 rtpTimestamp = 0;
    UINT64 now = GETTIME();
    // stats updates
    DOUBLE fps = 0.0;
    UINT32 frames = 0, keyframes = 0, bytesSent = 0, packetsSent = 0, headerBytesSent = 0, framesSent = 0;
    UINT32 packetsDiscardedOnSend = 0, bytesDiscardedOnSend = 0, framesDiscardedOnSend = 0;
    UINT64 lastPacketSentTimestamp = 0;
    // temp vars :(
    UINT64 tmpFrames, tmpTime;
    STATUS sendStatus;

    CHK(pKvsRtpTransceiver != NULL && pFrame != NULL, STATUS_RTP_NULL_ARG);

    pKvsPeerConnection = pKvsRtpTransceiver->pKvsPeerConnection;
    pPayloadArray = &(pKvsRtpTransceiver->sender.payloadArray);
    if (MEDIA_STREAM_TRACK_KIND_VIDEO == pKvsRtpTransceiver->sender.track.kind) {
        frames++;
        if (0 != (pFrame->flags & FRAME_FLAG_KEY_FRAME)) {
            keyframes++;
        }
        if (pKvsRtpTransceiver->sender.lastKnownFrameCountTime == 0) {
            pKvsRtpTransceiver->sender.lastKnownFrameCountTime = now;
            pKvsRtpTransceiver->sender.lastKnownFrameCount = pKvsRtpTransceiver->outboundStats.framesEncoded + frames;
        } else if (now - pKvsRtpTransceiver->sender.lastKnownFrameCountTime > HUNDREDS_OF_NANOS_IN_A_SECOND) {
            tmpFrames = (pKvsRtpTransceiver->outboundStats.framesEncoded + frames) - pKvsRtpTransceiver->sender.lastKnownFrameCount;
            tmpTime = now - pKvsRtpTransceiver->sender.lastKnownFrameCountTime;
            fps = (DOUBLE)(tmpFrames * HUNDREDS_OF_NANOS_IN_A_SECOND) / (DOUBLE) tmpTime;
        }
    }

    MUTEX_LOCK(pKvsPeerConnection->pSrtpSessionLock);
    locked = TRUE;
    CHK(pKvsPeerConnection->pSrtpSession != NULL, STATUS_SRTP_NOT_READY_YET); // Discard packets till SRTP is ready
    switch (pKvsRtpTransceiver->sender.track.codec) {
        case RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE:
            rtpPayloadFunc = createPayloadForH264;
            rtpTimestamp = CONVERT_TIMESTAMP_TO_RTP(VIDEO_CLOCKRATE, pFrame->presentationTs);
            break;

        case RTC_CODEC_OPUS:
            rtpPayloadFunc = createPayloadForOpus;
            rtpTimestamp = CONVERT_TIMESTAMP_TO_RTP(OPUS_CLOCKRATE, pFrame->presentationTs);
            break;

        case RTC_CODEC_MULAW:
        case RTC_CODEC_ALAW:
            rtpPayloadFunc = createPayloadForG711;
            rtpTimestamp = CONVERT_TIMESTAMP_TO_RTP(PCM_CLOCKRATE, pFrame->presentationTs);
            break;

        case RTC_CODEC_VP8:
            rtpPayloadFunc = createPayloadForVP8;
            rtpTimestamp = CONVERT_TIMESTAMP_TO_RTP(VIDEO_CLOCKRATE, pFrame->presentationTs);
            break;

        default:
            CHK(FALSE, STATUS_NOT_IMPLEMENTED);
    }

    rtpTimestamp += randomRtpTimeoffset;

    CHK_STATUS(rtpPayloadFunc(pKvsPeerConnection->MTU, (PBYTE) pFrame->frameData, pFrame->size, NULL, &(pPayloadArray->payloadLength), NULL,
                              &(pPayloadArray->payloadSubLenSize)));
    if (pPayloadArray->payloadLength > pPayloadArray->maxPayloadLength) {
        SAFE_MEMFREE(pPayloadArray->payloadBuffer);
        pPayloadArray->payloadBuffer = (PBYTE) MEMALLOC(pPayloadArray->payloadLength);
        pPayloadArray->maxPayloadLength = pPayloadArray->payloadLength;
    }
    if (pPayloadArray->payloadSubLenSize > pPayloadArray->maxPayloadSubLenSize) {
        SAFE_MEMFREE(pPayloadArray->payloadSubLength);
        pPayloadArray->payloadSubLength = (PUINT32) MEMALLOC(pPayloadArray->payloadSubLenSize * SIZEOF(UINT32));
        pPayloadArray->maxPayloadSubLenSize = pPayloadArray->payloadSubLenSize;
    }
    CHK_STATUS(rtpPayloadFunc(pKvsPeerConnection->MTU, (PBYTE) pFrame->frameData, pFrame->size, pPayloadArray->payloadBuffer,
                              &(pPayloadArray->payloadLength), pPayloadArray->payloadSubLength, &(pPayloadArray->payloadSubLenSize)));
    pPacketList = (PRtpPacket) MEMALLOC(pPayloadArray->payloadSubLenSize * SIZEOF(RtpPacket));

    CHK_STATUS(rtp_packet_constructPackets(pPayloadArray, pKvsRtpTransceiver->sender.payloadType, pKvsRtpTransceiver->sender.sequenceNumber,
                                           rtpTimestamp, pKvsRtpTransceiver->sender.ssrc, pPacketList, pPayloadArray->payloadSubLenSize));
    pKvsRtpTransceiver->sender.sequenceNumber = GET_UINT16_SEQ_NUM(pKvsRtpTransceiver->sender.sequenceNumber + pPayloadArray->payloadSubLenSize);

    bufferAfterEncrypt = (pKvsRtpTransceiver->sender.payloadType == pKvsRtpTransceiver->sender.rtxPayloadType);
    for (i = 0; i < pPayloadArray->payloadSubLenSize; i++) {
        pRtpPacket = pPacketList + i;

        // Get the required size first
        CHK_STATUS(rtp_packet_createBytesFromPacket(pRtpPacket, NULL, &packetLen));

        // Account for SRTP authentication tag
        allocSize = packetLen + SRTP_AUTH_TAG_OVERHEAD;
        CHK(NULL != (rawPacket = (PBYTE) MEMALLOC(allocSize)), STATUS_NOT_ENOUGH_MEMORY);
        CHK_STATUS(rtp_packet_createBytesFromPacket(pRtpPacket, rawPacket, &packetLen));

        if (!bufferAfterEncrypt) {
            pRtpPacket->pRawPacket = rawPacket;
            pRtpPacket->rawPacketLength = packetLen;
            CHK_STATUS(rtp_rolling_buffer_addRtpPacket(pKvsRtpTransceiver->sender.packetBuffer, pRtpPacket));
        }

        CHK_STATUS(srtp_session_encryptRtpPacket(pKvsPeerConnection->pSrtpSession, rawPacket, (PINT32) &packetLen));
        sendStatus = ice_agent_send(pKvsPeerConnection->pIceAgent, rawPacket, packetLen);
        if (sendStatus == STATUS_NET_SEND_DATA_FAILED) {
            packetsDiscardedOnSend++;
            bytesDiscardedOnSend += packetLen - headerLen;
            // TODO is frame considered discarded when at least one of its packets is discarded or all of its packets discarded?
            framesDiscardedOnSend = 1;
            SAFE_MEMFREE(rawPacket);
            continue;
        }
        CHK_STATUS(sendStatus);
        if (bufferAfterEncrypt) {
            pRtpPacket->pRawPacket = rawPacket;
            pRtpPacket->rawPacketLength = packetLen;
            CHK_STATUS(rtp_rolling_buffer_addRtpPacket(pKvsRtpTransceiver->sender.packetBuffer, pRtpPacket));
        }

        // https://tools.ietf.org/html/rfc3550#section-6.4.1
        // The total number of payload octets (i.e., not including header or padding) transmitted in RTP data packets by the sender
        headerLen = RTP_HEADER_LEN(pRtpPacket);
        bytesSent += packetLen - headerLen;
        packetsSent++;
        lastPacketSentTimestamp = KVS_CONVERT_TIMESCALE(GETTIME(), HUNDREDS_OF_NANOS_IN_A_SECOND, 1000);
        headerBytesSent += headerLen;

        SAFE_MEMFREE(rawPacket);
    }

    if (MEDIA_STREAM_TRACK_KIND_VIDEO == pKvsRtpTransceiver->sender.track.kind) {
        framesSent++;
    }

    if (pKvsRtpTransceiver->sender.firstFrameWallClockTime == 0) {
        pKvsRtpTransceiver->sender.rtpTimeOffset = randomRtpTimeoffset;
        pKvsRtpTransceiver->sender.firstFrameWallClockTime = now;
    }

CleanUp:
    if (locked) {
        MUTEX_UNLOCK(pKvsPeerConnection->pSrtpSessionLock);
    }
    MUTEX_LOCK(pKvsRtpTransceiver->statsLock);
    pKvsRtpTransceiver->outboundStats.totalEncodedBytesTarget += pFrame->size;
    pKvsRtpTransceiver->outboundStats.framesEncoded += frames;
    pKvsRtpTransceiver->outboundStats.keyFramesEncoded += keyframes;
    if (fps > 0.0) {
        pKvsRtpTransceiver->outboundStats.framesPerSecond = fps;
    }
    pKvsRtpTransceiver->sender.lastKnownFrameCountTime = now;
    pKvsRtpTransceiver->sender.lastKnownFrameCount = pKvsRtpTransceiver->outboundStats.framesEncoded;
    pKvsRtpTransceiver->outboundStats.sent.bytesSent += bytesSent;
    pKvsRtpTransceiver->outboundStats.sent.packetsSent += packetsSent;
    if (lastPacketSentTimestamp > 0) {
        pKvsRtpTransceiver->outboundStats.lastPacketSentTimestamp = lastPacketSentTimestamp;
    }
    pKvsRtpTransceiver->outboundStats.headerBytesSent += headerBytesSent;
    pKvsRtpTransceiver->outboundStats.framesSent += framesSent;
    if (pKvsRtpTransceiver->outboundStats.framesPerSecond > 0.0) {
        if (pFrame->size >=
            pKvsRtpTransceiver->outboundStats.targetBitrate / pKvsRtpTransceiver->outboundStats.framesPerSecond * HUGE_FRAME_MULTIPLIER) {
            pKvsRtpTransceiver->outboundStats.hugeFramesSent++;
        }
    }
    // ice_agent_send tries to send packet immediately, explicitly settings totalPacketSendDelay to 0
    pKvsRtpTransceiver->outboundStats.totalPacketSendDelay = 0;

    pKvsRtpTransceiver->outboundStats.framesDiscardedOnSend += framesDiscardedOnSend;
    pKvsRtpTransceiver->outboundStats.packetsDiscardedOnSend += packetsDiscardedOnSend;
    pKvsRtpTransceiver->outboundStats.bytesDiscardedOnSend += bytesDiscardedOnSend;
    MUTEX_UNLOCK(pKvsRtpTransceiver->statsLock);

    SAFE_MEMFREE(rawPacket);
    SAFE_MEMFREE(pPacketList);
    if (retStatus != STATUS_SRTP_NOT_READY_YET) {
        CHK_LOG_ERR(retStatus);
    }

    return retStatus;
}

STATUS rtp_writePacket(PKvsPeerConnection pKvsPeerConnection, PRtpPacket pRtpPacket)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;
    PBYTE pRawPacket = NULL;
    INT32 rawLen = 0;

    CHK(pKvsPeerConnection != NULL && pRtpPacket != NULL && pRtpPacket->pRawPacket != NULL, STATUS_RTP_NULL_ARG);

    MUTEX_LOCK(pKvsPeerConnection->pSrtpSessionLock);
    locked = TRUE;
    CHK(pKvsPeerConnection->pSrtpSession != NULL, STATUS_SUCCESS);               // Discard packets till SRTP is ready
    pRawPacket = MEMALLOC(pRtpPacket->rawPacketLength + SRTP_AUTH_TAG_OVERHEAD); // For SRTP authentication tag
    rawLen = pRtpPacket->rawPacketLength;
    MEMCPY(pRawPacket, pRtpPacket->pRawPacket, pRtpPacket->rawPacketLength);
    CHK_STATUS(srtp_session_encryptRtpPacket(pKvsPeerConnection->pSrtpSession, pRawPacket, &rawLen));
    CHK_STATUS(ice_agent_send(pKvsPeerConnection->pIceAgent, pRawPacket, rawLen));

CleanUp:
    if (locked) {
        MUTEX_UNLOCK(pKvsPeerConnection->pSrtpSessionLock);
    }
    SAFE_MEMFREE(pRawPacket);

    return retStatus;
}

STATUS rtp_findTransceiverByssrc(PKvsPeerConnection pKvsPeerConnection, UINT32 ssrc)
{
    PKvsRtpTransceiver p = NULL;
    return rtp_transceiver_findBySsrc(pKvsPeerConnection, &p, ssrc);
}

STATUS rtp_transceiver_findBySsrc(PKvsPeerConnection pKvsPeerConnection, PKvsRtpTransceiver* ppTransceiver, UINT32 ssrc)
{
    STATUS retStatus = STATUS_SUCCESS;
    PDoubleListNode pCurNode = NULL;
    UINT64 item = 0;
    PKvsRtpTransceiver pTransceiver = NULL;
    CHK(pKvsPeerConnection != NULL && ppTransceiver != NULL, STATUS_RTP_NULL_ARG);

    CHK_STATUS(double_list_getHeadNode(pKvsPeerConnection->pTransceivers, &pCurNode));
    while (pCurNode != NULL) {
        CHK_STATUS(double_list_getNodeData(pCurNode, &item));
        pTransceiver = (PKvsRtpTransceiver) item;
        if (pTransceiver->sender.ssrc == ssrc || pTransceiver->sender.rtxSsrc == ssrc || pTransceiver->jitterBufferSsrc == ssrc) {
            break;
        }
        pTransceiver = NULL;
        pCurNode = pCurNode->pNext;
    }
    CHK(pTransceiver != NULL, STATUS_NOT_FOUND);
    *ppTransceiver = pTransceiver;

CleanUp:
    CHK_LOG_ERR(retStatus);
    return retStatus;
}
#endif
