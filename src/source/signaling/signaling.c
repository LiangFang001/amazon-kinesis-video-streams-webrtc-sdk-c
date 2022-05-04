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
#define LOG_CLASS "Signaling"
// this is for pdPASS, #TBD.
#include <sys/socket.h>
#include "logger.h"
#include "channel_info.h"
#include "signaling_cache.h"
#include "signaling_fsm.h"
#include "signaling.h"
#include "base64.h"
#include "wss_api.h"
#include "http_api.h"

/******************************************************************************
 * DEFINITION
 ******************************************************************************/
#define SIGNALING_ENTER()  // ENTER()
#define SIGNALING_LEAVE()  // LEAVE()
#define SIGNALING_ENTERS() // ENTERS()
#define SIGNALING_LEAVES() // LEAVES()

/******************************************************************************
 * INTERNAL FUNCTION PROTOTYPE
 ******************************************************************************/
STATUS signaling_validateCallbacks(PSignalingClient, PSignalingClientCallbacks);
STATUS signaling_validateClientInfo(PSignalingClient, PSignalingClientInfoInternal);
STATUS signaling_storeOutboundMessage(PSignalingClient, PSignalingMessage);
STATUS signaling_refreshIceConfiguration(PSignalingClient pSignalingClient);
/******************************************************************************
 * FUNCTION
 ******************************************************************************/
/**
 * @brief
 *
 * @param[in] pArgs the argument of this thread handler.
 *
 * @return STATUS status of execution.
 */
STATUS signaling_getMessageTypeFromString(PCHAR typeStr, UINT32 typeLen, SIGNALING_MESSAGE_TYPE* pMessageType)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 len;

    CHK(typeStr != NULL && pMessageType != NULL, STATUS_WSS_API_NULL_ARG);

    if (typeLen == 0) {
        len = (UINT32) STRLEN(typeStr);
    } else {
        len = typeLen;
    }

    if (0 == STRNCMP(typeStr, SIGNALING_SDP_TYPE_OFFER, len)) {
        *pMessageType = SIGNALING_MESSAGE_TYPE_OFFER;
    } else if (0 == STRNCMP(typeStr, SIGNALING_SDP_TYPE_ANSWER, len)) {
        *pMessageType = SIGNALING_MESSAGE_TYPE_ANSWER;
    } else if (0 == STRNCMP(typeStr, SIGNALING_ICE_CANDIDATE, len)) {
        *pMessageType = SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE;
    } else if (0 == STRNCMP(typeStr, SIGNALING_GO_AWAY, len)) {
        *pMessageType = SIGNALING_MESSAGE_TYPE_GO_AWAY;
    } else if (0 == STRNCMP(typeStr, SIGNALING_RECONNECT_ICE_SERVER, len)) {
        *pMessageType = SIGNALING_MESSAGE_TYPE_RECONNECT_ICE_SERVER;
    } else if (0 == STRNCMP(typeStr, SIGNALING_STATUS_RESPONSE, len)) {
        *pMessageType = SIGNALING_MESSAGE_TYPE_STATUS_RESPONSE;
    } else {
        *pMessageType = SIGNALING_MESSAGE_TYPE_UNKNOWN;
        CHK_WARN(FALSE, retStatus, "Unrecognized message type received");
    }

CleanUp:

    LEAVES();
    return retStatus;
}
/**
 * @brief the thread handler of handling the wss messages.
 *
 * @param[in] pArgs the argument of this thread handler.
 *
 * @return STATUS status of execution.
 */
static PVOID signaling_handleMsg(PVOID pArgs)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = (PSignalingClient) pArgs;
    PSignalingMessageWrapper pMsg;
    BOOL connected;

    DLOGD("The thread of handling msg is up");
    while (!ATOMIC_LOAD_BOOL(&pSignalingClient->shutdownWssDispatch)) {
        BaseType_t err = xQueueReceive(pSignalingClient->inboundMsqQ, &pMsg, 50 / portTICK_PERIOD_MS);

        if (err == pdPASS) {
            DLOGD("Handling wss msg");

            PSignalingClient pSignalingClient = NULL;
            retStatus = STATUS_SUCCESS;

            CHK(pMsg != NULL, STATUS_SIGNALING_NULL_MSG);
            pSignalingClient = pMsg->pSignalingClient;
            CHK(pSignalingClient != NULL, STATUS_SIGNALING_INTERNAL_ERROR);

            switch (pMsg->receivedSignalingMessage.signalingMessage.messageType) {
                case SIGNALING_MESSAGE_TYPE_OFFER:
                    CHK(pMsg->receivedSignalingMessage.signalingMessage.peerClientId[0] != '\0', STATUS_SIGNALING_NO_PEER_CLIENT_ID_IN_MESSAGE);
                    // Explicit fall-through !!!
                case SIGNALING_MESSAGE_TYPE_ANSWER:
                case SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE:
                    CHK(pMsg->receivedSignalingMessage.signalingMessage.payloadLen > 0 &&
                            pMsg->receivedSignalingMessage.signalingMessage.payloadLen <= MAX_SIGNALING_MESSAGE_LEN,
                        STATUS_SIGNALING_INVALID_PAYLOAD_LEN_IN_MESSAGE);
                    CHK(pMsg->receivedSignalingMessage.signalingMessage.payload[0] != '\0', STATUS_SIGNALING_NO_PAYLOAD_IN_MESSAGE);

                    // Calling client receive message callback if specified
                    if (pSignalingClient->signalingClientCallbacks.messageReceivedFn != NULL) {
                        CHK_STATUS(pSignalingClient->signalingClientCallbacks.messageReceivedFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                                                &pMsg->receivedSignalingMessage));
                    }
                    break;

                case SIGNALING_MESSAGE_TYPE_STATUS_RESPONSE:
                    if (pMsg->receivedSignalingMessage.statusCode != HTTP_STATUS_OK) {
                        DLOGW("Failed to deliver message. Correlation ID: %s, Error Type: %s, Error Code: %u, Description: %s",
                              pMsg->receivedSignalingMessage.signalingMessage.correlationId, pMsg->receivedSignalingMessage.errorType,
                              pMsg->receivedSignalingMessage.statusCode, pMsg->receivedSignalingMessage.description);
                    }

                    CHK_STATUS(wss_api_disconnect(pSignalingClient));
                    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_UNKNOWN);
                    ATOMIC_STORE_BOOL(&pSignalingClient->connected, FALSE);
                    CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));
                    CHK(FALSE, retStatus);
                    break;

                case SIGNALING_MESSAGE_TYPE_GO_AWAY:
                    DLOGD("Wss msg type:%d", pMsg->receivedSignalingMessage.signalingMessage.messageType);
                    CHK_STATUS(wss_api_disconnect(pSignalingClient));
                    ATOMIC_STORE_BOOL(&pSignalingClient->connected, FALSE);
                    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_SIGNALING_GO_AWAY);
                    CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));
                    CHK(FALSE, retStatus);
                    break;

                case SIGNALING_MESSAGE_TYPE_RECONNECT_ICE_SERVER:
                    DLOGD("Wss msg type:%d", pMsg->receivedSignalingMessage.signalingMessage.messageType);
                    CHK_STATUS(wss_api_disconnect(pSignalingClient));
                    ATOMIC_STORE_BOOL(&pSignalingClient->connected, FALSE);
                    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_SIGNALING_RECONNECT_ICE);
                    CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));
                    CHK(FALSE, retStatus);
                    break;
                case SIGNALING_MESSAGE_TYPE_CTRL_CLOSE:
                    DLOGD("Wss msg type:%d", pMsg->receivedSignalingMessage.signalingMessage.messageType);
                    CHK_STATUS(wss_api_disconnect(pSignalingClient));
                    ATOMIC_STORE_BOOL(&pSignalingClient->connected, FALSE);
                    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_UNKNOWN);
                    CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));
                    // #TBD
                    ATOMIC_INCREMENT(&pSignalingClient->diagnostics.numberOfReconnects);
                    CHK(FALSE, retStatus);
                    break;
                default:
                    DLOGW("Unknown wss msg:%d", pMsg->receivedSignalingMessage.signalingMessage.messageType);
                    break;
            }

        CleanUp:
            CHK_LOG_ERR(retStatus);
            SAFE_MEMFREE(pMsg);
        }
    }

    DLOGD("The thread of handling msg is down");
    THREAD_EXIT(NULL);
    return (PVOID)(ULONG_PTR) retStatus;
}

