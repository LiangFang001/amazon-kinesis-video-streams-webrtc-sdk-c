#define LOG_CLASS "RequestInfo"
#include "Include_i.h"
#include "request_info.h"

STATUS request_info_create(PCHAR url, PCHAR body, PCHAR region, PCHAR certPath, PCHAR sslCertPath, PCHAR sslPrivateKeyPath,
                           SSL_CERTIFICATE_TYPE certType, PCHAR userAgent, UINT64 connectionTimeout, UINT64 completionTimeout, UINT64 lowSpeedLimit,
                           UINT64 lowSpeedTimeLimit, PAwsCredentials pAwsCredentials, PRequestInfo* ppRequestInfo)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PRequestInfo pRequestInfo = NULL;
    UINT32 size = SIZEOF(RequestInfo), bodySize = 0;

    CHK(region != NULL && url != NULL && ppRequestInfo != NULL, STATUS_NULL_ARG);

    // Add body to the size excluding NULL terminator
    if (body != NULL) {
        bodySize = (UINT32)(STRLEN(body) * SIZEOF(CHAR));
        size += bodySize;
    }

    // Allocate the entire structure
    pRequestInfo = (PRequestInfo) MEMCALLOC(1, size);
    CHK(pRequestInfo != NULL, STATUS_NOT_ENOUGH_MEMORY);

    pRequestInfo->pAwsCredentials = pAwsCredentials;
    pRequestInfo->verb = HTTP_REQUEST_VERB_POST;
    pRequestInfo->completionTimeout = completionTimeout;
    pRequestInfo->connectionTimeout = connectionTimeout;
    ATOMIC_STORE_BOOL(&pRequestInfo->terminating, FALSE);
    pRequestInfo->bodySize = bodySize;
    pRequestInfo->currentTime = GETTIME();
    pRequestInfo->callAfter = pRequestInfo->currentTime;
    STRNCPY(pRequestInfo->region, region, MAX_REGION_NAME_LEN);
    STRNCPY(pRequestInfo->url, url, MAX_URI_CHAR_LEN);
    if (certPath != NULL) {
        STRNCPY(pRequestInfo->certPath, certPath, MAX_PATH_LEN);
    }

    if (sslCertPath != NULL) {
        STRNCPY(pRequestInfo->sslCertPath, sslCertPath, MAX_PATH_LEN);
    }

    if (sslPrivateKeyPath != NULL) {
        STRNCPY(pRequestInfo->sslPrivateKeyPath, sslPrivateKeyPath, MAX_PATH_LEN);
    }

    pRequestInfo->certType = certType;
    pRequestInfo->lowSpeedLimit = lowSpeedLimit;
    pRequestInfo->lowSpeedTimeLimit = lowSpeedTimeLimit;

    // If the body is specified then it will be a request/response call
    // Otherwise we are streaming
    if (body != NULL) {
        pRequestInfo->body = (PCHAR)(pRequestInfo + 1);
        MEMCPY(pRequestInfo->body, body, bodySize);
    }

    // Create a list of headers
    CHK_STATUS(single_list_create(&pRequestInfo->pRequestHeaders));

    // Set user agent header
    CHK_STATUS(request_header_set(pRequestInfo, (PCHAR) "user-agent", 0, userAgent, 0));

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        request_info_free(&pRequestInfo);
        pRequestInfo = NULL;
    }

    // Set the return value if it's not NULL
    if (ppRequestInfo != NULL) {
        *ppRequestInfo = pRequestInfo;
    }

    LEAVES();
    return retStatus;
}

