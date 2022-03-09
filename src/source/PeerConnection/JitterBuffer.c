#ifdef ENABLE_STREAMING
#define LOG_CLASS "JitterBuffer"

#include "JitterBuffer.h"

STATUS jitter_buffer_create(FrameReadyFunc onFrameReadyFunc, FrameDroppedFunc onFrameDroppedFunc, DepayRtpPayloadFunc depayRtpPayloadFunc,
                            UINT32 maxLatency, UINT32 clockRate, UINT64 customData, PJitterBuffer* ppJitterBuffer)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PJitterBuffer pJitterBuffer = NULL;

    CHK(ppJitterBuffer != NULL && onFrameReadyFunc != NULL && onFrameDroppedFunc != NULL && depayRtpPayloadFunc != NULL, STATUS_NULL_ARG);
    CHK(clockRate != 0, STATUS_INVALID_ARG);

    pJitterBuffer = (PJitterBuffer) MEMALLOC(SIZEOF(JitterBuffer));
    CHK(pJitterBuffer != NULL, STATUS_NOT_ENOUGH_MEMORY);

    pJitterBuffer->onFrameReadyFn = onFrameReadyFunc;
    pJitterBuffer->onFrameDroppedFn = onFrameDroppedFunc;
    pJitterBuffer->depayPayloadFn = depayRtpPayloadFunc;
    pJitterBuffer->clockRate = clockRate;

    pJitterBuffer->maxLatency = maxLatency;
    if (pJitterBuffer->maxLatency == 0) {
        pJitterBuffer->maxLatency = DEFAULT_JITTER_BUFFER_MAX_LATENCY;
    }
    pJitterBuffer->maxLatency = pJitterBuffer->maxLatency * pJitterBuffer->clockRate / HUNDREDS_OF_NANOS_IN_A_SECOND;

    pJitterBuffer->lastPushTimestamp = 0;
    pJitterBuffer->lastPopTimestamp = MAX_UINT32;
    pJitterBuffer->lastRemovedSequenceNumber = MAX_SEQUENCE_NUM;
    pJitterBuffer->started = FALSE;

    pJitterBuffer->customData = customData;
    CHK_STATUS(hash_table_createWithParams(JITTER_BUFFER_HASH_TABLE_BUCKET_COUNT, JITTER_BUFFER_HASH_TABLE_BUCKET_LENGTH,
                                           &pJitterBuffer->pPkgBufferHashTable));

CleanUp:
    if (STATUS_FAILED(retStatus) && pJitterBuffer != NULL) {
        jitter_buffer_free(&pJitterBuffer);
        pJitterBuffer = NULL;
    }

    if (ppJitterBuffer != NULL) {
        *ppJitterBuffer = pJitterBuffer;
    }

    LEAVES();
    return retStatus;
}

