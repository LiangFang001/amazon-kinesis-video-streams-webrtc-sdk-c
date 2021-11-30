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
#ifndef __AWS_KVS_WEBRTC_CRC32_INCLUDE__
#define __AWS_KVS_WEBRTC_CRC32_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif
#include "kvs/common_defs.h"
/**
 * Updates a CRC32 checksum
 * @UINT32 - IN - initial checksum result from previous update; for the first call, it should be 0.
 * @PBYTE - IN - buffer used to compute checksum
 * @UINT32 - IN - number of bytes to use from buffer
 * @return - UINT32 crc32 checksum
 */
UINT32 updateCrc32(UINT32, PBYTE, UINT32);

/**
 * @PBYTE - IN - buffer used to compute checksum
 * @UINT32 - IN - number of bytes to use from buffer
 * @return - UINT32 crc32 checksum
 */
#define COMPUTE_CRC32(pBuffer, len) (updateCrc32(0, pBuffer, len))
#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_CRC32_INCLUDE__ */
