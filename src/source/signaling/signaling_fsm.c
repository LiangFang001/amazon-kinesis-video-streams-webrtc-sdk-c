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
#define LOG_CLASS "SignalingFsm"
#include "signaling_fsm.h"
#include "state_machine.h"

/******************************************************************************
 * DEFINITION
 ******************************************************************************/
#define SIGNALING_FSM_ENTER()  // DLOGD("%s enter", __func__)
#define SIGNALING_FSM_LEAVE()  // DLOGD("%s leave", __func__)
#define SIGNALING_FSM_ENTERS() // DLOGD("%s enters", __func__)
#define SIGNALING_FSM_LEAVES() // DLOGD("%s leaves", __func__)

#define SIGNALING_STATE_NEW_REQUIRED (SIGNALING_STATE_NONE | SIGNALING_STATE_NEW)
#define SIGNALING_STATE_GET_TOKEN_REQUIRED                                                                                                           \
    (SIGNALING_STATE_NEW | SIGNALING_STATE_DESCRIBE | SIGNALING_STATE_CREATE | SIGNALING_STATE_GET_ENDPOINT | SIGNALING_STATE_GET_ICE_CONFIG |       \
     SIGNALING_STATE_READY | SIGNALING_STATE_CONNECT | SIGNALING_STATE_CONNECTED | SIGNALING_STATE_GET_TOKEN)
#define SIGNALING_STATE_DESCRIBE_REQUIRED                                                                                                            \
    (SIGNALING_STATE_GET_TOKEN | SIGNALING_STATE_CREATE | SIGNALING_STATE_GET_ENDPOINT | SIGNALING_STATE_GET_ICE_CONFIG | SIGNALING_STATE_CONNECT |  \
     SIGNALING_STATE_CONNECTED | SIGNALING_STATE_DESCRIBE)
#define SIGNALING_STATE_CREATE_REQUIRED (SIGNALING_STATE_DESCRIBE | SIGNALING_STATE_CREATE)
#define SIGNALING_STATE_GET_ENDPOINT_REQUIRED                                                                                                        \
    (SIGNALING_STATE_DESCRIBE | SIGNALING_STATE_CREATE | SIGNALING_STATE_GET_TOKEN | SIGNALING_STATE_READY | SIGNALING_STATE_CONNECT |               \
     SIGNALING_STATE_CONNECTED | SIGNALING_STATE_GET_ENDPOINT)
#define SIGNALING_STATE_GET_ICE_CONFIG_REQUIRED                                                                                                      \
    (SIGNALING_STATE_DESCRIBE | SIGNALING_STATE_CONNECT | SIGNALING_STATE_CONNECTED | SIGNALING_STATE_GET_ENDPOINT | SIGNALING_STATE_READY |         \
     SIGNALING_STATE_GET_ICE_CONFIG)
#define SIGNALING_STATE_READY_REQUIRED        (SIGNALING_STATE_GET_ICE_CONFIG | SIGNALING_STATE_DISCONNECTED | SIGNALING_STATE_READY)
#define SIGNALING_STATE_CONNECT_REQUIRED      (SIGNALING_STATE_READY | SIGNALING_STATE_DISCONNECTED | SIGNALING_STATE_CONNECTED | SIGNALING_STATE_CONNECT)
#define SIGNALING_STATE_CONNECTED_REQUIRED    (SIGNALING_STATE_CONNECT | SIGNALING_STATE_CONNECTED)
#define SIGNALING_STATE_DISCONNECTED_REQUIRED (SIGNALING_STATE_CONNECT | SIGNALING_STATE_CONNECTED)

/******************************************************************************
 * INTERNAL FUNCTION PROTOTYPE
 ******************************************************************************/