static STATUS signaling_dispatchMsg(PVOID pMessage)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingMessageWrapper pMsg = (PSignalingMessageWrapper) pMessage;
    PSignalingClient pSignalingClient = NULL;
    UBaseType_t num = 0;
    CHK(pMsg != NULL, STATUS_SIGNALING_NULL_MSG);

    pSignalingClient = pMsg->pSignalingClient;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_INTERNAL_ERROR);
    // Updating the diagnostics info before calling the client callback
    ATOMIC_INCREMENT(&pSignalingClient->diagnostics.numberOfMessagesReceived);
    CHK(IS_VALID_TID_VALUE(pSignalingClient->dispatchMsgTid), STATUS_SIGNALING_NO_DISPATCHER);
    CHK(pSignalingClient->inboundMsqQ != NULL, STATUS_SIGNALING_NO_INBOUND_MSGQ);
    CHK((num = uxQueueSpacesAvailable(pSignalingClient->inboundMsqQ)) > 0, STATUS_SIGNALING_INBOUND_MSGQ_OVERFLOW);
    DLOGD("Queued wss msg: %d", WSS_INBOUND_MSGQ_LENGTH - num);
    CHK(xQueueSend(pSignalingClient->inboundMsqQ, &pMsg, 0) == pdPASS, STATUS_SIGNALING_DISPATCH_FAILED);

CleanUp:
    CHK_LOG_ERR(retStatus);
    if (STATUS_FAILED(retStatus)) {
        SAFE_MEMFREE(pMsg);
    }

    return retStatus;
}

STATUS signaling_create(PSignalingClientInfoInternal pClientInfo, PChannelInfo pChannelInfo, PSignalingClientCallbacks pCallbacks,
                        PAwsCredentialProvider pCredentialProvider, PSignalingClient* ppSignalingClient)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = NULL;
    PCHAR userLogLevelStr = NULL;
    UINT32 userLogLevel;
    BOOL cacheFound = FALSE;
    PSignalingFileCacheEntry pFileCacheEntry = NULL;

    // exception check
    CHK(pClientInfo != NULL && pChannelInfo != NULL && pCallbacks != NULL && pCredentialProvider != NULL && ppSignalingClient != NULL,
        STATUS_SIGNALING_NULL_ARG);
    CHK(pChannelInfo->version <= CHANNEL_INFO_CURRENT_VERSION, STATUS_SIGNALING_INVALID_CHANNEL_INFO_VERSION);

    // Allocate enough storage
    CHK(NULL != (pFileCacheEntry = (PSignalingFileCacheEntry) MEMALLOC(SIZEOF(SignalingFileCacheEntry))), STATUS_SIGNALING_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pSignalingClient = (PSignalingClient) MEMCALLOC(1, SIZEOF(SignalingClient))), STATUS_SIGNALING_NOT_ENOUGH_MEMORY);

    // Validate and store the input
    CHK_STATUS(channel_info_create(pChannelInfo, &pSignalingClient->pChannelInfo));
    CHK_STATUS(signaling_validateCallbacks(pSignalingClient, pCallbacks));
    CHK_STATUS(signaling_validateClientInfo(pSignalingClient, pClientInfo));

    // Set invalid call times
    pSignalingClient->apiCallHistory.describeTime = INVALID_TIMESTAMP_VALUE;
    pSignalingClient->apiCallHistory.createTime = INVALID_TIMESTAMP_VALUE;
    pSignalingClient->apiCallHistory.getEndpointTime = INVALID_TIMESTAMP_VALUE;
    pSignalingClient->apiCallHistory.getIceConfigTime = INVALID_TIMESTAMP_VALUE;
    pSignalingClient->apiCallHistory.deleteTime = INVALID_TIMESTAMP_VALUE;
    pSignalingClient->apiCallHistory.connectTime = INVALID_TIMESTAMP_VALUE;
    pSignalingClient->pDispatchMsgHandler = signaling_dispatchMsg;

    ATOMIC_STORE_BOOL(&pSignalingClient->shutdownWssDispatch, FALSE);
    pSignalingClient->inboundMsqQ = xQueueCreate(WSS_INBOUND_MSGQ_LENGTH, SIZEOF(PSignalingMessageWrapper));
    CHK(pSignalingClient->inboundMsqQ != NULL, STATUS_SIGNALING_CREATE_MSGQ_FAILED);
    CHK(THREAD_CREATE_EX(&pSignalingClient->dispatchMsgTid, WSS_DISPATCH_THREAD_NAME, WSS_DISPATCH_THREAD_SIZE, TRUE, signaling_handleMsg,
                         (PVOID) pSignalingClient) == STATUS_SUCCESS,
        STATUS_SIGNALING_CREATE_DISPATCHER_FAILED);

    // load the previous cached information of endpoint.
    if (pSignalingClient->pChannelInfo->cachingPolicy == SIGNALING_API_CALL_CACHE_TYPE_FILE) {
        // Signaling channel name can be NULL in case of pre-created channels in which case we use ARN as the name
        if (STATUS_FAILED(signaling_cache_loadFromFile(pChannelInfo->pChannelName != NULL ? pChannelInfo->pChannelName : pChannelInfo->pChannelArn,
                                                       pChannelInfo->pRegion, pChannelInfo->channelRoleType, pFileCacheEntry, &cacheFound))) {
            DLOGW("Failed to load signaling cache from file");
        } else if (cacheFound) {
            STRNCPY(pSignalingClient->channelDescription.channelArn, pFileCacheEntry->channelArn, MAX_ARN_LEN);
            STRNCPY(pSignalingClient->channelDescription.channelEndpointHttps, pFileCacheEntry->httpsEndpoint, MAX_SIGNALING_ENDPOINT_URI_LEN);
            STRNCPY(pSignalingClient->channelDescription.channelEndpointWss, pFileCacheEntry->wssEndpoint, MAX_SIGNALING_ENDPOINT_URI_LEN);
            pSignalingClient->apiCallHistory.describeTime = pFileCacheEntry->creationTsEpochSeconds * HUNDREDS_OF_NANOS_IN_A_SECOND;
            pSignalingClient->apiCallHistory.getEndpointTime = pFileCacheEntry->creationTsEpochSeconds * HUNDREDS_OF_NANOS_IN_A_SECOND;
        }
    }

    // Store the credential provider
    pSignalingClient->pCredentialProvider = pCredentialProvider;
    // Create the state machine
    CHK_STATUS(signaling_fsm_create(pSignalingClient, &pSignalingClient->signalingFsmHandle));

    ATOMIC_STORE_BOOL(&pSignalingClient->shutdown, FALSE);
    ATOMIC_STORE_BOOL(&pSignalingClient->connected, FALSE);
    pSignalingClient->connecting = FALSE;
    pSignalingClient->reconnect = pChannelInfo->reconnect;
    // Do not force ice config state
    ATOMIC_STORE_BOOL(&pSignalingClient->refreshIceConfig, FALSE);

    // Create the sync primitives
    pSignalingClient->nestedFsmLock = MUTEX_CREATE(TRUE);
    CHK(IS_VALID_MUTEX_VALUE(pSignalingClient->nestedFsmLock), STATUS_INVALID_OPERATION);

    pSignalingClient->outboundMsgQLock = MUTEX_CREATE(TRUE);
    CHK(IS_VALID_MUTEX_VALUE(pSignalingClient->outboundMsgQLock), STATUS_INVALID_OPERATION);

    pSignalingClient->diagnosticsLock = MUTEX_CREATE(TRUE);
    CHK(IS_VALID_MUTEX_VALUE(pSignalingClient->diagnosticsLock), STATUS_INVALID_OPERATION);

    pSignalingClient->wssContextLock = MUTEX_CREATE(TRUE);
    CHK(IS_VALID_MUTEX_VALUE(pSignalingClient->wssContextLock), STATUS_INVALID_OPERATION);

    // Create the ongoing message list
    CHK_STATUS(stack_queue_create(&pSignalingClient->pOutboundMsgQ));

    // Initializing the diagnostics mostly is taken care of by zero-mem in MEMCALLOC
    pSignalingClient->diagnostics.createTime = GETTIME();

    // At this point we have constructed the main object and we can assign to the returned pointer
    *ppSignalingClient = pSignalingClient;

    // Set the time out before execution
    pSignalingClient->stepUntil = pSignalingClient->diagnostics.createTime + SIGNALING_CREATE_TIMEOUT;

    // Notify of the state change initially as the state machinery is already in the NEW state
    if (pSignalingClient->signalingClientCallbacks.stateChangeFn != NULL) {
        CHK_STATUS(pSignalingClient->signalingClientCallbacks.stateChangeFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                            signaling_getCurrentState(pSignalingClient)));
    }

    // Prime the state machine
    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_NONE);
    CHK_STATUS(signaling_fsm_step(pSignalingClient, STATUS_SUCCESS));

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (STATUS_FAILED(retStatus)) {
        signaling_free(&pSignalingClient);
    }

    if (ppSignalingClient != NULL) {
        *ppSignalingClient = pSignalingClient;
    }
    SAFE_MEMFREE(pFileCacheEntry);
    LEAVES();
    return retStatus;
}

