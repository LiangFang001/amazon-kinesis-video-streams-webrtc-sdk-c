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
#ifndef __KINESIS_VIDEO_REQUEST_INFO_INCLUDE_I__
#define __KINESIS_VIDEO_REQUEST_INFO_INCLUDE_I__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "kvs/common_defs.h"
#include "kvs/common.h"
/**
 * Request Header structure
 */
typedef struct __RequestHeader RequestHeader;
struct __RequestHeader {
    // Request header name
    PCHAR pName;

    // Header name length
    UINT32 nameLen;

    // Request header value
    PCHAR pValue;

    // Header value length
    UINT32 valueLen;
};
typedef struct __RequestHeader* PRequestHeader;

/**
 * Request info structure
 */
typedef struct __RequestInfo {
    // Indicating whether the request is being terminated
    volatile ATOMIC_BOOL terminating;

    // HTTP verb
    HTTP_REQUEST_VERB verb;

    // Body of the request.
    // NOTE: In streaming mode the body will be NULL
    // NOTE: The body will follow the main struct
    PCHAR body;

    // Size of the body in bytes
    UINT32 bodySize;

    // The URL for the request
    CHAR url[MAX_URI_CHAR_LEN + 1];

    // CA Certificate path to use - optional
    CHAR certPath[MAX_PATH_LEN + 1];

    // SSL Certificate file path to use - optional
    CHAR sslCertPath[MAX_PATH_LEN + 1];

    // SSL Certificate private key file path to use - optional
    CHAR sslPrivateKeyPath[MAX_PATH_LEN + 1];

    // One of the following types "DER", "PEM", "ENG"
    SSL_CERTIFICATE_TYPE certType;

    // Region
    CHAR region[MAX_REGION_NAME_LEN + 1];

    // Current time when request was created
    UINT64 currentTime;

    // Call completion timeout
    UINT64 completionTimeout;

    // Connection completion timeout
    UINT64 connectionTimeout;

    // Call after time
    UINT64 callAfter;

    // Low-speed limit
    UINT64 lowSpeedLimit;

    // Low-time limit
    UINT64 lowSpeedTimeLimit;

    // AWS Credentials
    PAwsCredentials pAwsCredentials;

    // Request headers
    PSingleList pRequestHeaders;
} RequestInfo, *PRequestInfo;

/**
 * Call Info structure
 */
typedef struct __CallInfo {
    // Original request info
    PRequestInfo pRequestInfo;

    // HTTP status code of the execution
    UINT32 httpStatus;

    // Execution result
    HTTP_STATUS_CODE callResult;

    // Error buffer for curl calls
    CHAR errorBuffer[CALL_INFO_ERROR_BUFFER_LEN + 1];

    // Response Headers list
    PStackQueue pResponseHeaders;

    // Request ID if specified
    PRequestHeader pRequestId;

    // Buffer to write the data to - will be allocated. Buffer is freed by a caller.
    PCHAR responseData;

    // Response data size
    UINT32 responseDataLen;
} CallInfo, *PCallInfo;

/**
 * @brief Creates a Request Info object
 *
 * @param[in] PCHAR URL of the request
 * @param[in, out] PCHAR Body of the request
 * @param[in] CHAR Region
 * @param[in, out] PCHAR CA Certificate path/file
 * @param[in, out] PCHAR SSL Certificate path/file
 * @param[in, out] PCHAR SSL Certificate private key file path
 * @param[in, out] SSL_CERTIFICATE_TYPE  SSL certificate file type
 * @param[in, out] PCHAR  User agent string
 * @param[in] UINT64  Connection timeout
 * @param[in] UINT64  Completion timeout
 * @param[in, out] UINT64 Low speed limit
 * @param[in, out] UINT64 Low speed time limit
 * @param[in, out] PAwsCredentials Credentials to use for the call
 * @param[in, out] PRequestInfo* The newly created object
 *
 * @return STATUS code of the execution
 */
STATUS createRequestInfo(PCHAR url, PCHAR body, PCHAR region, PCHAR certPath, PCHAR sslCertPath, PCHAR sslPrivateKeyPath,
                         SSL_CERTIFICATE_TYPE certType, PCHAR userAgent, UINT64 connectionTimeout, UINT64 completionTimeout, UINT64 lowSpeedLimit,
                         UINT64 lowSpeedTimeLimit, PAwsCredentials pAwsCredentials, PRequestInfo* ppRequestInfo);

/**
 * @brief Frees a Request Info object
 *
 * @param[in, out] PRequestInfo* The object to release
 *
 * @return STATUS code of the execution
 */
STATUS freeRequestInfo(PRequestInfo* ppRequestInfo);

/**
 * @brief Checks whether the request URL requires a secure connection
 *
 * @param[in] pUrl Request URL
 * @param[in, out] pSecure returned value
 *
 * @return STATUS code of the execution
 */
STATUS requestRequiresSecureConnection(PCHAR pUrl, PBOOL pSecure);
/**
 * @brief Sets a header in the request info
 *
 * @param[in] pRequestInfo Request Info object
 * @param[in] headerName Header name
 * @param[in, out] headerNameLen Header name length. Calculated if 0
 * @param[in] headerValue Header value
 * @param[in, out] headerValueLen Header value length. Calculated if 0
 *
 * @return STATUS code of the execution.
 */
STATUS setRequestHeader(PRequestInfo pRequestInfo, PCHAR headerName, UINT32 headerNameLen, PCHAR headerValue, UINT32 headerValueLen);
/**
 * @brief Removes a header from the headers list if exists
 *
 * @param[in] PRequestInfo Request Info object
 * @param[in] PCHAR Header name to check and remove
 *
 * @return STATUS code of the execution.
 */
STATUS removeRequestHeader(PRequestInfo pRequestInfo, PCHAR headerName);
/**
 * @brief Removes and deletes all headers
 *
 * @param[in] PRequestInfo Request Info object
 *
 * @return STATUS code of the execution.
 */
STATUS removeRequestHeaders(PRequestInfo pRequestInfo);
/**
 * @brief Creates a request header
 *
 * @param[in] headerName Header name
 * @param[in] Header name length
 * @param[in] Header value
 * @param[in] Header value length
 * @param[in, out] Resulting object
 *
 * @return STATUS code of the execution.
 */
STATUS createRequestHeader(PCHAR headerName, UINT32 headerNameLen, PCHAR headerValue, UINT32 headerValueLen, PRequestHeader* ppHeader);
/**
 * @brief Convenience method to convert HTTP statuses to HTTP_STATUS_CODE status.
 *
 * @param[in] http_status the HTTP status code of the call
 *
 * @return The HTTP status translated into a HTTP_STATUS_CODE value.
 */
HTTP_STATUS_CODE getServiceCallResultFromHttpStatus(UINT32);
/**
 * @brief Releases the CallInfo allocations
 *
 * @param[in] PCallInfo Call info object to release
 *
 * @return STATUS code of the execution
 */
STATUS releaseCallInfo(PCallInfo);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_REQUEST_INFO_INCLUDE_I__ */