STATUS signaling_fsm_exitNew(UINT64, PUINT64);
STATUS signaling_fsm_new(UINT64, UINT64);
STATUS signaling_fsm_exitGetToken(UINT64, PUINT64);
STATUS signaling_fsm_getToken(UINT64, UINT64);
STATUS signaling_fsm_exitDescribe(UINT64, PUINT64);
STATUS signaling_fsm_describe(UINT64, UINT64);
STATUS signaling_fsm_exitCreate(UINT64, PUINT64);
STATUS signaling_fsm_executCreate(UINT64, UINT64);
STATUS signaling_fsm_exitGetEndpoint(UINT64, PUINT64);
STATUS signaling_fsm_getEndpoint(UINT64, UINT64);
STATUS signaling_fsm_exitGetIceConfig(UINT64, PUINT64);
STATUS signaling_fsm_getIceConfig(UINT64, UINT64);
STATUS signaling_fsm_exitReady(UINT64, PUINT64);
STATUS signaling_fsm_ready(UINT64 customData, UINT64 time);
STATUS signaling_fsm_exitConnect(UINT64, PUINT64);
STATUS signaling_fsm_connect(UINT64, UINT64);
STATUS signaling_fsm_exitConnected(UINT64, PUINT64);
STATUS signaling_fsm_connected(UINT64, UINT64);
STATUS signaling_fsm_exitDisconnected(UINT64, PUINT64);
STATUS signaling_fsm_disconnected(UINT64, UINT64);
/**
 * Static definitions of the states
 */
static StateMachineState SIGNALING_STATE_MACHINE_STATES[] = {
    // http connection.
    {SIGNALING_STATE_NEW, SIGNALING_STATE_NEW_REQUIRED, signaling_fsm_exitNew, signaling_fsm_new, INFINITE_RETRY_COUNT_SENTINEL,
     STATUS_SIGNALING_INVALID_READY_STATE},
    {SIGNALING_STATE_GET_TOKEN, SIGNALING_STATE_GET_TOKEN_REQUIRED, signaling_fsm_exitGetToken, signaling_fsm_getToken,
     SIGNALING_STATES_DEFAULT_RETRY_COUNT, STATUS_SIGNALING_GET_TOKEN_CALL_FAILED},
    {SIGNALING_STATE_DESCRIBE, SIGNALING_STATE_DESCRIBE_REQUIRED, signaling_fsm_exitDescribe, signaling_fsm_describe,
     SIGNALING_STATES_DEFAULT_RETRY_COUNT, STATUS_SIGNALING_DESCRIBE_CALL_FAILED},
    {SIGNALING_STATE_CREATE, SIGNALING_STATE_CREATE_REQUIRED, signaling_fsm_exitCreate, signaling_fsm_executCreate,
     SIGNALING_STATES_DEFAULT_RETRY_COUNT, STATUS_SIGNALING_CREATE_CALL_FAILED},
    {SIGNALING_STATE_GET_ENDPOINT, SIGNALING_STATE_GET_ENDPOINT_REQUIRED, signaling_fsm_exitGetEndpoint, signaling_fsm_getEndpoint,
     SIGNALING_STATES_DEFAULT_RETRY_COUNT, STATUS_SIGNALING_GET_ENDPOINT_CALL_FAILED},
    {SIGNALING_STATE_GET_ICE_CONFIG, SIGNALING_STATE_GET_ICE_CONFIG_REQUIRED, signaling_fsm_exitGetIceConfig, signaling_fsm_getIceConfig,
     SIGNALING_STATES_DEFAULT_RETRY_COUNT, STATUS_SIGNALING_GET_ICE_CONFIG_CALL_FAILED},
    {SIGNALING_STATE_READY, SIGNALING_STATE_READY_REQUIRED, signaling_fsm_exitReady, signaling_fsm_ready, INFINITE_RETRY_COUNT_SENTINEL,
     STATUS_SIGNALING_READY_CALLBACK_FAILED},
    // websocket connection.
    {SIGNALING_STATE_CONNECT, SIGNALING_STATE_CONNECT_REQUIRED, signaling_fsm_exitConnect, signaling_fsm_connect, INFINITE_RETRY_COUNT_SENTINEL,
     STATUS_SIGNALING_CONNECT_CALL_FAILED},
    {SIGNALING_STATE_CONNECTED, SIGNALING_STATE_CONNECTED_REQUIRED, signaling_fsm_exitConnected, signaling_fsm_connected,
     INFINITE_RETRY_COUNT_SENTINEL, STATUS_SIGNALING_CONNECTED_CALLBACK_FAILED},
    {SIGNALING_STATE_DISCONNECTED, SIGNALING_STATE_DISCONNECTED_REQUIRED, signaling_fsm_exitDisconnected, signaling_fsm_disconnected,
     SIGNALING_STATES_DEFAULT_RETRY_COUNT, STATUS_SIGNALING_DISCONNECTED_CALLBACK_FAILED},
};