STATUS signaling_free(PSignalingClient* ppSignalingClient)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient;

    CHK(ppSignalingClient != NULL, STATUS_SIGNALING_NULL_ARG);
    pSignalingClient = *ppSignalingClient;
    CHK(pSignalingClient != NULL, retStatus);

    ATOMIC_STORE_BOOL(&pSignalingClient->shutdown, TRUE);
    pSignalingClient->reconnect = FALSE;

    // termination wss connection.
    DLOGD("Closing the wss client.");
    wss_api_disconnect(pSignalingClient);
    DLOGD("The wss client is done.");
    ATOMIC_STORE_BOOL(&pSignalingClient->connected, FALSE);
    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_OK);

    if (IS_VALID_TID_VALUE(pSignalingClient->dispatchMsgTid)) {
        DLOGD("Waiting wss dispatcher done.");
        ATOMIC_STORE_BOOL(&pSignalingClient->shutdownWssDispatch, TRUE);
        THREAD_JOIN(pSignalingClient->dispatchMsgTid, NULL);
        pSignalingClient->dispatchMsgTid = INVALID_TID_VALUE;
        DLOGD("The wss dispatcher is done.");
    }

    if (pSignalingClient->inboundMsqQ != NULL) {
        DLOGD("Delete the queue of msg");
        vQueueDelete(pSignalingClient->inboundMsqQ);
        pSignalingClient->inboundMsqQ = NULL;
    }

    signaling_fsm_free(pSignalingClient->signalingFsmHandle);
    channel_info_free(&pSignalingClient->pChannelInfo);
    stack_queue_free(pSignalingClient->pOutboundMsgQ);

    if (IS_VALID_MUTEX_VALUE(pSignalingClient->nestedFsmLock)) {
        MUTEX_FREE(pSignalingClient->nestedFsmLock);
    }

    if (IS_VALID_MUTEX_VALUE(pSignalingClient->outboundMsgQLock)) {
        MUTEX_FREE(pSignalingClient->outboundMsgQLock);
    }

    if (IS_VALID_MUTEX_VALUE(pSignalingClient->diagnosticsLock)) {
        MUTEX_FREE(pSignalingClient->diagnosticsLock);
    }

    if (IS_VALID_MUTEX_VALUE(pSignalingClient->wssContextLock)) {
        MUTEX_FREE(pSignalingClient->wssContextLock);
    }

    MEMFREE(pSignalingClient);

    *ppSignalingClient = NULL;

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS signaling_send(PSignalingClient pSignalingClient, PSignalingMessage pSignalingMessage)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PCHAR pOfferType = NULL;
    BOOL removeFromList = FALSE;
    PCHAR pEncodedMessage = NULL;
    UINT32 size, writtenSize, correlationLen;
    PBYTE pSendBuffer = NULL;

    CHK(pSignalingClient != NULL && pSignalingMessage != NULL, STATUS_SIGNALING_NULL_ARG);
    CHK(pSignalingMessage->peerClientId != NULL && pSignalingMessage->payload != NULL, STATUS_SIGNALING_INVALID_ARG);
    CHK(pSignalingMessage->version <= SIGNALING_MESSAGE_CURRENT_VERSION, STATUS_SIGNALING_INVALID_SIGNALING_MESSAGE_VERSION);

    // Prepare the buffer to send
    switch (pSignalingMessage->messageType) {
        case SIGNALING_MESSAGE_TYPE_OFFER:
            pOfferType = (PCHAR) SIGNALING_SDP_TYPE_OFFER;
            break;
        case SIGNALING_MESSAGE_TYPE_ANSWER:
            pOfferType = (PCHAR) SIGNALING_SDP_TYPE_ANSWER;
            break;
        case SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE:
            pOfferType = (PCHAR) SIGNALING_ICE_CANDIDATE;
            break;
        default:
            CHK(FALSE, STATUS_SIGNALING_INVALID_ARG);
    }

    // Ensure we are in a connected state
    CHK(signaling_fsm_accept(pSignalingClient, SIGNALING_STATE_CONNECTED) == STATUS_SUCCESS, STATUS_SIGNALING_FSM_INVALID_STATE);
    CHK(pSignalingClient != NULL && pSignalingClient->pWssContext != NULL, STATUS_SIGNALING_NULL_ARG);
    // allocate related buffers.
    CHK(NULL != (pEncodedMessage = (PCHAR) MEMALLOC(MAX_SESSION_DESCRIPTION_INIT_SDP_LEN + 1)), STATUS_SIGNALING_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pSendBuffer = (PBYTE) MEMALLOC(MAX_SIGNALING_MESSAGE_LEN)), STATUS_SIGNALING_NOT_ENOUGH_MEMORY);

    // Store the signaling message
    CHK_STATUS(signaling_storeOutboundMessage(pSignalingClient, pSignalingMessage));
    removeFromList = TRUE;

    // Calculate the lengths if not specified
    if (pSignalingMessage->payloadLen == 0) {
        size = (UINT32) STRLEN(pSignalingMessage->payload);
    } else {
        size = pSignalingMessage->payloadLen;
    }

    correlationLen = (UINT32) STRLEN(pSignalingMessage->correlationId);

    // Base64 encode the message
    writtenSize = MAX_SESSION_DESCRIPTION_INIT_SDP_LEN + 1;
    CHK_STATUS(base64Encode(pSignalingMessage->payload, size, pEncodedMessage, &writtenSize));

    // Account for the template expansion + Action string + max recipient id
    size = MAX_SIGNALING_MESSAGE_LEN;
    CHK(writtenSize <= size, STATUS_SIGNALING_MAX_MESSAGE_LEN_AFTER_ENCODING);

    // Prepare json message
    if (correlationLen == 0) {
        writtenSize = (UINT32) SNPRINTF((PCHAR)(pSendBuffer), size, WSS_MESSAGE_TEMPLATE, pOfferType, MAX_SIGNALING_CLIENT_ID_LEN,
                                        pSignalingMessage->peerClientId, pEncodedMessage);
    } else {
        writtenSize = (UINT32) SNPRINTF((PCHAR)(pSendBuffer), size, WSS_MESSAGE_TEMPLATE_WITH_CORRELATION_ID, pOfferType, MAX_SIGNALING_CLIENT_ID_LEN,
                                        pSignalingMessage->peerClientId, pEncodedMessage, correlationLen, pSignalingMessage->correlationId);
    }

    // Validate against max
    CHK(writtenSize <= MAX_SIGNALING_MESSAGE_LEN, STATUS_SIGNALING_MAX_MESSAGE_LEN_AFTER_ENCODING);
    writtenSize *= SIZEOF(CHAR);
    CHK(writtenSize <= size, STATUS_SIGNALING_INVALID_ARG);
    // Send the data to the web socket
    CHK(wss_api_send(pSignalingClient, pSendBuffer, writtenSize) == STATUS_SUCCESS, STATUS_SIGNALING_SEND_FAILED);

    // Update the internal diagnostics only after successfully sending
    ATOMIC_INCREMENT(&pSignalingClient->diagnostics.numberOfMessagesSent);

