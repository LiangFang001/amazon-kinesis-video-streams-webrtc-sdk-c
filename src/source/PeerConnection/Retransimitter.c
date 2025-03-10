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
#ifdef ENABLE_STREAMING
#define LOG_CLASS "Retransmitter"

#include "RtpPacket.h"
#include "Rtp.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS retransmitter_create(UINT32 seqNumListLen, UINT32 validIndexListLen, PRetransmitter* ppRetransmitter)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PRetransmitter pRetransmitter = MEMALLOC(SIZEOF(Retransmitter) + SIZEOF(UINT16) * seqNumListLen + SIZEOF(UINT64) * validIndexListLen);
    CHK(pRetransmitter != NULL, STATUS_NOT_ENOUGH_MEMORY);
    pRetransmitter->sequenceNumberList = (PUINT16) (pRetransmitter + 1);
    pRetransmitter->seqNumListLen = seqNumListLen;
    pRetransmitter->validIndexList = (PUINT64) (pRetransmitter->sequenceNumberList + seqNumListLen);
    pRetransmitter->validIndexListLen = validIndexListLen;

CleanUp:
    if (STATUS_FAILED(retStatus) && pRetransmitter != NULL) {
        retransmitter_free(&pRetransmitter);
        pRetransmitter = NULL;
    }
    if (ppRetransmitter != NULL) {
        *ppRetransmitter = pRetransmitter;
    }
    LEAVES();
    return retStatus;
}