static UINT32 SIGNALING_STATE_MACHINE_STATE_COUNT = ARRAY_SIZE(SIGNALING_STATE_MACHINE_STATES);
/******************************************************************************
 * FUNCTION
 ******************************************************************************/
/******************************************************************************
 * State machine callback functions
 ******************************************************************************/
STATUS signaling_fsm_exitNew(UINT64 customData, PUINT64 pState)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);
    UINT64 state;

    CHK(pSignalingClient != NULL && pState != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    // Transition to auth state
    state = SIGNALING_STATE_GET_TOKEN;
    *pState = state;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_new(UINT64 customData, UINT64 time)
{
    SIGNALING_FSM_ENTERS();
    UNUSED_PARAM(time);
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);
    // Notify of the state change
    if (pSignalingClient->signalingClientCallbacks.stateChangeFn != NULL) {
        CHK(pSignalingClient->signalingClientCallbacks.stateChangeFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                     SIGNALING_CLIENT_STATE_NEW) == STATUS_SUCCESS,
            STATUS_SIGNALING_FSM_STATE_CHANGE_FAILED);
    }

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}
/**
 * @brief   change the fsm from get token to describe if we do not have channal arn.
 *          change the fsm from get token to get endpoint if we have channal arn.
 *          change the fsm from get token to delete if we are deleting the channel.
 */
STATUS signaling_fsm_exitGetToken(UINT64 customData, PUINT64 pState)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);
    UINT64 state = SIGNALING_STATE_GET_TOKEN;

    CHK(pSignalingClient != NULL && pState != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    // get the iot credential successfully.
    if ((HTTP_STATUS_CODE) ATOMIC_LOAD(&pSignalingClient->apiCallStatus) == HTTP_STATUS_OK) {
        // do we have the channel endpoint.
        if (pSignalingClient->pChannelInfo->pChannelArn != NULL && pSignalingClient->pChannelInfo->pChannelArn[0] != '\0') {
            // If the client application has specified the Channel ARN then we will skip describe and create states
            // Store the ARN in the stream description object first
            STRNCPY(pSignalingClient->channelDescription.channelArn, pSignalingClient->pChannelInfo->pChannelArn, MAX_ARN_LEN);
            pSignalingClient->channelDescription.channelArn[MAX_ARN_LEN] = '\0';

            // Move to get endpoint state
            state = SIGNALING_STATE_GET_ENDPOINT;
        } else {
            state = SIGNALING_STATE_DESCRIBE;
        }
    }

    *pState = state;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}
/**
 * @brief   get the aws crendential, and validate the credential. step the fsm.
 */