CleanUp:

    CHK_LOG_ERR(retStatus);

    // Remove from the list if previously added
    if (removeFromList) {
        signaling_removeOutboundMessage(pSignalingClient, pSignalingMessage->correlationId);
    }

    SAFE_MEMFREE(pEncodedMessage);
    SAFE_MEMFREE(pSendBuffer);

    LEAVES();
    return retStatus;
}

SIGNALING_CLIENT_STATE signaling_getCurrentState(PSignalingClient pSignalingClient)
{
    STATUS retStatus = STATUS_SUCCESS;
    SIGNALING_CLIENT_STATE clientState = SIGNALING_CLIENT_STATE_UNKNOWN;
    UINT64 state;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    state = signaling_fsm_getCurrentState(pSignalingClient);

    switch (state) {
        case SIGNALING_STATE_NONE:
            clientState = SIGNALING_CLIENT_STATE_UNKNOWN;
            break;
        case SIGNALING_STATE_NEW:
            clientState = SIGNALING_CLIENT_STATE_NEW;
            break;
        case SIGNALING_STATE_GET_TOKEN:
            clientState = SIGNALING_CLIENT_STATE_GET_CREDENTIALS;
            break;
        case SIGNALING_STATE_DESCRIBE:
            clientState = SIGNALING_CLIENT_STATE_DESCRIBE;
            break;
        case SIGNALING_STATE_CREATE:
            clientState = SIGNALING_CLIENT_STATE_CREATE;
            break;
        case SIGNALING_STATE_GET_ENDPOINT:
            clientState = SIGNALING_CLIENT_STATE_GET_ENDPOINT;
            break;
        case SIGNALING_STATE_GET_ICE_CONFIG:
            clientState = SIGNALING_CLIENT_STATE_GET_ICE_CONFIG;
            break;
        case SIGNALING_STATE_READY:
            clientState = SIGNALING_CLIENT_STATE_READY;
            break;
        case SIGNALING_STATE_CONNECT:
            clientState = SIGNALING_CLIENT_STATE_CONNECTING;
            break;
        case SIGNALING_STATE_CONNECTED:
            clientState = SIGNALING_CLIENT_STATE_CONNECTED;
            break;
        case SIGNALING_STATE_DISCONNECTED:
            clientState = SIGNALING_CLIENT_STATE_DISCONNECTED;
            break;
        default:
            clientState = SIGNALING_CLIENT_STATE_UNKNOWN;
    }
CleanUp:
    return clientState;
}

STATUS signaling_getIceConfigInfoCout(PSignalingClient pSignalingClient, PUINT32 pIceConfigCount)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pSignalingClient != NULL && pIceConfigCount != NULL, STATUS_SIGNALING_NULL_ARG);

    CHK_STATUS(signaling_refreshIceConfiguration(pSignalingClient));

    *pIceConfigCount = pSignalingClient->iceConfigCount;

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS signaling_getIceConfigInfo(PSignalingClient pSignalingClient, UINT32 index, PIceConfigInfo* ppIceConfigInfo)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pSignalingClient != NULL && ppIceConfigInfo != NULL, STATUS_SIGNALING_NULL_ARG);
    // Refresh the ICE configuration first
    CHK_STATUS(signaling_refreshIceConfiguration(pSignalingClient));

    CHK(index < pSignalingClient->iceConfigCount, STATUS_SIGNALING_INVALID_ARG);

    *ppIceConfigInfo = &pSignalingClient->iceConfigs[index];

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS signaling_connect(PSignalingClient pSignalingClient)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT64 state = SIGNALING_STATE_NONE;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_NULL_ARG);

    // Validate the state
    CHK(signaling_fsm_accept(pSignalingClient,
                             SIGNALING_STATE_READY | SIGNALING_STATE_CONNECT | SIGNALING_STATE_DISCONNECTED | SIGNALING_STATE_CONNECTED) ==
            STATUS_SUCCESS,
        STATUS_SIGNALING_FSM_INVALID_STATE);

    // Check if we are already connected
    CHK(!ATOMIC_LOAD_BOOL(&pSignalingClient->connected), retStatus);

    // Self-prime through the ready state
    pSignalingClient->connecting = TRUE;

    // Store the signaling state in case we error/timeout so we can re-set it on exit
    state = signaling_fsm_getCurrentState(pSignalingClient);
    // Set the time out before execution
    pSignalingClient->stepUntil = GETTIME() + SIGNALING_CONNECT_STATE_TIMEOUT;

    CHK(signaling_fsm_step(pSignalingClient, retStatus) == STATUS_SUCCESS, STATUS_SIGNALING_FSM_STEP_FAILED);

