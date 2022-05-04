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
#ifndef __AWS_KVS_WEBRTC_CHANNEL_INFO_INCLUDE__
#define __AWS_KVS_WEBRTC_CHANNEL_INFO_INCLUDE__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "kvs/webrtc_client.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
// Max control plane URI char len
#define MAX_CONTROL_PLANE_URI_CHAR_LEN 256

// Max channel status string length in describe API call in chars
#define MAX_DESCRIBE_CHANNEL_STATUS_LEN 32

// Max channel type string length in describe API call in chars
#define MAX_DESCRIBE_CHANNEL_TYPE_LEN 128

// Signaling channel type string
#define SIGNALING_CHANNEL_TYPE_UNKNOWN_STR       (PCHAR) "UNKOWN"
#define SIGNALING_CHANNEL_TYPE_SINGLE_MASTER_STR (PCHAR) "SINGLE_MASTER"

// Signaling channel role type string
#define SIGNALING_CHANNEL_ROLE_TYPE_UNKNOWN_STR (PCHAR) "UNKOWN"
#define SIGNALING_CHANNEL_ROLE_TYPE_MASTER_STR  (PCHAR) "MASTER"
#define SIGNALING_CHANNEL_ROLE_TYPE_VIEWER_STR  (PCHAR) "VIEWER"

// Min and max for the message TTL value
#define MIN_SIGNALING_MESSAGE_TTL_VALUE (5 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define MAX_SIGNALING_MESSAGE_TTL_VALUE (120 * HUNDREDS_OF_NANOS_IN_A_SECOND)

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
/**
 * @brief Takes in a pointer to a public version of ChannelInfo object.
 * Validates and creates an internal object
 *
 * @param[in] pOrigChannelInfo
 * @param[in, out] ppChannelInfo
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS channel_info_create(PChannelInfo pOrigChannelInfo, PChannelInfo* ppChannelInfo);

/**
 * Frees the channel info object.
 *
 * @param - PChannelInfo* - IN - Channel info object to free
 *
 * @return - STATUS code of the execution
 */
STATUS channel_info_free(PChannelInfo*);

/**
 * Returns the signaling channel status from a string
 *
 * @param - PCHAR - IN - String representation of the channel status
 * @param - UINT32 - IN - String length
 *
 * @return - Signaling channel status type
 */
SIGNALING_CHANNEL_STATUS getChannelStatusFromString(PCHAR, UINT32);

/**
 * Returns the signaling channel type from a string
 *
 * @param - PCHAR - IN - String representation of the channel type
 * @param - UINT32 - IN - String length
 *
 * @return - Signaling channel type
 */
SIGNALING_CHANNEL_TYPE getChannelTypeFromString(PCHAR, UINT32);

/**
 * Returns the signaling channel type string
 *
 * @param - SIGNALING_CHANNEL_TYPE - IN - Signaling channel type
 *
 * @return - Signaling channel type string
 */
PCHAR getStringFromChannelType(SIGNALING_CHANNEL_TYPE);

/**
 * Returns the signaling channel Role from a string
 *
 * @param - PCHAR - IN - String representation of the channel role
 * @param - UINT32 - IN - String length
 *
 * @return - Signaling channel type
 */
SIGNALING_CHANNEL_ROLE_TYPE getChannelRoleTypeFromString(PCHAR, UINT32);

/**
 * Returns the signaling channel role type string
 *
 * @param - SIGNALING_CHANNEL_TYPE - IN - Signaling channel type
 *
 * @return - Signaling channel type string
 */
PCHAR getStringFromChannelRoleType(SIGNALING_CHANNEL_ROLE_TYPE);

#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_CHANNEL_INFO_INCLUDE__ */
