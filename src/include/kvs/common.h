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
#ifndef __KINESIS_VIDEO_COMMON_INCLUDE__
#define __KINESIS_VIDEO_COMMON_INCLUDE__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "kvs/error.h"
#include "single_linked_list.h"
#include "stack_queue.h"

/* Config for Ameba-Pro */
#include "sample_config_webrtc.h"

#ifndef JSMN_HEADER
#define JSMN_HEADER
#endif
#include <jsmn.h>

/******************************************************************************
 * DEFINITIONS
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

/**
 * Environment variable to enable file logging. Run export AWS_ENABLE_FILE_LOGGING=TRUE to enable file
 * logging
 */
#define ENABLE_FILE_LOGGING ((PCHAR) "AWS_ENABLE_FILE_LOGGING")

/**
 * Max region name
 */
#define MAX_REGION_NAME_LEN 128

/**
 * Max user agent string
 */
#define MAX_USER_AGENT_LEN 256

/**
 * Max custom user agent string
 */
#define MAX_CUSTOM_USER_AGENT_LEN 128

/**
 * Max custom user agent name postfix string
 */
#define MAX_CUSTOM_USER_AGENT_NAME_POSTFIX_LEN 32

/**
 * Default Video track ID to be used
 */
#define DEFAULT_VIDEO_TRACK_ID 1

/**
 * Default Audio track ID to be used
 */
#define DEFAULT_AUDIO_TRACK_ID 2

/*
 * Max access key id length https://docs.aws.amazon.com/STS/latest/APIReference/API_Credentials.html
 */
#define MAX_ACCESS_KEY_LEN 128

/*
 * Max secret access key length
 */
#define MAX_SECRET_KEY_LEN 128

/*
 * Max session token string length
 */
#define MAX_SESSION_TOKEN_LEN 2048

/*
 * Max expiration string length
 */
#define MAX_EXPIRATION_LEN 128

/*
 * Max role alias length https://docs.aws.amazon.com/iot/latest/apireference/API_UpdateRoleAlias.html
 */
#define MAX_ROLE_ALIAS_LEN 128

/**
 * Max string length for IoT thing name
 */
#define MAX_IOT_THING_NAME_LEN MAX_DEVICE_NAME_LEN

/**
 * Default period for the cached endpoint update
 */
#define DEFAULT_ENDPOINT_CACHE_UPDATE_PERIOD (40 * HUNDREDS_OF_NANOS_IN_A_MINUTE)

/**
 * Sentinel value indicating to use default update period
 */
#define ENDPOINT_UPDATE_PERIOD_SENTINEL_VALUE 0

/**
 * Max period for the cached endpoint update
 */
#define MAX_ENDPOINT_CACHE_UPDATE_PERIOD (24 * HUNDREDS_OF_NANOS_IN_AN_HOUR)

/**
 * AWS credential environment variable name
 */
#define ACCESS_KEY_ENV_VAR      ((PCHAR) "AWS_ACCESS_KEY_ID")
#define SECRET_KEY_ENV_VAR      ((PCHAR) "AWS_SECRET_ACCESS_KEY")
#define SESSION_TOKEN_ENV_VAR   ((PCHAR) "AWS_SESSION_TOKEN")
#define DEFAULT_REGION_ENV_VAR  ((PCHAR) "AWS_DEFAULT_REGION")
#define CACERT_PATH_ENV_VAR     ((PCHAR) "AWS_KVS_CACERT_PATH")
#define DEBUG_LOG_LEVEL_ENV_VAR ((PCHAR) "AWS_KVS_LOG_LEVEL")

#ifdef CMAKE_DETECTED_CACERT_PATH
#define DEFAULT_KVS_CACERT_PATH KVS_CA_CERT_PATH
#else
#ifdef KVS_PLAT_RTK_FREERTOS
#define KVS_CA_CERT_PATH        KVS_WEBRTC_ROOT_CA_PATH
#define DEFAULT_KVS_CACERT_PATH KVS_WEBRTC_ROOT_CA_PATH
#else
#define DEFAULT_KVS_CACERT_PATH EMPTY_STRING
#endif
#endif

// Protocol scheme names
#define HTTPS_SCHEME_NAME "https"
#define WSS_SCHEME_NAME   "wss"

// Max header name length in chars
#define MAX_REQUEST_HEADER_NAME_LEN 128

// Max header value length in chars
#define MAX_REQUEST_HEADER_VALUE_LEN 2048

// Max header count
#define MAX_REQUEST_HEADER_COUNT 200

// Max delimiter characters when packing headers into a string for printout
#define MAX_REQUEST_HEADER_OUTPUT_DELIMITER 5

// Max request header length in chars including the name/value, delimiter and null terminator
#define MAX_REQUEST_HEADER_STRING_LEN (MAX_REQUEST_HEADER_NAME_LEN + MAX_REQUEST_HEADER_VALUE_LEN + 3)

// Literal definitions of the request verbs
#define HTTP_REQUEST_VERB_GET_STRING  (PCHAR) "GET"
#define HTTP_REQUEST_VERB_PUT_STRING  (PCHAR) "PUT"
#define HTTP_REQUEST_VERB_POST_STRING (PCHAR) "POST"