CleanUp:

    CHK_LOG_ERR(retStatus);

    // Re-set the state if we failed
    if (STATUS_FAILED(retStatus)) {
        signaling_fsm_resetRetryCount(pSignalingClient);
        signaling_fsm_setCurrentState(pSignalingClient, state);
    }

    LEAVES();
    return retStatus;
}

STATUS signaling_disconnect(PSignalingClient pSignalingClient)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_NULL_ARG);

    // Do not self-prime through the ready state
    pSignalingClient->connecting = FALSE;

    // Check if we are already not connected
    CHK(ATOMIC_LOAD_BOOL(&pSignalingClient->connected), retStatus);

    wss_api_disconnect(pSignalingClient);
    ATOMIC_STORE_BOOL(&pSignalingClient->connected, FALSE);
    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_OK);

    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_OK);

    CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS signaling_delete(PSignalingClient pSignalingClient)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_NULL_ARG);

    CHK_STATUS(signaling_disconnect(pSignalingClient));
    //#TBD.
    CHK_STATUS(signaling_channel_delete(pSignalingClient, 0));

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}
/**
 * @brief   validate the version of singaling callbacks.
 *
 * @param[]
 * @param[]
 *
 * @return
 */
// static
STATUS signaling_validateCallbacks(PSignalingClient pSignalingClient, PSignalingClientCallbacks pCallbacks)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pSignalingClient != NULL && pCallbacks != NULL, STATUS_SIGNALING_NULL_ARG);
    CHK(pCallbacks->version <= SIGNALING_CLIENT_CALLBACKS_CURRENT_VERSION, STATUS_SIGNALING_INVALID_SIGNALING_CALLBACKS_VERSION);

    // Store and validate
    pSignalingClient->signalingClientCallbacks = *pCallbacks;

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}
/**
 * @brief   validate the client info and assign it to the context of singling client.
 *
 * @param[in, out]
 * @param[in]
 *
 * @return
 */
STATUS signaling_validateClientInfo(PSignalingClient pSignalingClient, PSignalingClientInfoInternal pClientInfo)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pSignalingClient != NULL && pClientInfo != NULL, STATUS_SIGNALING_NULL_ARG);
    CHK(pClientInfo->signalingClientInfo.version <= SIGNALING_CLIENT_INFO_CURRENT_VERSION, STATUS_SIGNALING_INVALID_CLIENT_INFO_VERSION);
    CHK(STRNLEN(pClientInfo->signalingClientInfo.clientId, MAX_SIGNALING_CLIENT_ID_LEN + 1) <= MAX_SIGNALING_CLIENT_ID_LEN,
        STATUS_SIGNALING_INVALID_CLIENT_INFO_CLIENT_LENGTH);

    // Copy and store internally
    pSignalingClient->clientInfo = *pClientInfo;

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS signaling_validateIceConfiguration(PSignalingClient pSignalingClient)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 i;
    UINT64 minTtl = MAX_UINT64;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_NULL_ARG);
    CHK(pSignalingClient->iceConfigCount <= MAX_ICE_CONFIG_COUNT, STATUS_SIGNALING_MAX_ICE_CONFIG_COUNT);
    CHK(pSignalingClient->iceConfigCount > 0, STATUS_SIGNALING_NO_CONFIG_SPECIFIED);

    for (i = 0; i < pSignalingClient->iceConfigCount; i++) {
        CHK(pSignalingClient->iceConfigs[i].version <= SIGNALING_ICE_CONFIG_INFO_CURRENT_VERSION, STATUS_SIGNALING_INVALID_ICE_CONFIG_INFO_VERSION);
        CHK(pSignalingClient->iceConfigs[i].uriCount > 0, STATUS_SIGNALING_NO_CONFIG_URI_SPECIFIED);
        CHK(pSignalingClient->iceConfigs[i].uriCount <= MAX_ICE_CONFIG_URI_COUNT, STATUS_SIGNALING_MAX_ICE_URI_COUNT);

        minTtl = MIN(minTtl, pSignalingClient->iceConfigs[i].ttl);
    }

    CHK(minTtl > ICE_CONFIGURATION_REFRESH_GRACE_PERIOD, STATUS_SIGNALING_ICE_TTL_LESS_THAN_GRACE_PERIOD);

    pSignalingClient->iceConfigTime = GETTIME();
    pSignalingClient->iceConfigExpiration = pSignalingClient->iceConfigTime + (minTtl - ICE_CONFIGURATION_REFRESH_GRACE_PERIOD);
    DLOGD("The expiration of ice config: %" PRIu64 ", ttl: %" PRIu64, pSignalingClient->iceConfigExpiration, minTtl / HUNDREDS_OF_NANOS_IN_A_SECOND);

CleanUp:
    CHK_LOG_ERR(retStatus);
    LEAVES();
    return retStatus;
}
/**
 * @brief   refresh the information of ice server.
 */
STATUS signaling_refreshIceConfiguration(PSignalingClient pSignalingClient)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    CHAR iceRefreshErrMsg[SIGNALING_MAX_ERROR_MESSAGE_LEN + 1];
    UINT32 iceRefreshErrLen;
    UINT64 curTime;
    UINT64 state = SIGNALING_STATE_NONE;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_NULL_ARG);

    DLOGD("Refreshing the ICE Server Configuration");
    // Check whether we have a valid not-yet-expired ICE configuration and if so early exit
    curTime = GETTIME();
    CHK(pSignalingClient->iceConfigCount == 0 || curTime > pSignalingClient->iceConfigExpiration, retStatus);

    CHK(signaling_fsm_accept(pSignalingClient,
                             SIGNALING_STATE_READY | SIGNALING_STATE_CONNECT | SIGNALING_STATE_CONNECTED | SIGNALING_STATE_DISCONNECTED) ==
            STATUS_SUCCESS,
        STATUS_SIGNALING_FSM_INVALID_STATE);

    // Check if we are in a connect, connected, disconnected or ready states and if not bail.
    // The ICE state will be called in any other states
    state = signaling_fsm_getCurrentState(pSignalingClient);
    CHK(state == SIGNALING_STATE_CONNECT || state == SIGNALING_STATE_CONNECTED || state == SIGNALING_STATE_DISCONNECTED ||
            state == SIGNALING_STATE_READY,
        retStatus);

    // Force the state machine to revert back to get ICE configuration without re-connection
    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_SIGNALING_RECONNECT_ICE);
    ATOMIC_STORE(&pSignalingClient->refreshIceConfig, TRUE);

    // Iterate the state machinery in steady states only - ready or connected
    if (state == SIGNALING_STATE_READY || state == SIGNALING_STATE_CONNECTED) {
        // Set the time out before execution
        pSignalingClient->stepUntil = GETTIME() + SIGNALING_REFRESH_ICE_CONFIG_STATE_TIMEOUT;

        CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    // Notify the client in case of an error
    if (pSignalingClient != NULL && STATUS_FAILED(retStatus)) {
        // Update the diagnostics info prior calling the error callback
        ATOMIC_INCREMENT(&pSignalingClient->diagnostics.numberOfRuntimeErrors);

        // Reset the stored state as we could have been connected prior to the ICE refresh and we still need to be connected
        signaling_fsm_setCurrentState(pSignalingClient, state);

        // Need to invoke the error handler callback
        if (pSignalingClient->signalingClientCallbacks.errorReportFn != NULL) {
            iceRefreshErrLen = SNPRINTF(iceRefreshErrMsg, SIGNALING_MAX_ERROR_MESSAGE_LEN, SIGNALING_ICE_CONFIG_REFRESH_ERROR_MSG, retStatus);
            iceRefreshErrMsg[SIGNALING_MAX_ERROR_MESSAGE_LEN] = '\0';
            pSignalingClient->signalingClientCallbacks.errorReportFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                     STATUS_SIGNALING_ICE_CONFIG_REFRESH_FAILED, iceRefreshErrMsg, iceRefreshErrLen);
        }
    }

    LEAVES();
    return retStatus;
}
/**
 * @brief   store the singaling ongoing message, and also check the duplicated messages.
 *
 * @param[in] pSignalingClient the context of signaling client.
 * @param[in] pSignalingMessage the signaling message.
 *
 * @return STATUS status of execution.
 */