STATUS signaling_fsm_getToken(UINT64 customData, UINT64 time)
{
    UNUSED_PARAM(time);
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);
    HTTP_STATUS_CODE serviceCallResult;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_NONE);

    // Notify of the state change
    if (pSignalingClient->signalingClientCallbacks.stateChangeFn != NULL) {
        CHK(pSignalingClient->signalingClientCallbacks.stateChangeFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                     SIGNALING_CLIENT_STATE_GET_CREDENTIALS) == STATUS_SUCCESS,
            STATUS_SIGNALING_FSM_STATE_CHANGE_FAILED);
    }

    // Use the credential provider to get the token
    retStatus = pSignalingClient->pCredentialProvider->getCredentialsFn(pSignalingClient->pCredentialProvider, &pSignalingClient->pAwsCredentials);

    // Check the expiration
    if (NULL == pSignalingClient->pAwsCredentials || GETTIME() >= pSignalingClient->pAwsCredentials->expiration) {
        serviceCallResult = HTTP_STATUS_UNAUTHORIZED;
    } else {
        serviceCallResult = HTTP_STATUS_OK;
    }

    ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) serviceCallResult);

    // Self-prime the next state
    CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));

    // Reset the ret status
    retStatus = STATUS_SUCCESS;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_exitDescribe(UINT64 customData, PUINT64 pState)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);
    UINT64 state = SIGNALING_STATE_DESCRIBE;
    SIZE_T result;

    CHK(pSignalingClient != NULL && pState != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    result = ATOMIC_LOAD(&pSignalingClient->apiCallStatus);
    switch (result) {
        case HTTP_STATUS_OK:
            // If we are trying to delete the channel then move to delete state
            state = SIGNALING_STATE_GET_ENDPOINT;
            break;

        case HTTP_STATUS_NOT_FOUND:
            state = SIGNALING_STATE_CREATE;
            break;

        case HTTP_STATUS_FORBIDDEN:
        case HTTP_STATUS_UNAUTHORIZED:
            state = SIGNALING_STATE_GET_TOKEN;
            break;

        default:
            break;
    }

    *pState = state;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_describe(UINT64 customData, UINT64 time)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);
    // Notify of the state change
    if (pSignalingClient->signalingClientCallbacks.stateChangeFn != NULL) {
        CHK(pSignalingClient->signalingClientCallbacks.stateChangeFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                     SIGNALING_CLIENT_STATE_DESCRIBE) == STATUS_SUCCESS,
            STATUS_SIGNALING_FSM_STATE_CHANGE_FAILED);
    }

    // Call the aggregate function
    retStatus = signaling_channel_describe(pSignalingClient, time);

    CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));

    // Reset the ret status
    retStatus = STATUS_SUCCESS;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_exitCreate(UINT64 customData, PUINT64 pState)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);
    UINT64 state = SIGNALING_STATE_CREATE;
    SIZE_T result;

    CHK(pSignalingClient != NULL && pState != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    result = ATOMIC_LOAD(&pSignalingClient->apiCallStatus);
    switch (result) {
        case HTTP_STATUS_OK:
            state = SIGNALING_STATE_DESCRIBE;
            break;

        case HTTP_STATUS_FORBIDDEN:
        case HTTP_STATUS_UNAUTHORIZED:
            state = SIGNALING_STATE_GET_TOKEN;
            break;

        default:
            break;
    }

    *pState = state;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_executCreate(UINT64 customData, UINT64 time)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);
    // Notify of the state change
    if (pSignalingClient->signalingClientCallbacks.stateChangeFn != NULL) {
        CHK(pSignalingClient->signalingClientCallbacks.stateChangeFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                     SIGNALING_CLIENT_STATE_CREATE) == STATUS_SUCCESS,
            STATUS_SIGNALING_FSM_STATE_CHANGE_FAILED);
    }

    // Call the aggregate function
    retStatus = signaling_channel_create(pSignalingClient, time);

    CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));

    // Reset the ret status
    retStatus = STATUS_SUCCESS;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_exitGetEndpoint(UINT64 customData, PUINT64 pState)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);
    UINT64 state = SIGNALING_STATE_GET_ENDPOINT;
    SIZE_T result;

    CHK(pSignalingClient != NULL && pState != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    result = ATOMIC_LOAD(&pSignalingClient->apiCallStatus);
    switch (result) {
        case HTTP_STATUS_OK:
            state = SIGNALING_STATE_GET_ICE_CONFIG;
            break;

        case HTTP_STATUS_FORBIDDEN:
        case HTTP_STATUS_UNAUTHORIZED:
            state = SIGNALING_STATE_GET_TOKEN;
            break;

        default:
            break;
    }

    *pState = state;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_getEndpoint(UINT64 customData, UINT64 time)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);
    // Notify of the state change
    if (pSignalingClient->signalingClientCallbacks.stateChangeFn != NULL) {
        CHK(pSignalingClient->signalingClientCallbacks.stateChangeFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                     SIGNALING_CLIENT_STATE_GET_ENDPOINT) == STATUS_SUCCESS,
            STATUS_SIGNALING_FSM_STATE_CHANGE_FAILED);
    }

    // Call the aggregate function
    retStatus = signaling_channel_getEndpoint(pSignalingClient, time);

    CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));

    // Reset the ret status
    retStatus = STATUS_SUCCESS;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_exitGetIceConfig(UINT64 customData, PUINT64 pState)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);
    UINT64 state = SIGNALING_STATE_GET_ICE_CONFIG;
    SIZE_T result;

    CHK(pSignalingClient != NULL && pState != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    result = ATOMIC_LOAD(&pSignalingClient->apiCallStatus);
    switch (result) {
        case HTTP_STATUS_OK:
            state = SIGNALING_STATE_READY;
            break;

        case HTTP_STATUS_FORBIDDEN:
        case HTTP_STATUS_UNAUTHORIZED:
            state = SIGNALING_STATE_GET_TOKEN;
            break;

        default:
            break;
    }

    *pState = state;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_getIceConfig(UINT64 customData, UINT64 time)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);
    // Notify of the state change
    if (pSignalingClient->signalingClientCallbacks.stateChangeFn != NULL) {
        CHK(pSignalingClient->signalingClientCallbacks.stateChangeFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                     SIGNALING_CLIENT_STATE_GET_ICE_CONFIG) == STATUS_SUCCESS,
            STATUS_SIGNALING_FSM_STATE_CHANGE_FAILED);
    }

    // Call the aggregate function
    retStatus = signaling_channel_getIceConfig(pSignalingClient, time);

    CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));

    // Reset the ret status
    retStatus = STATUS_SUCCESS;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_exitReady(UINT64 customData, PUINT64 pState)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);
    UINT64 state = SIGNALING_STATE_READY;

    SIZE_T result;
    CHK(pSignalingClient != NULL && pState != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    result = ATOMIC_LOAD(&pSignalingClient->apiCallStatus);
    switch (result) {
        case HTTP_STATUS_OK:
            state = SIGNALING_STATE_CONNECT;
            break;

        case HTTP_STATUS_SIGNALING_RECONNECT_ICE:
            state = SIGNALING_STATE_GET_ICE_CONFIG;
            break;

        case HTTP_STATUS_FORBIDDEN:
        case HTTP_STATUS_UNAUTHORIZED:
            state = SIGNALING_STATE_GET_TOKEN;
            break;

        default:
            break;
    }

    // Overwrite the state if we are force refreshing
    state = ATOMIC_EXCHANGE_BOOL(&pSignalingClient->refreshIceConfig, FALSE) ? SIGNALING_STATE_GET_ICE_CONFIG : state;

    *pState = state;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_ready(UINT64 customData, UINT64 time)
{
    UNUSED_PARAM(time);
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    // Notify of the state change
    if (pSignalingClient->signalingClientCallbacks.stateChangeFn != NULL) {
        CHK(pSignalingClient->signalingClientCallbacks.stateChangeFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                     SIGNALING_CLIENT_STATE_READY) == STATUS_SUCCESS,
            STATUS_SIGNALING_FSM_STATE_CHANGE_FAILED);
    }

    // Ensure we won't async the GetIceConfig as we reach the ready state
    if (pSignalingClient->connecting) {
        // Self-prime the connect
        CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));
    } else {
        // Reset the timeout for the state machine
        pSignalingClient->stepUntil = 0;
    }

    // Reset the ret status
    retStatus = STATUS_SUCCESS;
CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_exitConnect(UINT64 customData, PUINT64 pState)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);
    UINT64 state = SIGNALING_STATE_CONNECT;
    SIZE_T result;
    BOOL connected;

    CHK(pSignalingClient != NULL && pState != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    result = ATOMIC_LOAD(&pSignalingClient->apiCallStatus);
    connected = ATOMIC_LOAD_BOOL(&pSignalingClient->connected);
    switch (result) {
        case HTTP_STATUS_OK:
            // We also need to check whether we terminated OK and connected or
            // simply terminated without being connected
            if (connected) {
                state = SIGNALING_STATE_CONNECTED;
            }

            break;

        case HTTP_STATUS_NOT_FOUND:
            state = SIGNALING_STATE_DESCRIBE;
            break;

        case HTTP_STATUS_FORBIDDEN:
        case HTTP_STATUS_UNAUTHORIZED:
            state = SIGNALING_STATE_GET_TOKEN;
            break;

        case HTTP_STATUS_INTERNAL_SERVER_ERROR:
        case HTTP_STATUS_BAD_REQUEST:
            state = SIGNALING_STATE_GET_ENDPOINT;
            break;

        case HTTP_STATUS_SIGNALING_RECONNECT_ICE:
            state = SIGNALING_STATE_GET_ICE_CONFIG;
            break;

        case HTTP_STATUS_NETWORK_CONNECTION_TIMEOUT:
        case HTTP_STATUS_NETWORK_READ_TIMEOUT:
        case HTTP_STATUS_REQUEST_TIMEOUT:
        case HTTP_STATUS_GATEWAY_TIMEOUT:
            // Attempt to get a new endpoint
            state = SIGNALING_STATE_GET_ENDPOINT;
            break;

        default:
            state = SIGNALING_STATE_GET_TOKEN;
            break;
    }

    // Overwrite the state if we are force refreshing
    state = ATOMIC_EXCHANGE_BOOL(&pSignalingClient->refreshIceConfig, FALSE) ? SIGNALING_STATE_GET_ICE_CONFIG : state;

    *pState = state;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_connect(UINT64 customData, UINT64 time)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    // Notify of the state change
    if (pSignalingClient->signalingClientCallbacks.stateChangeFn != NULL) {
        CHK(pSignalingClient->signalingClientCallbacks.stateChangeFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                     SIGNALING_CLIENT_STATE_CONNECTING) == STATUS_SUCCESS,
            STATUS_SIGNALING_FSM_STATE_CHANGE_FAILED);
    }

    retStatus = signaling_channel_connect(pSignalingClient, time);

    CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));

    // Reset the ret status
    retStatus = STATUS_SUCCESS;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_exitConnected(UINT64 customData, PUINT64 pState)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);
    UINT64 state = SIGNALING_STATE_CONNECTED;
    SIZE_T result;

    CHK(pSignalingClient != NULL && pState != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    result = ATOMIC_LOAD(&pSignalingClient->apiCallStatus);
    switch (result) {
        case HTTP_STATUS_OK:
            if (!ATOMIC_LOAD_BOOL(&pSignalingClient->connected)) {
                state = SIGNALING_STATE_DISCONNECTED;
            }
            break;

        case HTTP_STATUS_NOT_FOUND:
        case HTTP_STATUS_SIGNALING_GO_AWAY:
            state = SIGNALING_STATE_DESCRIBE;
            break;

        case HTTP_STATUS_FORBIDDEN:
        case HTTP_STATUS_UNAUTHORIZED:
            state = SIGNALING_STATE_GET_TOKEN;
            break;

        case HTTP_STATUS_INTERNAL_SERVER_ERROR:
        case HTTP_STATUS_BAD_REQUEST:
            state = SIGNALING_STATE_GET_ENDPOINT;
            break;

        case HTTP_STATUS_SIGNALING_RECONNECT_ICE:
            state = SIGNALING_STATE_GET_ICE_CONFIG;
            break;

        default:
            state = SIGNALING_STATE_GET_TOKEN;
            break;
    }

    // Overwrite the state if we are force refreshing
    state = ATOMIC_EXCHANGE_BOOL(&pSignalingClient->refreshIceConfig, FALSE) ? SIGNALING_STATE_GET_ICE_CONFIG : state;

    *pState = state;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_connected(UINT64 customData, UINT64 time)
{
    SIGNALING_FSM_ENTERS();
    UNUSED_PARAM(time);
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    // Notify of the state change
    if (pSignalingClient->signalingClientCallbacks.stateChangeFn != NULL) {
        CHK(pSignalingClient->signalingClientCallbacks.stateChangeFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                     SIGNALING_CLIENT_STATE_CONNECTED) == STATUS_SUCCESS,
            STATUS_SIGNALING_FSM_STATE_CHANGE_FAILED);
    }

    // Reset the timeout for the state machine
    MUTEX_LOCK(pSignalingClient->nestedFsmLock);
    pSignalingClient->stepUntil = 0;
    MUTEX_UNLOCK(pSignalingClient->nestedFsmLock);

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_exitDisconnected(UINT64 customData, PUINT64 pState)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);
    UINT64 state = SIGNALING_STATE_DISCONNECTED;
    SIZE_T result;

    CHK(pSignalingClient != NULL && pState != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    // See if we need to retry first of all
    CHK(pSignalingClient->reconnect, STATUS_SUCCESS);

    result = ATOMIC_LOAD(&pSignalingClient->apiCallStatus);
    switch (result) {
        case HTTP_STATUS_FORBIDDEN:
        case HTTP_STATUS_UNAUTHORIZED:
            state = SIGNALING_STATE_GET_TOKEN;
            break;

        default:
            state = SIGNALING_STATE_GET_ICE_CONFIG;
            break;
    }

    // Overwrite the state if we are force refreshing
    state = ATOMIC_EXCHANGE_BOOL(&pSignalingClient->refreshIceConfig, FALSE) ? SIGNALING_STATE_GET_ICE_CONFIG : state;
    *pState = state;

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_disconnected(UINT64 customData, UINT64 time)
{
    SIGNALING_FSM_ENTERS();
    UNUSED_PARAM(time);
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingClient pSignalingClient = SIGNALING_CLIENT_FROM_CUSTOM_DATA(customData);

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    // Notify of the state change
    if (pSignalingClient->signalingClientCallbacks.stateChangeFn != NULL) {
        CHK(pSignalingClient->signalingClientCallbacks.stateChangeFn(pSignalingClient->signalingClientCallbacks.customData,
                                                                     SIGNALING_CLIENT_STATE_DISCONNECTED) == STATUS_SUCCESS,
            STATUS_SIGNALING_FSM_STATE_CHANGE_FAILED);
    }

    // Self-prime the next state
    if (pSignalingClient->reconnect == TRUE) {
        CHK_STATUS(signaling_fsm_step(pSignalingClient, retStatus));
    }

CleanUp:

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

UINT64 signaling_fsm_getCurrentTime(UINT64 customData)
{
    UNUSED_PARAM(customData);
    return GETTIME();
}

STATUS signaling_fsm_create(PSignalingClient pSignalingClient, PSignalingFsmHandle pSignalingFsmHandle)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PStateMachine pStateMachine = NULL;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    CHK_STATUS(state_machine_create(SIGNALING_STATE_MACHINE_STATES, SIGNALING_STATE_MACHINE_STATE_COUNT, pSignalingClient,
                                    signaling_fsm_getCurrentTime, pSignalingClient, &pStateMachine));

CleanUp:
    *pSignalingFsmHandle = pStateMachine;
    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_free(SignalingFsmHandle signalingFsmHandle)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    state_machine_free(signalingFsmHandle);

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_step(PSignalingClient pSignalingClient, STATUS status)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 i;
    BOOL locked = FALSE;
    UINT64 currentTime;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    // Check for a shutdown
    CHK(!ATOMIC_LOAD_BOOL(&pSignalingClient->shutdown), retStatus);

    MUTEX_LOCK(pSignalingClient->nestedFsmLock);
    locked = TRUE;

    // Check if an error and the retry is OK
    if (!pSignalingClient->pChannelInfo->retry && STATUS_FAILED(status)) {
        CHK(FALSE, status);
    }

    currentTime = GETTIME();

    CHK(pSignalingClient->stepUntil == 0 || currentTime <= pSignalingClient->stepUntil, STATUS_SIGNALING_FSM_TIMEOUT);

    // Check if the status is any of the retry/failed statuses
    if (STATUS_FAILED(status)) {
        for (i = 0; i < SIGNALING_STATE_MACHINE_STATE_COUNT; i++) {
            CHK(status != SIGNALING_STATE_MACHINE_STATES[i].status, SIGNALING_STATE_MACHINE_STATES[i].status);
        }
    }
    //#TBD.
    // Fix-up the expired credentials transition
    // NOTE: Api Gateway might not return an error that can be interpreted as unauthorized to
    // make the correct transition to auth integration state.

    if (pSignalingClient->pAwsCredentials != NULL && pSignalingClient->pAwsCredentials->expiration < currentTime) {
        // Set the call status as auth error
        ATOMIC_STORE(&pSignalingClient->apiCallStatus, (SIZE_T) HTTP_STATUS_UNAUTHORIZED);
    }

    // Step the state machine
    CHK_STATUS(state_machine_step(pSignalingClient->signalingFsmHandle));

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pSignalingClient->nestedFsmLock);
    }

    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_resetRetryCount(PSignalingClient pSignalingClient)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    MUTEX_LOCK(pSignalingClient->nestedFsmLock);
    locked = TRUE;

    state_machine_resetRetryCount(pSignalingClient->signalingFsmHandle);