// Schema delimiter string
#define SCHEMA_DELIMITER_STRING (PCHAR) "://"

// Default canonical URI if we fail to get anything from the parsing
#define DEFAULT_CANONICAL_URI_STRING (PCHAR) "/"

// HTTP status OK
#define HTTP_STATUS_CODE_OK 200

// HTTP status Request timed out
#define HTTP_STATUS_CODE_REQUEST_TIMEOUT 408

/**
 * Maximal length of the credentials file
 */
#define MAX_CREDENTIAL_FILE_LEN MAX_AUTH_LEN

/**
 * Default AWS region
 */
#define DEFAULT_AWS_REGION KVS_WEBRTC_REGION

/**
 * Control plane prefix
 */
#define CONTROL_PLANE_URI_PREFIX "https://"

/**
 * KVS service name
 */
#define KINESIS_VIDEO_SERVICE_NAME "kinesisvideo"

/**
 * Control plane postfix
 */
#define CONTROL_PLANE_URI_POSTFIX ".amazonaws.com"

/**
 * Default user agent name
 */
#define DEFAULT_USER_AGENT_NAME "AWS-SDK-KVS"

/**
 * Max number of tokens in the API return JSON
 */
#define MAX_JSON_TOKEN_COUNT 100

/**
 * Max parameter JSON string len which will be used for preparing the parameterized strings for the API calls.
 */
#define MAX_JSON_PARAMETER_STRING_LEN (10 * 1024)

/**
 * Current versions for the public structs
 */
#define AWS_CREDENTIALS_CURRENT_VERSION 0

/**
 * Buffer length for the error to be stored in
 */
#define CALL_INFO_ERROR_BUFFER_LEN 256

/**
 * Parameterized string for each tag pair
 */
#define TAG_PARAM_JSON_TEMPLATE "\n\t\t\"%s\": \"%s\","

/**
 * Low speed limits in bytes per duration
 */
#define DEFAULT_LOW_SPEED_LIMIT 30

/**
 * Low speed limits in 100ns for the amount of bytes per this duration
 */
#define DEFAULT_LOW_SPEED_TIME_LIMIT (30 * HUNDREDS_OF_NANOS_IN_A_SECOND)

// Header delimiter for requests and it's size
#define REQUEST_HEADER_DELIMITER      ((PCHAR) ": ")
#define REQUEST_HEADER_DELIMITER_SIZE (2 * SIZEOF(CHAR))

/*
 * Default SSL port
 */
#define DEFAULT_SSL_PORT_NUMBER 443

/*
 * Default non-SSL port
 */
#define DEFAULT_NON_SSL_PORT_NUMBER 8080

/**
 * AWS service Request id header name
 */
#define KVS_REQUEST_ID_HEADER_NAME "x-amzn-RequestId"

////////////////////////////////////////////////////
// Main structure declarations
////////////////////////////////////////////////////

/**
 * Types of verbs
 */
typedef enum { HTTP_REQUEST_VERB_GET, HTTP_REQUEST_VERB_POST, HTTP_REQUEST_VERB_PUT } HTTP_REQUEST_VERB;

/**
 * Request SSL certificate type Not specified, "DER", "PEM", "ENG"
 */
typedef enum {
    SSL_CERTIFICATE_TYPE_NOT_SPECIFIED,
    SSL_CERTIFICATE_TYPE_DER,
    SSL_CERTIFICATE_TYPE_PEM,
    SSL_CERTIFICATE_TYPE_ENG,
} SSL_CERTIFICATE_TYPE;

/**
 * AWS Credentials declaration
 */
typedef struct __AwsCredentials AwsCredentials;
struct __AwsCredentials {
    // Version
    UINT32 version;

    // Size of the entire structure in bytes including the struct itself
    UINT32 size;

    // Access Key ID - NULL terminated
    PCHAR accessKeyId;

    // Length of the access key id - not including NULL terminator
    UINT32 accessKeyIdLen;

    // Secret Key - NULL terminated
    PCHAR secretKey;

    // Length of the secret key - not including NULL terminator
    UINT32 secretKeyLen;

    // Session token - NULL terminated
    PCHAR sessionToken;

    // Length of the session token - not including NULL terminator
    UINT32 sessionTokenLen;

    // Expiration in absolute time in 100ns.
    UINT64 expiration;

    // The rest of the data might follow the structure
};
typedef struct __AwsCredentials* PAwsCredentials;

typedef struct __AwsCredentialProvider* PAwsCredentialProvider;

/**
 * Function returning AWS credentials
 */
typedef STATUS (*GetCredentialsFunc)(PAwsCredentialProvider, PAwsCredentials*);

/**
 * Abstract base for the credential provider
 */
typedef struct __AwsCredentialProvider AwsCredentialProvider;
struct __AwsCredentialProvider {
    // Get credentials function which will be overwritten by different implementations
    GetCredentialsFunc getCredentialsFn;
};

////////////////////////////////////////////////////
// Public functions
////////////////////////////////////////////////////

#include <util.h>
#include <version.h>

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_COMMON_INCLUDE__ */
