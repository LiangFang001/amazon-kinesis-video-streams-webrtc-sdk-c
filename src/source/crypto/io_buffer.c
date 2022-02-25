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
#define LOG_CLASS "IOBuffer"
#include "io_buffer.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS io_buffer_create(UINT32 initialCap, PIOBuffer* ppBuffer)
{
    STATUS retStatus = STATUS_SUCCESS;
    PIOBuffer pBuffer = NULL;

    pBuffer = (PIOBuffer) MEMCALLOC(SIZEOF(IOBuffer), 1);
    CHK(pBuffer != NULL, STATUS_NOT_ENOUGH_MEMORY);

    if (initialCap != 0) {
        pBuffer->raw = (PBYTE) MEMALLOC(initialCap);
        CHK(pBuffer->raw != NULL, STATUS_NOT_ENOUGH_MEMORY);
        pBuffer->cap = initialCap;
    }

    *ppBuffer = pBuffer;

CleanUp:

    if (STATUS_FAILED(retStatus) && pBuffer != NULL) {
        io_buffer_free(&pBuffer);
    }

    return retStatus;
}

STATUS io_buffer_free(PIOBuffer* ppBuffer)
{
    STATUS retStatus = STATUS_SUCCESS;
    PIOBuffer pBuffer;

    CHK(ppBuffer != NULL, STATUS_NULL_ARG);

    pBuffer = *ppBuffer;
    CHK(pBuffer != NULL, retStatus);

    MEMFREE(pBuffer->raw);
    SAFE_MEMFREE(*ppBuffer);

CleanUp:

    return retStatus;
}

STATUS io_buffer_reset(PIOBuffer pBuffer)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pBuffer != NULL, STATUS_NULL_ARG);

    pBuffer->len = 0;
    pBuffer->off = 0;

CleanUp:

    return retStatus;
}

STATUS io_buffer_write(PIOBuffer pBuffer, PBYTE pData, UINT32 dataLen)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 freeSpace;
    UINT32 newCap;

    CHK(pBuffer != NULL && pData != NULL, STATUS_NULL_ARG);

    freeSpace = pBuffer->cap - pBuffer->len;
    if (freeSpace < dataLen) {
        newCap = pBuffer->len + dataLen;
        pBuffer->raw = MEMREALLOC(pBuffer->raw, newCap);
        CHK(pBuffer->raw != NULL, STATUS_NOT_ENOUGH_MEMORY);
        pBuffer->cap = newCap;
    }

    MEMCPY(pBuffer->raw + pBuffer->len, pData, dataLen);
    pBuffer->len += dataLen;

CleanUp:

    return retStatus;
}

STATUS io_buffer_read(PIOBuffer pBuffer, PBYTE pData, UINT32 bufferLen, PUINT32 pDataLen)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 dataLen;

    CHK(pBuffer != NULL && pDataLen != NULL, STATUS_NULL_ARG);

    dataLen = MIN(bufferLen, pBuffer->len - pBuffer->off);

    MEMCPY(pData, pBuffer->raw + pBuffer->off, dataLen);
    pBuffer->off += dataLen;

    if (pBuffer->off == pBuffer->len) {
        io_buffer_reset(pBuffer);
    }

    *pDataLen = dataLen;

CleanUp:

    return retStatus;
}