STATUS retransmitter_free(PRetransmitter* ppRetransmitter)
{
    ENTERS();

    STATUS retStatus = STATUS_SUCCESS;

    CHK(ppRetransmitter != NULL, STATUS_NULL_ARG);
    SAFE_MEMFREE(*ppRetransmitter);
CleanUp:
    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS retransmitter_resendPacketOnNack(PRtcpPacket pRtcpPacket, PKvsPeerConnection pKvsPeerConnection)
{
    ENTERS();

    STATUS retStatus = STATUS_SUCCESS;
    UINT32 senderSsrc = 0, receiverSsrc = 0;
    UINT32 filledLen = 0, validIndexListLen = 0;
    PKvsRtpTransceiver pSenderTranceiver = NULL;
    UINT64 item, index;
    STATUS tmpStatus = STATUS_SUCCESS;
    PRtpPacket pRtpPacket = NULL, pRtxRtpPacket = NULL;
    PRetransmitter pRetransmitter = NULL;
    // stats
    UINT32 retransmittedPacketsSent = 0, retransmittedBytesSent = 0, nackCount = 0;

    CHK(pKvsPeerConnection != NULL && pRtcpPacket != NULL, STATUS_NULL_ARG);
    CHK_STATUS(rtcp_packet_getNackList(pRtcpPacket->payload, pRtcpPacket->payloadLength, &senderSsrc, &receiverSsrc, NULL, &filledLen));

    tmpStatus = rtp_transceiver_findBySsrc(pKvsPeerConnection, &pSenderTranceiver, receiverSsrc);
    if (STATUS_NOT_FOUND == tmpStatus) {
        CHK_STATUS_ERR(rtp_transceiver_findBySsrc(pKvsPeerConnection, &pSenderTranceiver, senderSsrc), STATUS_RTCP_INPUT_SSRC_INVALID,
                       "Receiving NACK for non existing ssrcs: senderSsrc %lu receiverSsrc %lu", senderSsrc, receiverSsrc);
    }
    CHK_STATUS(tmpStatus);

    pRetransmitter = pSenderTranceiver->sender.retransmitter;
    // TODO it is not very clear from the spec whether nackCount is number of packets received or number of rtp packets lost reported in nack packets
    nackCount++;

    CHK_ERR(pRetransmitter != NULL, STATUS_INVALID_OPERATION,
            "Sender re-transmitter is not created successfully for an existing ssrcs: senderSsrc %lu receiverSsrc %lu", senderSsrc, receiverSsrc);

    filledLen = pRetransmitter->seqNumListLen;
    CHK_STATUS(rtcp_packet_getNackList(pRtcpPacket->payload, pRtcpPacket->payloadLength, &senderSsrc, &receiverSsrc,
                                       pRetransmitter->sequenceNumberList, &filledLen));
    validIndexListLen = pRetransmitter->validIndexListLen;
    CHK_STATUS(rtp_rolling_buffer_getValidSeqIndexList(pSenderTranceiver->sender.packetBuffer, pRetransmitter->sequenceNumberList, filledLen,
                                                       pRetransmitter->validIndexList, &validIndexListLen));
    for (index = 0; index < validIndexListLen; index++) {
        retStatus = rolling_buffer_extractData(pSenderTranceiver->sender.packetBuffer->pRollingBuffer, pRetransmitter->validIndexList[index], &item);
        pRtpPacket = (PRtpPacket) item;
        CHK(retStatus == STATUS_SUCCESS, retStatus);

        if (pRtpPacket != NULL) {
            if (pSenderTranceiver->sender.payloadType == pSenderTranceiver->sender.rtxPayloadType) {
                retStatus = ice_agent_send(pKvsPeerConnection->pIceAgent, pRtpPacket->pRawPacket, pRtpPacket->rawPacketLength);
                CHK_LOG_ERR(retStatus);
            } else {
                CHK_STATUS(rtp_packet_constructRetransmitPacketFromBytes(
                    pRtpPacket->pRawPacket, pRtpPacket->rawPacketLength, pSenderTranceiver->sender.rtxSequenceNumber,
                    pSenderTranceiver->sender.rtxPayloadType, pSenderTranceiver->sender.rtxSsrc, &pRtxRtpPacket));
                pSenderTranceiver->sender.rtxSequenceNumber++;
                retStatus = rtp_writePacket(pKvsPeerConnection, pRtxRtpPacket);
            }
            // resendPacket
            if (STATUS_SUCCEEDED(retStatus)) {
                retransmittedPacketsSent++;
                retransmittedBytesSent += pRtpPacket->rawPacketLength - RTP_HEADER_LEN(pRtpPacket);
                DLOGV("Resent packet ssrc %lu seq %lu succeeded", pRtpPacket->header.ssrc, pRtpPacket->header.sequenceNumber);
            } else {
                DLOGV("Resent packet ssrc %lu seq %lu failed 0x%08x", pRtpPacket->header.ssrc, pRtpPacket->header.sequenceNumber, retStatus);
            }
            // putBackPacketToRollingBuffer
            retStatus =
                rolling_buffer_insertData(pSenderTranceiver->sender.packetBuffer->pRollingBuffer, pRetransmitter->sequenceNumberList[index], item);
            CHK(retStatus == STATUS_SUCCESS || retStatus == STATUS_ROLLING_BUFFER_NOT_IN_RANGE, retStatus);

            // free the packet if it is not in the valid range any more
            if (retStatus == STATUS_ROLLING_BUFFER_NOT_IN_RANGE) {
                DLOGS("Retransmit STATUS_ROLLING_BUFFER_NOT_IN_RANGE free %lu by self", pRtpPacket->header.sequenceNumber);
                rtp_packet_free(&pRtpPacket);
                retStatus = STATUS_SUCCESS;
            } else {
                DLOGS("Retransmit add back to rolling %lu", pRtpPacket->header.sequenceNumber);
            }

            rtp_packet_free(&pRtxRtpPacket);
            pRtpPacket = NULL;
        }
    }
CleanUp:

    MUTEX_LOCK(pSenderTranceiver->statsLock);
    pSenderTranceiver->outboundStats.nackCount += nackCount;
    pSenderTranceiver->outboundStats.retransmittedPacketsSent += retransmittedPacketsSent;
    pSenderTranceiver->outboundStats.retransmittedBytesSent += retransmittedBytesSent;
    MUTEX_UNLOCK(pSenderTranceiver->statsLock);

    CHK_LOG_ERR(retStatus);
    if (pRtpPacket != NULL) {
        // free the packet as it is not put back into rolling buffer
        rtp_packet_free(&pRtpPacket);
        pRtpPacket = NULL;
    }

    LEAVES();
    return retStatus;
}
#endif