STATUS jitter_buffer_free(PJitterBuffer* ppJitterBuffer)
{
    ENTERS();

    STATUS retStatus = STATUS_SUCCESS;
    PJitterBuffer pJitterBuffer = NULL;

    CHK(ppJitterBuffer != NULL, STATUS_NULL_ARG);
    // jitter_buffer_free is idempotent
    CHK(*ppJitterBuffer != NULL, retStatus);

    pJitterBuffer = *ppJitterBuffer;

    jitter_buffer_pop(pJitterBuffer, TRUE);
    jitter_buffer_dropBufferData(pJitterBuffer, 0, MAX_SEQUENCE_NUM, 0);
    hash_table_free(pJitterBuffer->pPkgBufferHashTable);

    MEMFREE(*ppJitterBuffer);

CleanUp:
    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS jitter_buffer_push(PJitterBuffer pJitterBuffer, PRtpPacket pRtpPacket, PBOOL pPacketDiscarded)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS, status = STATUS_SUCCESS;
    UINT64 hashValue = 0;
    PRtpPacket pCurPacket = NULL;

    CHK(pJitterBuffer != NULL && pRtpPacket != NULL, STATUS_NULL_ARG);

    if (!pJitterBuffer->started ||
        (pJitterBuffer->lastPopTimestamp == pRtpPacket->header.sequenceNumber &&
         pJitterBuffer->lastRemovedSequenceNumber >= pRtpPacket->header.sequenceNumber)) {
        // Set to started and initialize the sequence number
        pJitterBuffer->started = TRUE;
        pJitterBuffer->lastRemovedSequenceNumber = UINT16_DEC(pRtpPacket->header.sequenceNumber);
    }

    if (pJitterBuffer->lastPushTimestamp < pRtpPacket->header.timestamp) {
        pJitterBuffer->lastPushTimestamp = pRtpPacket->header.timestamp;
    }

    if ((pRtpPacket->header.timestamp < pJitterBuffer->maxLatency && pJitterBuffer->lastPushTimestamp <= pJitterBuffer->maxLatency) ||
        pRtpPacket->header.timestamp >= pJitterBuffer->lastPushTimestamp - pJitterBuffer->maxLatency) {
        // check the same sequence number is existed or not.
        status = hash_table_get(pJitterBuffer->pPkgBufferHashTable, pRtpPacket->header.sequenceNumber, &hashValue);
        pCurPacket = (PRtpPacket) hashValue;
        // remove the old sequence number.
        if (STATUS_SUCCEEDED(status) && pCurPacket != NULL) {
            rtp_packet_free(&pCurPacket);
            CHK_STATUS(hash_table_remove(pJitterBuffer->pPkgBufferHashTable, pRtpPacket->header.sequenceNumber));
        }
        // push the new sequence number.
        CHK_STATUS(hash_table_put(pJitterBuffer->pPkgBufferHashTable, pRtpPacket->header.sequenceNumber, (UINT64) pRtpPacket));
        pJitterBuffer->lastPopTimestamp = MIN(pJitterBuffer->lastPopTimestamp, pRtpPacket->header.timestamp);
        DLOGS("jitter_buffer_push get packet timestamp %lu seqNum %lu", pRtpPacket->header.timestamp, pRtpPacket->header.sequenceNumber);
    } else {
        // Free the packet if it is out of range, jitter buffer need to own the packet and do free
        rtp_packet_free(&pRtpPacket);
        if (pPacketDiscarded != NULL) {
            *pPacketDiscarded = TRUE;
        }
    }

    CHK_STATUS(jitter_buffer_pop(pJitterBuffer, FALSE));

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS jitter_buffer_pop(PJitterBuffer pJitterBuffer, BOOL bufferClosed)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT16 index;
    UINT16 lastIndex;
    UINT32 earliestTimestamp = 0;
    BOOL isFrameDataContinuous = TRUE;
    UINT32 curTimestamp = 0;
    UINT16 startDropIndex = 0;
    UINT32 curFrameSize = 0;
    UINT32 partialFrameSize = 0;
    UINT64 hashValue = 0;
    BOOL isStart = FALSE, containStartForEarliestFrame = FALSE, hasEntry = FALSE;
    UINT16 lastNonNullIndex = 0;
    PRtpPacket pCurPacket = NULL;

    CHK(pJitterBuffer != NULL && pJitterBuffer->onFrameDroppedFn != NULL && pJitterBuffer->onFrameReadyFn != NULL, STATUS_NULL_ARG);
    CHK(pJitterBuffer->lastPushTimestamp != 0, retStatus);

    if (pJitterBuffer->lastPushTimestamp > pJitterBuffer->maxLatency) {
        earliestTimestamp = pJitterBuffer->lastPushTimestamp - pJitterBuffer->maxLatency;
    }

    lastIndex = pJitterBuffer->lastRemovedSequenceNumber;
    index = pJitterBuffer->lastRemovedSequenceNumber + 1;
    startDropIndex = index;
    for (; index != lastIndex; index++) {
        CHK_STATUS(hash_table_contains(pJitterBuffer->pPkgBufferHashTable, index, &hasEntry));
        if (!hasEntry) {
            isFrameDataContinuous = FALSE;
            CHK(pJitterBuffer->lastPopTimestamp < earliestTimestamp || bufferClosed, retStatus);
        } else {
            lastNonNullIndex = index;
            retStatus = hash_table_get(pJitterBuffer->pPkgBufferHashTable, index, &hashValue);
            pCurPacket = (PRtpPacket) hashValue;
            if (retStatus == STATUS_SUCCESS || retStatus == STATUS_HASH_KEY_NOT_PRESENT) {
                retStatus = STATUS_SUCCESS;
            } else {
                CHK(FALSE, retStatus);
            }
            curTimestamp = pCurPacket->header.timestamp;
            if (curTimestamp != pJitterBuffer->lastPopTimestamp) {
                if (pJitterBuffer->lastPopTimestamp < earliestTimestamp || bufferClosed) {
                    if (containStartForEarliestFrame && isFrameDataContinuous) {
                        // TODO: if switch to curBuffer, need to carefully calculate ptr of UINT16_DEC(index) as it is a circulate buffer
                        CHK_STATUS(pJitterBuffer->onFrameReadyFn(pJitterBuffer->customData, startDropIndex, UINT16_DEC(index), curFrameSize));
                        CHK_STATUS(jitter_buffer_dropBufferData(pJitterBuffer, startDropIndex, UINT16_DEC(index), curTimestamp));
                        curFrameSize = 0;
                        containStartForEarliestFrame = FALSE;
                    } else {
                        CHK_STATUS(pJitterBuffer->onFrameDroppedFn(pJitterBuffer->customData, startDropIndex, UINT16_DEC(index),
                                                                   pJitterBuffer->lastPopTimestamp));
                        CHK_STATUS(jitter_buffer_dropBufferData(pJitterBuffer, startDropIndex, UINT16_DEC(index), curTimestamp));
                        curFrameSize = 0;
                        isFrameDataContinuous = TRUE;
                    }
                    startDropIndex = index;
                } else {
                    if (containStartForEarliestFrame) {
                        CHK(!bufferClosed, retStatus);
                        if (isFrameDataContinuous) {
                            // TODO: if switch to curBuffer, need to carefully calculate ptr of UINT16_DEC(index) as it is a circulate buffer
                            CHK_STATUS(pJitterBuffer->onFrameReadyFn(pJitterBuffer->customData, startDropIndex, UINT16_DEC(index), curFrameSize));
                            CHK_STATUS(jitter_buffer_dropBufferData(pJitterBuffer, startDropIndex, UINT16_DEC(index), curTimestamp));
                            startDropIndex = index;
                            curFrameSize = 0;
                        }
                        containStartForEarliestFrame = FALSE;
                    }
                }
            }

            CHK_STATUS(pJitterBuffer->depayPayloadFn(pCurPacket->payload, pCurPacket->payloadLength, NULL, &partialFrameSize, &isStart));
            curFrameSize += partialFrameSize;
            if (isStart && pJitterBuffer->lastPopTimestamp == curTimestamp) {
                containStartForEarliestFrame = TRUE;
            }
        }
    }

    // Deal with last frame
    if (bufferClosed && curFrameSize > 0) {
        curFrameSize = 0;
        hasEntry = TRUE;
        for (index = startDropIndex; UINT16_DEC(index) != lastNonNullIndex && hasEntry; index++) {
            CHK_STATUS(hash_table_contains(pJitterBuffer->pPkgBufferHashTable, index, &hasEntry));
            if (hasEntry) {
                CHK_STATUS(hash_table_get(pJitterBuffer->pPkgBufferHashTable, index, &hashValue));
                pCurPacket = (PRtpPacket) hashValue;
                CHK_STATUS(pJitterBuffer->depayPayloadFn(pCurPacket->payload, pCurPacket->payloadLength, NULL, &partialFrameSize, NULL));
                curFrameSize += partialFrameSize;
            }
        }

        // There is no NULL between startIndex and lastNonNullIndex
        if (UINT16_DEC(index) == lastNonNullIndex) {
            CHK_STATUS(pJitterBuffer->onFrameReadyFn(pJitterBuffer->customData, startDropIndex, lastNonNullIndex, curFrameSize));
            CHK_STATUS(jitter_buffer_dropBufferData(pJitterBuffer, startDropIndex, lastNonNullIndex, pJitterBuffer->lastPopTimestamp));
        } else {
            CHK_STATUS(
                pJitterBuffer->onFrameDroppedFn(pJitterBuffer->customData, startDropIndex, UINT16_DEC(index), pJitterBuffer->lastPopTimestamp));
            CHK_STATUS(jitter_buffer_dropBufferData(pJitterBuffer, startDropIndex, lastNonNullIndex, pJitterBuffer->lastPopTimestamp));
        }
    }

CleanUp:
    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS jitter_buffer_dropBufferData(PJitterBuffer pJitterBuffer, UINT16 startIndex, UINT16 endIndex, UINT32 nextTimestamp)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT16 index = startIndex;
    UINT64 hashValue;
    PRtpPacket pCurPacket = NULL;
    BOOL hasEntry = FALSE;

    CHK(pJitterBuffer != NULL, STATUS_NULL_ARG);
    for (; UINT16_DEC(index) != endIndex; index++) {
        CHK_STATUS(hash_table_contains(pJitterBuffer->pPkgBufferHashTable, index, &hasEntry));
        if (hasEntry) {
            CHK_STATUS(hash_table_get(pJitterBuffer->pPkgBufferHashTable, index, &hashValue));
            pCurPacket = (PRtpPacket) hashValue;
            rtp_packet_free(&pCurPacket);
            CHK_STATUS(hash_table_remove(pJitterBuffer->pPkgBufferHashTable, index));
        }
    }
    pJitterBuffer->lastPopTimestamp = nextTimestamp;
    pJitterBuffer->lastRemovedSequenceNumber = endIndex;

CleanUp:
    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS jitter_buffer_fillFrameData(PJitterBuffer pJitterBuffer, PBYTE pFrame, UINT32 frameSize, PUINT32 pFilledSize, UINT16 startIndex,
                                   UINT16 endIndex)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT16 index = startIndex;
    UINT64 hashValue;
    PRtpPacket pCurPacket = NULL;
    PBYTE pCurPtrInFrame = pFrame;
    UINT32 remainingFrameSize = frameSize;
    UINT32 partialFrameSize = 0;

    CHK(pJitterBuffer != NULL && pFrame != NULL && pFilledSize != NULL, STATUS_NULL_ARG);
    for (; UINT16_DEC(index) != endIndex; index++) {
        hashValue = 0;
        retStatus = hash_table_get(pJitterBuffer->pPkgBufferHashTable, index, &hashValue);
        pCurPacket = (PRtpPacket) hashValue;
        if (retStatus == STATUS_SUCCESS || retStatus == STATUS_HASH_KEY_NOT_PRESENT) {
            retStatus = STATUS_SUCCESS;
        } else {
            CHK(FALSE, retStatus);
        }
        CHK(pCurPacket != NULL, STATUS_NULL_ARG);
        partialFrameSize = remainingFrameSize;
        CHK_STATUS(pJitterBuffer->depayPayloadFn(pCurPacket->payload, pCurPacket->payloadLength, pCurPtrInFrame, &partialFrameSize, NULL));
        pCurPtrInFrame += partialFrameSize;
        remainingFrameSize -= partialFrameSize;
    }

CleanUp:
    if (pFilledSize != NULL) {
        *pFilledSize = frameSize - remainingFrameSize;
    }
    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}
#endif
