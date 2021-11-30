/*******************************************
Request Info internal header
*******************************************/
#ifndef __KINESIS_VIDEO_REQUEST_INFO_INCLUDE_I__
#define __KINESIS_VIDEO_REQUEST_INFO_INCLUDE_I__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

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
    SERVICE_CALL_RESULT callResult;

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
 * Creates a Request Info object
 *
 * @param - PCHAR - IN - URL of the request
 * @param - PCHAR - IN/OPT - Body of the request
 * @param - PCHAR - IN - Region
 * @param - PCHAR - IN/OPT - CA Certificate path/file
 * @param - PCHAR - IN/OPT - SSL Certificate path/file
 * @param - PCHAR - IN/OPT - SSL Certificate private key file path
 * @param - SSL_CERTIFICATE_TYPE - IN/OPT - SSL certificate file type
 * @param - PCHAR - IN/OPT - User agent string
 * @param - UINT64 - IN - Connection timeout
 * @param - UINT64 - IN - Completion timeout
 * @param - UINT64 - IN/OPT - Low speed limit
 * @param - UINT64 - IN/OPT - Low speed time limit
 * @param - PAwsCredentials - IN/OPT - Credentials to use for the call
 * @param - PRequestInfo* - IN/OUT - The newly created object
 *
 * @return - STATUS code of the execution
 */
STATUS createRequestInfo(PCHAR, PCHAR, PCHAR, PCHAR, PCHAR, PCHAR, SSL_CERTIFICATE_TYPE, PCHAR, UINT64, UINT64, UINT64, UINT64, PAwsCredentials,
                         PRequestInfo*);

/**
 * Frees a Request Info object
 *
 * @param - PRequestInfo* - IN/OUT - The object to release
 *
 * @return - STATUS code of the execution
 */
STATUS freeRequestInfo(PRequestInfo*);

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
 * Sets a header in the request info
 *
 * @param - PRequestInfo - IN - Request Info object
 * @param - PCHAR - IN - Header name
 * @param - UINT32 - IN/OPT - Header name length. Calculated if 0
 * @param - PCHAR - IN - Header value
 * @param - UINT32 - IN/OPT - Header value length. Calculated if 0
 *
 * @return - STATUS code of the execution
 */
STATUS setRequestHeader(PRequestInfo, PCHAR, UINT32, PCHAR, UINT32);
/**
 * Removes a header from the headers list if exists
 *
 * @param - PRequestInfo - IN - Request Info object
 * @param - PCHAR - IN - Header name to check and remove
 *
 * @return - STATUS code of the execution
 */
STATUS removeRequestHeader(PRequestInfo, PCHAR);
/**
 * Removes and deletes all headers
 *
 * @param - PRequestInfo - IN - Request Info object
 *
 * @return - STATUS code of the execution
 */
STATUS removeRequestHeaders(PRequestInfo);
/**
 * Creates a request header
 *
 * @param - PCHAR - IN - Header name
 * @param - UINT32 - IN - Header name length
 * @param - PCHAR - IN - Header value
 * @param - UINT32 - IN - Header value length
 * @param - PRequestHeader* - OUT - Resulting object
 *
 * @return
 */
STATUS createRequestHeader(PCHAR, UINT32, PCHAR, UINT32, PRequestHeader*);
/**
 * Convenience method to convert HTTP statuses to SERVICE_CALL_RESULT status.
 *
 * @param - UINT32 - http_status the HTTP status code of the call
 *
 * @return The HTTP status translated into a SERVICE_CALL_RESULT value.
 */
SERVICE_CALL_RESULT getServiceCallResultFromHttpStatus(UINT32);
/**
 * Releases the CallInfo allocations
 *
 * @param - PCallInfo - IN - Call info object to release
 *
 * @return - STATUS code of the execution
 */
STATUS releaseCallInfo(PCallInfo);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_REQUEST_INFO_INCLUDE_I__ */