STATUS request_info_free(PRequestInfo* ppRequestInfo)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PRequestInfo pRequestInfo = NULL;

    CHK(ppRequestInfo != NULL, STATUS_NULL_ARG);

    pRequestInfo = (PRequestInfo) *ppRequestInfo;

    // Call is idempotent
    CHK(pRequestInfo != NULL, retStatus);

    // Remove and free the headers
    request_header_removeAll(pRequestInfo);

    // Free the header list itself
    single_list_free(pRequestInfo->pRequestHeaders);

    // Release the object
    MEMFREE(pRequestInfo);

    // Set the pointer to NULL
    *ppRequestInfo = NULL;

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS requestRequiresSecureConnection(PCHAR pUrl, PBOOL pSecure)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pUrl != NULL && pSecure != NULL, STATUS_NULL_ARG);
    *pSecure =
        (0 == STRNCMPI(pUrl, HTTPS_SCHEME_NAME, SIZEOF(HTTPS_SCHEME_NAME) - 1) || 0 == STRNCMPI(pUrl, WSS_SCHEME_NAME, SIZEOF(WSS_SCHEME_NAME) - 1));

CleanUp:

    return retStatus;
}

STATUS request_header_create(PCHAR headerName, UINT32 headerNameLen, PCHAR headerValue, UINT32 headerValueLen, PRequestHeader* ppHeader)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 nameLen, valueLen, size;
    PRequestHeader pRequestHeader = NULL;

    CHK(ppHeader != NULL && headerName != NULL && headerValue != NULL, STATUS_NULL_ARG);

    // Calculate the length if needed
    if (headerNameLen == 0) {
        nameLen = (UINT32) STRLEN(headerName);
    } else {
        nameLen = headerNameLen;
    }

    if (headerValueLen == 0) {
        valueLen = (UINT32) STRLEN(headerValue);
    } else {
        valueLen = headerValueLen;
    }

    CHK(nameLen > 0 && valueLen > 0, STATUS_INVALID_ARG);
    CHK(nameLen < MAX_REQUEST_HEADER_NAME_LEN, STATUS_MAX_REQUEST_HEADER_NAME_LEN);
    CHK(valueLen < MAX_REQUEST_HEADER_VALUE_LEN, STATUS_MAX_REQUEST_HEADER_VALUE_LEN);

    size = SIZEOF(RequestHeader) + (nameLen + 1 + valueLen + 1) * SIZEOF(CHAR);

    // Create the request header
    pRequestHeader = (PRequestHeader) MEMALLOC(size);
    CHK(pRequestHeader != NULL, STATUS_NOT_ENOUGH_MEMORY);
    pRequestHeader->nameLen = nameLen;
    pRequestHeader->valueLen = valueLen;

    // Pointing after the structure
    pRequestHeader->pName = (PCHAR)(pRequestHeader + 1);
    pRequestHeader->pValue = pRequestHeader->pName + nameLen + 1;

    MEMCPY(pRequestHeader->pName, headerName, nameLen * SIZEOF(CHAR));
    pRequestHeader->pName[nameLen] = '\0';
    MEMCPY(pRequestHeader->pValue, headerValue, valueLen * SIZEOF(CHAR));
    pRequestHeader->pValue[valueLen] = '\0';

CleanUp:

    if (STATUS_FAILED(retStatus) && pRequestHeader != NULL) {
        MEMFREE(pRequestHeader);
        pRequestHeader = NULL;
    }

    if (ppHeader != NULL) {
        *ppHeader = pRequestHeader;
    }

    return retStatus;
}

STATUS request_header_set(PRequestInfo pRequestInfo, PCHAR headerName, UINT32 headerNameLen, PCHAR headerValue, UINT32 headerValueLen)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 count;
    PSingleListNode pCurNode, pPrevNode = NULL;
    PRequestHeader pRequestHeader = NULL, pCurrentHeader;
    UINT64 item;

    CHK(pRequestInfo != NULL && headerName != NULL && headerValue != NULL, STATUS_NULL_ARG);
    CHK_STATUS(single_list_getNodeCount(pRequestInfo->pRequestHeaders, &count));
    CHK(count < MAX_REQUEST_HEADER_COUNT, STATUS_MAX_REQUEST_HEADER_COUNT);

    CHK_STATUS(request_header_create(headerName, headerNameLen, headerValue, headerValueLen, &pRequestHeader));

    // Iterate through the list and insert in an alpha order
    CHK_STATUS(single_list_getHeadNode(pRequestInfo->pRequestHeaders, &pCurNode));
    while (pCurNode != NULL) {
        CHK_STATUS(single_list_getNodeData(pCurNode, &item));
        pCurrentHeader = (PRequestHeader) item;

        if (STRCMPI(pCurrentHeader->pName, pRequestHeader->pName) > 0) {
            if (pPrevNode == NULL) {
                // Insert at the head
                CHK_STATUS(singleListInsertItemHead(pRequestInfo->pRequestHeaders, (UINT64) pRequestHeader));
            } else {
                CHK_STATUS(singleListInsertItemAfter(pRequestInfo->pRequestHeaders, pPrevNode, (UINT64) pRequestHeader));
            }

            // Early return
            CHK(FALSE, retStatus);
        }

        pPrevNode = pCurNode;

        CHK_STATUS(singleListGetNextNode(pCurNode, &pCurNode));
    }

    // If not inserted then add to the tail
    CHK_STATUS(single_list_insertItemTail(pRequestInfo->pRequestHeaders, (UINT64) pRequestHeader));