STATUS signaling_storeOutboundMessage(PSignalingClient pSignalingClient, PSignalingMessage pSignalingMessage)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;
    PSignalingMessage pExistingMessage = NULL;

    CHK(pSignalingClient != NULL && pSignalingMessage != NULL, STATUS_SIGNALING_NULL_ARG);
    MUTEX_LOCK(pSignalingClient->outboundMsgQLock);
    locked = TRUE;
    // check the duplicated signaling messages.
    CHK_STATUS(signaling_getOutboundMessage(pSignalingClient, pSignalingMessage->correlationId, pSignalingMessage->peerClientId, &pExistingMessage));
    CHK(pExistingMessage == NULL, STATUS_SIGNALING_DUPLICATE_MESSAGE_BEING_SENT);
    // push it into signaling message queue.
    CHK_STATUS(stack_queue_enqueue(pSignalingClient->pOutboundMsgQ, (UINT64) pSignalingMessage));

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pSignalingClient->outboundMsgQLock);
    }

    LEAVES();
    return retStatus;
}

STATUS signaling_removeOutboundMessage(PSignalingClient pSignalingClient, PCHAR correlationId)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;
    PSignalingMessage pExistingMessage;
    StackQueueIterator iterator;
    UINT64 data;

    CHK(pSignalingClient != NULL && correlationId != NULL, STATUS_SIGNALING_NULL_ARG);
    MUTEX_LOCK(pSignalingClient->outboundMsgQLock);
    locked = TRUE;

    CHK_STATUS(stack_queue_iterator_get(pSignalingClient->pOutboundMsgQ, &iterator));
    while (IS_VALID_ITERATOR(iterator)) {
        CHK_STATUS(stack_queue_iterator_getItem(iterator, &data));

        pExistingMessage = (PSignalingMessage) data;
        CHK(pExistingMessage != NULL, STATUS_SIGNALING_INTERNAL_ERROR);

        if ((correlationId[0] == '\0' && pExistingMessage->correlationId[0] == '\0') || 0 == STRCMP(pExistingMessage->correlationId, correlationId)) {
            // Remove the match
            CHK_STATUS(stack_queue_removeItem(pSignalingClient->pOutboundMsgQ, data));

            // Early return
            CHK(FALSE, retStatus);
        }

        CHK_STATUS(stack_queue_iterator_getNext(&iterator));
    }

    // Didn't find a match
    CHK(FALSE, STATUS_NOT_FOUND);

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pSignalingClient->outboundMsgQLock);
    }

    LEAVES();
    return retStatus;
}
/**
 * @brief   Get the corresponding message with correction id and peer client id.
 *
 * @param[in] pSignalingClient
 * @param[in] correlationId A unique identifier for the message.
 *                          Type: String. Length constraints: Minimum length of 1. Maximum length of 256. Pattern: [a-zA-Z0-9_.-]+. Required: No
 * @param[in] peerClientId A unique identifier for the recipient.
 * @param[in, out] ppSignalingMessage
 *
 * @return STATUS status of execution.
 */
STATUS signaling_getOutboundMessage(PSignalingClient pSignalingClient, PCHAR correlationId, PCHAR peerClientId, PSignalingMessage* ppSignalingMessage)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE, checkPeerClientId = TRUE;
    PSignalingMessage pExistingMessage = NULL;
    StackQueueIterator iterator;
    UINT64 data;

    CHK(pSignalingClient != NULL && correlationId != NULL && ppSignalingMessage != NULL, STATUS_SIGNALING_NULL_ARG);
    if (peerClientId == NULL || IS_EMPTY_STRING(peerClientId)) {
        checkPeerClientId = FALSE;
    }

    MUTEX_LOCK(pSignalingClient->outboundMsgQLock);
    locked = TRUE;

    CHK_STATUS(stack_queue_iterator_get(pSignalingClient->pOutboundMsgQ, &iterator));
    while (IS_VALID_ITERATOR(iterator)) {
        CHK_STATUS(stack_queue_iterator_getItem(iterator, &data));

        pExistingMessage = (PSignalingMessage) data;
        CHK(pExistingMessage != NULL, STATUS_SIGNALING_INTERNAL_ERROR);

        if (((correlationId[0] == '\0' && pExistingMessage->correlationId[0] == '\0') ||
             0 == STRCMP(pExistingMessage->correlationId, correlationId)) &&
            (!checkPeerClientId || 0 == STRCMP(pExistingMessage->peerClientId, peerClientId))) {
            *ppSignalingMessage = pExistingMessage;

            // Early return
            CHK(FALSE, retStatus);
        }

        CHK_STATUS(stack_queue_iterator_getNext(&iterator));
    }

CleanUp:

    if (ppSignalingMessage != NULL) {
        *ppSignalingMessage = pExistingMessage;
    }

    if (locked) {
        MUTEX_UNLOCK(pSignalingClient->outboundMsgQLock);
    }

    LEAVES();
    return retStatus;
}

STATUS signaling_channel_describe(PSignalingClient pSignalingClient, UINT64 time)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL apiCall = TRUE;
    UINT32 httpStatusCode = HTTP_STATUS_NONE;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_NULL_ARG);

    // Check for the stale credentials
    CHECK_SIGNALING_CREDENTIALS_EXPIRATION(pSignalingClient);

    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_NONE);

    switch (pSignalingClient->pChannelInfo->cachingPolicy) {
        case SIGNALING_API_CALL_CACHE_TYPE_NONE:
            break;

        case SIGNALING_API_CALL_CACHE_TYPE_DESCRIBE_GETENDPOINT:
            /* explicit fall-through */
        case SIGNALING_API_CALL_CACHE_TYPE_FILE:
            if (IS_VALID_TIMESTAMP(pSignalingClient->apiCallHistory.describeTime) &&
                time <= pSignalingClient->apiCallHistory.describeTime + pSignalingClient->pChannelInfo->cachingPeriod) {
                apiCall = FALSE;
            }

            break;
    }

    // Call DescribeChannel API
    if (STATUS_SUCCEEDED(retStatus)) {
        if (apiCall) {
            // Call pre hook func
            if (pSignalingClient->clientInfo.describePreHookFn != NULL) {
                retStatus = pSignalingClient->clientInfo.describePreHookFn(pSignalingClient->clientInfo.hookCustomData);
            }

            if (STATUS_SUCCEEDED(retStatus)) {
                retStatus = http_api_describeChannel(pSignalingClient, &httpStatusCode);
                ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) httpStatusCode);

                // Store the last call time on success
                if (STATUS_SUCCEEDED(retStatus)) {
                    pSignalingClient->apiCallHistory.describeTime = time;
                }

                // Calculate the latency whether the call succeeded or not
                SIGNALING_API_LATENCY_CALCULATION(pSignalingClient, time, TRUE);
            }

            // Call post hook func
            if (pSignalingClient->clientInfo.describePostHookFn != NULL) {
                retStatus = pSignalingClient->clientInfo.describePostHookFn(pSignalingClient->clientInfo.hookCustomData);
            }
        } else {
            DLOGD("Skip the call of describing the channel");
            ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_OK);
        }
    }

