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
#ifndef __AWS_KVS_WEBRTC_FILE_IO_INCLUDE__
#define __AWS_KVS_WEBRTC_FILE_IO_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "kvs/error.h"
#include "kvs/common_defs.h"
/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS readFile(PCHAR filePath, BOOL binMode, PBYTE pBuffer, PUINT64 pSize);
STATUS readFileSegment(PCHAR filePath, BOOL binMode, PBYTE pBuffer, UINT64 offset, UINT64 readSize);
STATUS writeFile(PCHAR filePath, BOOL binMode, BOOL append, PBYTE pBuffer, UINT64 size);
STATUS getFileLength(PCHAR filePath, PUINT64 pSize);
STATUS fileExists(PCHAR filePath, PBOOL pExists);
STATUS createFile(PCHAR filePath, UINT64 size);

#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_FILE_IO_INCLUDE__ */