CleanUp:
    if (locked) {
        MUTEX_UNLOCK(pSignalingClient->nestedFsmLock);
    }
    SIGNALING_FSM_LEAVES();
    return retStatus;
}

STATUS signaling_fsm_setCurrentState(PSignalingClient pSignalingClient, UINT64 state)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    MUTEX_LOCK(pSignalingClient->nestedFsmLock);
    locked = TRUE;

    state_machine_setCurrentState(pSignalingClient->signalingFsmHandle, state);

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pSignalingClient->nestedFsmLock);
    }
    SIGNALING_FSM_LEAVES();
    return retStatus;
}

UINT64 signaling_fsm_getCurrentState(PSignalingClient pSignalingClient)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PStateMachineState pStateMachineState = NULL;
    UINT64 state = SIGNALING_STATE_NONE;
    BOOL locked = FALSE;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    MUTEX_LOCK(pSignalingClient->nestedFsmLock);
    locked = TRUE;

    CHK_STATUS(state_machine_getCurrentState(pSignalingClient->signalingFsmHandle, &pStateMachineState));
    state = pStateMachineState->state;

CleanUp:
    if (locked) {
        MUTEX_UNLOCK(pSignalingClient->nestedFsmLock);
    }
    SIGNALING_FSM_LEAVES();
    return state;
}

STATUS signaling_fsm_accept(PSignalingClient pSignalingClient, UINT64 requiredStates)
{
    SIGNALING_FSM_ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;

    CHK(pSignalingClient != NULL, STATUS_SIGNALING_FSM_NULL_ARG);

    MUTEX_LOCK(pSignalingClient->nestedFsmLock);
    locked = TRUE;

    // Step the state machine
    CHK_STATUS(state_machine_accept(pSignalingClient->signalingFsmHandle, requiredStates));

CleanUp:
    if (locked) {
        MUTEX_UNLOCK(pSignalingClient->nestedFsmLock);
    }
    CHK_LOG_ERR(retStatus);
    SIGNALING_FSM_LEAVES();
    return retStatus;
}