CleanUp:

    if (STATUS_FAILED(retStatus) && pSignalingClient != NULL) {
        ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_UNKNOWN);
    }
    LEAVES();
    return retStatus;
}

STATUS signaling_channel_create(PSignalingClient pSignalingClient, UINT64 time)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 httpStatusCode = HTTP_STATUS_NONE;
    CHK(pSignalingClient != NULL, STATUS_SIGNALING_NULL_ARG);

    // Check for the stale credentials
    CHECK_SIGNALING_CREDENTIALS_EXPIRATION(pSignalingClient);

    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_NONE);

    // We are not caching create calls
    if (pSignalingClient->clientInfo.createPreHookFn != NULL) {
        retStatus = pSignalingClient->clientInfo.createPreHookFn(pSignalingClient->clientInfo.hookCustomData);
    }

    if (STATUS_SUCCEEDED(retStatus)) {
        retStatus = http_api_createChannel(pSignalingClient, &httpStatusCode);
        ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) httpStatusCode);

        // Store the time of the call on success
        if (STATUS_SUCCEEDED(retStatus)) {
            pSignalingClient->apiCallHistory.createTime = time;
        }

        // Calculate the latency whether the call succeeded or not
        SIGNALING_API_LATENCY_CALCULATION(pSignalingClient, time, TRUE);
    }

    if (pSignalingClient->clientInfo.createPostHookFn != NULL) {
        retStatus = pSignalingClient->clientInfo.createPostHookFn(pSignalingClient->clientInfo.hookCustomData);
    }

CleanUp:
    CHK_LOG_ERR(retStatus);
    if (STATUS_FAILED(retStatus) && pSignalingClient != NULL) {
        ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_UNKNOWN);
    }
    LEAVES();
    return retStatus;
}

STATUS signaling_channel_getEndpoint(PSignalingClient pSignalingClient, UINT64 time)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL apiCall = TRUE;
    UINT32 httpStatusCode = HTTP_STATUS_NONE;
    PSignalingFileCacheEntry psignalingFileCacheEntry = NULL;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_NULL_ARG);

    // Check for the stale credentials
    CHECK_SIGNALING_CREDENTIALS_EXPIRATION(pSignalingClient);

    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_NONE);

    switch (pSignalingClient->pChannelInfo->cachingPolicy) {
        case SIGNALING_API_CALL_CACHE_TYPE_NONE:
            break;

        case SIGNALING_API_CALL_CACHE_TYPE_DESCRIBE_GETENDPOINT:
            /* explicit fall-through */
        case SIGNALING_API_CALL_CACHE_TYPE_FILE:
            if (IS_VALID_TIMESTAMP(pSignalingClient->apiCallHistory.getEndpointTime) &&
                time <= pSignalingClient->apiCallHistory.getEndpointTime + pSignalingClient->pChannelInfo->cachingPeriod) {
                apiCall = FALSE;
            }

            break;
    }

    if (STATUS_SUCCEEDED(retStatus)) {
        if (apiCall) {
            if (pSignalingClient->clientInfo.getEndpointPreHookFn != NULL) {
                retStatus = pSignalingClient->clientInfo.getEndpointPreHookFn(pSignalingClient->clientInfo.hookCustomData);
            }

            if (STATUS_SUCCEEDED(retStatus)) {
                retStatus = http_api_getChannelEndpoint(pSignalingClient, &httpStatusCode);
                ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) httpStatusCode);

                if (STATUS_SUCCEEDED(retStatus)) {
                    pSignalingClient->apiCallHistory.getEndpointTime = time;

                    if (pSignalingClient->pChannelInfo->cachingPolicy == SIGNALING_API_CALL_CACHE_TYPE_FILE) {
                        CHK(NULL != (psignalingFileCacheEntry = (PSignalingFileCacheEntry) MEMCALLOC(1, SIZEOF(SignalingFileCacheEntry))),
                            STATUS_SIGNALING_NOT_ENOUGH_MEMORY);
                        psignalingFileCacheEntry->creationTsEpochSeconds = time / HUNDREDS_OF_NANOS_IN_A_SECOND;
                        psignalingFileCacheEntry->role = pSignalingClient->pChannelInfo->channelRoleType;
                        // In case of pre-created channels, the channel name can be NULL in which case we will use ARN.
                        // The validation logic in the channel info validates that both can't be NULL at the same time.
                        STRNCPY(psignalingFileCacheEntry->channelName,
                                pSignalingClient->pChannelInfo->pChannelName != NULL ? pSignalingClient->pChannelInfo->pChannelName
                                                                                     : pSignalingClient->pChannelInfo->pChannelArn,
                                MAX_CHANNEL_NAME_LEN);
                        STRNCPY(psignalingFileCacheEntry->region, pSignalingClient->pChannelInfo->pRegion, MAX_REGION_NAME_LEN);
                        STRNCPY(psignalingFileCacheEntry->channelArn, pSignalingClient->channelDescription.channelArn, MAX_ARN_LEN);
                        STRNCPY(psignalingFileCacheEntry->httpsEndpoint, pSignalingClient->channelDescription.channelEndpointHttps,
                                MAX_SIGNALING_ENDPOINT_URI_LEN);
                        STRNCPY(psignalingFileCacheEntry->wssEndpoint, pSignalingClient->channelDescription.channelEndpointWss,
                                MAX_SIGNALING_ENDPOINT_URI_LEN);

                        if (STATUS_FAILED(signaling_cache_saveToFile(psignalingFileCacheEntry))) {
                            DLOGW("Failed to save signaling cache to file");
                        }
                    }
                }
                // Calculate the latency whether the call succeeded or not
                SIGNALING_API_LATENCY_CALCULATION(pSignalingClient, time, TRUE);
            }

            if (pSignalingClient->clientInfo.getEndpointPostHookFn != NULL) {
                retStatus = pSignalingClient->clientInfo.getEndpointPostHookFn(pSignalingClient->clientInfo.hookCustomData);
            }
        } else {
            DLOGD("Skip the call of getting the endpoint");
            ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_OK);
        }
    }

CleanUp:
    CHK_LOG_ERR(retStatus);
    if (STATUS_FAILED(retStatus) && pSignalingClient != NULL) {
        ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_UNKNOWN);
    }
    SAFE_MEMFREE(psignalingFileCacheEntry);
    LEAVES();
    return retStatus;
}

