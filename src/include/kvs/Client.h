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
#ifndef __KINESIS_VIDEO_CLIENT_INCLUDE__
#define __KINESIS_VIDEO_CLIENT_INCLUDE__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "kvs/common_defs.h"
#include <kvs/platform_utils.h>
#include "kvs/error.h"
/******************************************************************************
 * Main defines
 ******************************************************************************/
/**
 * Max device name length in chars
 */
#define MAX_DEVICE_NAME_LEN 256

/**
 * Max update version length in chars
 * https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_DeleteSignalingChannel.html#KinesisVideo-DeleteSignalingChannel-request-CurrentVersion
 */
#define MAX_UPDATE_VERSION_LEN 64

/**
 * Max ARN len in chars
 * https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_DescribeSignalingChannel.html#API_DescribeSignalingChannel_RequestSyntax
 * https://docs.aws.amazon.com/kinesisvideostreams/latest/dg/API_CreateStream.html#KinesisVideo-CreateStream-request-KmsKeyId
 */
#define MAX_ARN_LEN 1024

/**
 * Max len of the auth data (STS or Cert) in bytes
 */
#define MAX_AUTH_LEN 10000

/**
 * Max len of the fully qualified URI
 */
#define MAX_URI_CHAR_LEN 10 * 1024

/**
 * Min streaming token expiration duration. Currently defined as 30 seconds.
 */
#define MIN_STREAMING_TOKEN_EXPIRATION_DURATION (30 * HUNDREDS_OF_NANOS_IN_A_SECOND)

/**
 * The max streaming token expiration duration after which the ingestion host will force terminate the connection.
 */
#define MAX_ENFORCED_TOKEN_EXPIRATION_DURATION (40 * HUNDREDS_OF_NANOS_IN_A_MINUTE)

/**
 * Grace period for the streaming token expiration - 3 seconds
 */
#define STREAMING_TOKEN_EXPIRATION_GRACE_PERIOD (3 * HUNDREDS_OF_NANOS_IN_A_SECOND)

/**
 * Service call default timeout - 5 seconds
 */
#define SERVICE_CALL_DEFAULT_TIMEOUT (5 * HUNDREDS_OF_NANOS_IN_A_SECOND)

/**
 * Service call infinite timeout for streaming
 */
#define SERVICE_CALL_INFINITE_TIMEOUT MAX_UINT64

/**
 * Default service call retry count
 */
#define SERVICE_CALL_MAX_RETRY_COUNT 5

/**
 * This is a sentinel indicating an invalid timestamp value
 */
#ifndef INVALID_TIMESTAMP_VALUE
#define INVALID_TIMESTAMP_VALUE ((UINT64) 0xFFFFFFFFFFFFFFFFULL)
#endif

/**
 * Checks for the handle validity
 */
#ifndef IS_VALID_TIMESTAMP
#define IS_VALID_TIMESTAMP(h) ((h) != INVALID_TIMESTAMP_VALUE)
#endif

////////////////////////////////////////////////////
// Main structure declarations
////////////////////////////////////////////////////

/**
 * @brief Service call result
 *  https://developer.mozilla.org/en-US/docs/Web/HTTP/Status
 */
typedef enum {
    // Not defined
    HTTP_STATUS_NONE = 0,
    // Information responses
    //
    HTTP_STATUS_CONTINUE = 100,

    HTTP_STATUS_SWITCH_PROTOCOL = 101,

    // Successful responses
    // All OK
    HTTP_STATUS_OK = 200,

    // Client error responses
    // Bad request
    HTTP_STATUS_BAD_REQUEST = 400,
    // Security error
    HTTP_STATUS_UNAUTHORIZED = 401,
    // Forbidden
    HTTP_STATUS_FORBIDDEN = 403,
    // Resource not found exception
    HTTP_STATUS_NOT_FOUND = 404,
    // Invalid params error
    HTTP_STATUS_NOT_ACCEPTABLE = 406,
    // Request timeout
    HTTP_STATUS_REQUEST_TIMEOUT = 408,
    // Internal server error
    HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,

    // Server error responses
    // Not implemented
    HTTP_STATUS_NOT_IMPLEMENTED = 501,

    // Service unavailable
    HTTP_STATUS_SERVICE_UNAVAILABLE = 503,

    // Gateway timeout
    HTTP_STATUS_GATEWAY_TIMEOUT = 504,

    // Network read timeout
    HTTP_STATUS_NETWORK_READ_TIMEOUT = 598,

    // Network connection timeout
    HTTP_STATUS_NETWORK_CONNECTION_TIMEOUT = 599,

    // Go Away result
    HTTP_STATUS_SIGNALING_GO_AWAY = 6000,

    // Reconnect ICE Server
    HTTP_STATUS_SIGNALING_RECONNECT_ICE = 6001,

    // Client limit exceeded error
    HTTP_STATUS_CLIENT_LIMIT = 10000,

    // Device limit exceeded error
    HTTP_STATUS_DEVICE_LIMIT = 10001,

    // Stream limit exception
    HTTP_STATUS_STREAM_LIMIT = 10002,

    // Resource in use exception
    HTTP_STATUS_RESOURCE_IN_USE = 10003,

    // Device not provisioned
    HTTP_STATUS_DEVICE_NOT_PROVISIONED = 10004,

    // Device not found
    HTTP_STATUS_DEVICE_NOT_FOUND = 10005,
    // Other errors
    HTTP_STATUS_UNKNOWN = 10006,
    // Resource deleted exception
    HTTP_STATUS_RESOURCE_DELETED = 10400,

} HTTP_STATUS_CODE;

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_CLIENT_INCLUDE__ */