CleanUp:

    if (STATUS_FAILED(retStatus) && pRequestHeader != NULL) {
        MEMFREE(pRequestHeader);
    }

    return retStatus;
}

STATUS request_header_remove(PRequestInfo pRequestInfo, PCHAR headerName)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSingleListNode pCurNode;
    PRequestHeader pCurrentHeader = NULL;
    UINT64 item;

    CHK(pRequestInfo != NULL && headerName != NULL, STATUS_NULL_ARG);

    // Iterate through the list and insert in an alpha order
    CHK_STATUS(single_list_getHeadNode(pRequestInfo->pRequestHeaders, &pCurNode));
    while (pCurNode != NULL) {
        CHK_STATUS(single_list_getNodeData(pCurNode, &item));
        pCurrentHeader = (PRequestHeader) item;

        if (STRCMPI(pCurrentHeader->pName, headerName) == 0) {
            CHK_STATUS(single_list_deleteNode(pRequestInfo->pRequestHeaders, pCurNode));

            // Early return
            CHK(FALSE, retStatus);
        }

        CHK_STATUS(singleListGetNextNode(pCurNode, &pCurNode));
    }

CleanUp:

    SAFE_MEMFREE(pCurrentHeader);

    return retStatus;
}

STATUS request_header_removeAll(PRequestInfo pRequestInfo)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSingleListNode pNode;
    UINT32 itemCount;
    PRequestHeader pRequestHeader;

    CHK(pRequestInfo != NULL, STATUS_NULL_ARG);

    single_list_getNodeCount(pRequestInfo->pRequestHeaders, &itemCount);
    while (itemCount-- != 0) {
        // Remove and delete the data
        single_list_getHeadNode(pRequestInfo->pRequestHeaders, &pNode);
        pRequestHeader = (PRequestHeader) pNode->data;
        SAFE_MEMFREE(pRequestHeader);

        // Iterate
        single_list_deleteHead(pRequestInfo->pRequestHeaders);
    }

CleanUp:

    return retStatus;
}

HTTP_STATUS_CODE getServiceCallResultFromHttpStatus(UINT32 httpStatus)
{
    switch (httpStatus) {
        case HTTP_STATUS_OK:
        case HTTP_STATUS_NOT_ACCEPTABLE:
        case HTTP_STATUS_NOT_FOUND:
        case HTTP_STATUS_FORBIDDEN:
        case HTTP_STATUS_RESOURCE_DELETED:
        case HTTP_STATUS_UNAUTHORIZED:
        case HTTP_STATUS_NOT_IMPLEMENTED:
        case HTTP_STATUS_INTERNAL_SERVER_ERROR:
        case HTTP_STATUS_REQUEST_TIMEOUT:
        case HTTP_STATUS_GATEWAY_TIMEOUT:
        case HTTP_STATUS_NETWORK_READ_TIMEOUT:
        case HTTP_STATUS_NETWORK_CONNECTION_TIMEOUT:
            return (HTTP_STATUS_CODE) httpStatus;
        default:
            return HTTP_STATUS_UNKNOWN;
    }
}
