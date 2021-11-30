/**
 * Main public include file
 */
#ifndef __KINESIS_VIDEO_CLIENT_INCLUDE__
#define __KINESIS_VIDEO_CLIENT_INCLUDE__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "kvs/common_defs.h"
#include <kvs/platform_utils.h>
#include "kvs/error.h"

////////////////////////////////////////////////////
// Main defines
////////////////////////////////////////////////////
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
 * Service call result
 */
typedef enum {
    // Not defined
    SERVICE_CALL_RESULT_NOT_SET = 0,

    // All OK
    SERVICE_CALL_RESULT_OK = 200,

    // Invalid params error
    SERVICE_CALL_INVALID_ARG = 406,

    // Resource not found exception
    SERVICE_CALL_RESOURCE_NOT_FOUND = 404,

    // Client limit exceeded error
    SERVICE_CALL_CLIENT_LIMIT = 10000,

    // Device limit exceeded error
    SERVICE_CALL_DEVICE_LIMIT = 10001,

    // Stream limit exception
    SERVICE_CALL_STREAM_LIMIT = 10002,

    // Resource in use exception
    SERVICE_CALL_RESOURCE_IN_USE = 10003,

    // Bad request
    SERVICE_CALL_BAD_REQUEST = 400,

    // Forbidden
    SERVICE_CALL_FORBIDDEN = 403,

    // Device not provisioned
    SERVICE_CALL_DEVICE_NOT_PROVISIONED = 10004,

    // Device not found
    SERVICE_CALL_DEVICE_NOT_FOUND = 10005,

    // Security error
    SERVICE_CALL_NOT_AUTHORIZED = 401,

    // Request timeout
    SERVICE_CALL_REQUEST_TIMEOUT = 408,

    // Gateway timeout
    SERVICE_CALL_GATEWAY_TIMEOUT = 504,

    // Network read timeout
    SERVICE_CALL_NETWORK_READ_TIMEOUT = 598,

    // Network connection timeout
    SERVICE_CALL_NETWORK_CONNECTION_TIMEOUT = 599,

    // Resource deleted exception
    SERVICE_CALL_RESOURCE_DELETED = 10400,

    // Not implemented
    SERVICE_CALL_NOT_IMPLEMENTED = 501,

    // Internal server error
    SERVICE_CALL_INTERNAL_ERROR = 500,

    // Service unavailable
    SERVICE_CALL_SERVICE_UNAVAILABLE = 503,

    // Other errors
    SERVICE_CALL_UNKNOWN = 10006,

    // Go Away result
    SERVICE_CALL_RESULT_SIGNALING_GO_AWAY = 6000,

    // Reconnect ICE Server
    SERVICE_CALL_RESULT_SIGNALING_RECONNECT_ICE = 6001,

} SERVICE_CALL_RESULT;

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_CLIENT_INCLUDE__ */
