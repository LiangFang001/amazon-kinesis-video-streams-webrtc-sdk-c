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
#define LOG_CLASS "RtpRollingBuffer"

#include "RtpRollingBuffer.h"
/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS rtp_rolling_buffer_create(UINT32 capacity, PRtpRollingBuffer* ppRtpRollingBuffer)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PRtpRollingBuffer pRtpRollingBuffer = NULL;
    CHK(capacity != 0, STATUS_INVALID_ARG);
    CHK(ppRtpRollingBuffer != NULL, STATUS_NULL_ARG);

    pRtpRollingBuffer = (PRtpRollingBuffer) MEMALLOC(SIZEOF(RtpRollingBuffer));
    CHK(pRtpRollingBuffer != NULL, STATUS_NOT_ENOUGH_MEMORY);
    CHK_STATUS(rolling_buffer_create(capacity, rtp_rolling_buffer_freeData, &pRtpRollingBuffer->pRollingBuffer));

CleanUp:
    if (ppRtpRollingBuffer != NULL) {
        *ppRtpRollingBuffer = pRtpRollingBuffer;
    }
    LEAVES();
    return retStatus;
}

STATUS rtp_rolling_buffer_free(PRtpRollingBuffer* ppRtpRollingBuffer)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(ppRtpRollingBuffer != NULL, STATUS_NULL_ARG);

    if (*ppRtpRollingBuffer != NULL) {
        rolling_buffer_free(&(*ppRtpRollingBuffer)->pRollingBuffer);
    }
    SAFE_MEMFREE(*ppRtpRollingBuffer);
CleanUp:
    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS rtp_rolling_buffer_freeData(PUINT64 pData)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    CHK(pData != NULL, STATUS_NULL_ARG);
    CHK_STATUS(freeRtpPacket((PRtpPacket*) pData));
CleanUp:
    LEAVES();
    return retStatus;
}

STATUS rtp_rolling_buffer_addRtpPacket(PRtpRollingBuffer pRollingBuffer, PRtpPacket pRtpPacket)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PRtpPacket pRtpPacketCopy = NULL;
    PBYTE pRawPacketCopy = NULL;
    UINT64 index = 0;
    CHK(pRollingBuffer != NULL && pRtpPacket != NULL, STATUS_RTP_NULL_ARG);

    pRawPacketCopy = (PBYTE) MEMALLOC(pRtpPacket->rawPacketLength);
    CHK(pRawPacketCopy != NULL, STATUS_RTP_NOT_ENOUGH_MEMORY);
    MEMCPY(pRawPacketCopy, pRtpPacket->pRawPacket, pRtpPacket->rawPacketLength);
    CHK_STATUS(createRtpPacketFromBytes(pRawPacketCopy, pRtpPacket->rawPacketLength, &pRtpPacketCopy));

    CHK_STATUS(rolling_buffer_appendData(pRollingBuffer->pRollingBuffer, (UINT64) pRtpPacketCopy, &index));
    pRollingBuffer->lastIndex = index;

CleanUp:
    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS rtp_rolling_buffer_getValidSeqIndexList(PRtpRollingBuffer pRollingBuffer, PUINT16 pSequenceNumberList, UINT32 sequenceNumberListLen,
                                               PUINT64 pValidSeqIndexList, PUINT32 pValidIndexListLen)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 index = 0, returnPacketCount = 0;
    UINT16 startSeq, endSeq;
    BOOL crossMaxSeq = FALSE, foundPacket = FALSE;
    PUINT16 pCurSeqPtr;
    PUINT64 pCurSeqIndexListPtr;
    UINT16 seqNum;
    UINT32 size = 0;

    CHK(pRollingBuffer != NULL && pValidSeqIndexList != NULL && pSequenceNumberList != NULL, STATUS_NULL_ARG);

    CHK_STATUS(rolling_buffer_getSize(pRollingBuffer->pRollingBuffer, &size));
    // Empty buffer, just return
    CHK(size > 0, retStatus);

    startSeq = GET_UINT16_SEQ_NUM(pRollingBuffer->lastIndex - size + 1);
    endSeq = GET_UINT16_SEQ_NUM(pRollingBuffer->lastIndex);

    if (startSeq >= endSeq) {
        crossMaxSeq = TRUE;
    }

    for (index = 0, pCurSeqPtr = pSequenceNumberList, pCurSeqIndexListPtr = pValidSeqIndexList; index < sequenceNumberListLen;
         index++, pCurSeqPtr++) {
        seqNum = *pCurSeqPtr;
        foundPacket = FALSE;
        if ((!crossMaxSeq && seqNum >= startSeq && seqNum <= endSeq) || (crossMaxSeq && seqNum >= startSeq)) {
            *pCurSeqIndexListPtr = pRollingBuffer->lastIndex - size + 1 + seqNum - startSeq;
            foundPacket = TRUE;
        } else if (crossMaxSeq && seqNum <= endSeq) {
            *pCurSeqIndexListPtr = pRollingBuffer->lastIndex - endSeq + seqNum;
            foundPacket = TRUE;
        }
        if (foundPacket) {
            pCurSeqIndexListPtr++;
            // Return if filled up given valid sequence number array
            CHK(++returnPacketCount < *pValidIndexListLen, retStatus);
            *pCurSeqIndexListPtr = (UINT32) NULL;
        }
    }

CleanUp:
    CHK_LOG_ERR(retStatus);

    if (pValidIndexListLen != NULL) {
        *pValidIndexListLen = returnPacketCount;
    }

    LEAVES();
    return retStatus;
}