STATUS signaling_channel_getIceConfig(PSignalingClient pSignalingClient, UINT64 time)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 timerId;
    UINT32 httpStatusCode = HTTP_STATUS_NONE;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_NULL_ARG);

    // Check for the stale credentials
    CHECK_SIGNALING_CREDENTIALS_EXPIRATION(pSignalingClient);

    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_NONE);

    // We are not caching ICE server config calls
    if (pSignalingClient->clientInfo.getIceConfigPreHookFn != NULL) {
        retStatus = pSignalingClient->clientInfo.getIceConfigPreHookFn(pSignalingClient->clientInfo.hookCustomData);
    }

    if (STATUS_SUCCEEDED(retStatus)) {
        retStatus = http_api_getIceConfig(pSignalingClient, &httpStatusCode);
        ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) httpStatusCode);

        if (STATUS_SUCCEEDED(retStatus)) {
            pSignalingClient->apiCallHistory.getIceConfigTime = time;
        } else {
            DLOGE("failed to get the configuration of ice servers.");
        }

        // Calculate the latency whether the call succeeded or not
        SIGNALING_API_LATENCY_CALCULATION(pSignalingClient, time, FALSE);
    }

    if (pSignalingClient->clientInfo.getIceConfigPostHookFn != NULL) {
        retStatus = pSignalingClient->clientInfo.getIceConfigPostHookFn(pSignalingClient->clientInfo.hookCustomData);
    }

CleanUp:

    if (STATUS_FAILED(retStatus) && pSignalingClient != NULL) {
        ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_UNKNOWN);
    }
    LEAVES();
    return retStatus;
}

STATUS signaling_channel_delete(PSignalingClient pSignalingClient, UINT64 time)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 httpStatusCode = HTTP_STATUS_NONE;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_NULL_ARG);
    //#TBD
    // Check if we need to terminate the ongoing listener
    wss_api_disconnect(pSignalingClient);
    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_OK);
    ATOMIC_STORE_BOOL(&pSignalingClient->connected, FALSE);

    // Check for the stale credentials
    CHECK_SIGNALING_CREDENTIALS_EXPIRATION(pSignalingClient);

    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_NONE);

    // We are not caching delete calls
    if (pSignalingClient->clientInfo.deletePreHookFn != NULL) {
        retStatus = pSignalingClient->clientInfo.deletePreHookFn(pSignalingClient->clientInfo.hookCustomData);
    }

    if (STATUS_SUCCEEDED(retStatus)) {
        retStatus = http_api_deleteChannel(pSignalingClient, &httpStatusCode);
        ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) httpStatusCode);

        // Store the time of the call on success
        if (STATUS_SUCCEEDED(retStatus)) {
            pSignalingClient->apiCallHistory.deleteTime = time;
        }

        // Calculate the latency whether the call succeeded or not
        SIGNALING_API_LATENCY_CALCULATION(pSignalingClient, time, TRUE);
    }

    if (pSignalingClient->clientInfo.deletePostHookFn != NULL) {
        retStatus = pSignalingClient->clientInfo.deletePostHookFn(pSignalingClient->clientInfo.hookCustomData);
    }

CleanUp:
    CHK_LOG_ERR(retStatus);
    if (STATUS_FAILED(retStatus) && pSignalingClient != NULL) {
        ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_UNKNOWN);
    }

    LEAVES();
    return retStatus;
}

STATUS signaling_channel_connect(PSignalingClient pSignalingClient, UINT64 time)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 httpStatusCode = HTTP_STATUS_NONE;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_NULL_ARG);

    // Check for the stale credentials
    CHECK_SIGNALING_CREDENTIALS_EXPIRATION(pSignalingClient);

    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_NONE);

    // We are not caching connect calls
    // pre-hook function.
    if (pSignalingClient->clientInfo.connectPreHookFn != NULL) {
        retStatus = pSignalingClient->clientInfo.connectPreHookFn(pSignalingClient->clientInfo.hookCustomData);
    }

    if (STATUS_SUCCEEDED(retStatus)) {
        // No need to reconnect again if already connected. This can happen if we get to this state after ice refresh
        if (!ATOMIC_LOAD_BOOL(&pSignalingClient->connected)) {
            ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_NONE);
            retStatus = wss_api_connect(pSignalingClient, &httpStatusCode);
            ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) httpStatusCode);

            // Store the time of the call on success
            if (STATUS_SUCCEEDED(retStatus)) {
                ATOMIC_STORE_BOOL(&pSignalingClient->connected, TRUE);
                pSignalingClient->apiCallHistory.connectTime = time;
            }
        } else {
            ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_OK);
        }
    }
    // post-hook function.
    if (pSignalingClient->clientInfo.connectPostHookFn != NULL) {
        retStatus = pSignalingClient->clientInfo.connectPostHookFn(pSignalingClient->clientInfo.hookCustomData);
    }

CleanUp:
    LEAVES();
    return retStatus;
}

STATUS signaling_getMetrics(PSignalingClient pSignalingClient, PSignalingClientMetrics pSignalingClientMetrics)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT64 curTime = GETTIME();

    CHK(pSignalingClient != NULL && pSignalingClientMetrics != NULL, STATUS_SIGNALING_NULL_ARG);
    CHK(pSignalingClientMetrics->version <= SIGNALING_CLIENT_METRICS_CURRENT_VERSION, STATUS_SIGNALING_INVALID_METRICS_VERSION);

    // Interlock the threading due to data race possibility
    MUTEX_LOCK(pSignalingClient->diagnosticsLock);

    // Fill in the data structures according to the version of the requested structure - currently only v0
    pSignalingClientMetrics->signalingClientStats.signalingClientUptime = curTime - pSignalingClient->diagnostics.createTime;
    pSignalingClientMetrics->signalingClientStats.numberOfMessagesSent = (UINT32) pSignalingClient->diagnostics.numberOfMessagesSent;
    pSignalingClientMetrics->signalingClientStats.numberOfMessagesReceived = (UINT32) pSignalingClient->diagnostics.numberOfMessagesReceived;
    pSignalingClientMetrics->signalingClientStats.iceRefreshCount = (UINT32) pSignalingClient->diagnostics.iceRefreshCount;
    pSignalingClientMetrics->signalingClientStats.numberOfErrors = (UINT32) pSignalingClient->diagnostics.numberOfErrors;
    pSignalingClientMetrics->signalingClientStats.numberOfRuntimeErrors = (UINT32) pSignalingClient->diagnostics.numberOfRuntimeErrors;
    pSignalingClientMetrics->signalingClientStats.numberOfReconnects = (UINT32) pSignalingClient->diagnostics.numberOfReconnects;
    pSignalingClientMetrics->signalingClientStats.cpApiCallLatency = pSignalingClient->diagnostics.cpApiLatency;
    pSignalingClientMetrics->signalingClientStats.dpApiCallLatency = pSignalingClient->diagnostics.dpApiLatency;

    pSignalingClientMetrics->signalingClientStats.connectionDuration =
        ATOMIC_LOAD_BOOL(&pSignalingClient->connected) ? curTime - pSignalingClient->diagnostics.connectTime : 0;

    MUTEX_UNLOCK(pSignalingClient->diagnosticsLock);

CleanUp:

    LEAVES();
    return retStatus;
}
