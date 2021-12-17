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
#ifndef __KINESIS_VIDEO_WEBRTC_SIGNALING_STATE_MACHINE__
#define __KINESIS_VIDEO_WEBRTC_SIGNALING_STATE_MACHINE__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "signaling.h"
/******************************************************************************
 * DEFINITION
 ******************************************************************************/
/**
 * Signaling states definitions
 */
#define SIGNALING_STATE_NONE           ((UINT64) 0)
#define SIGNALING_STATE_NEW            ((UINT64)(1 << 0))
#define SIGNALING_STATE_GET_TOKEN      ((UINT64)(1 << 1))
#define SIGNALING_STATE_DESCRIBE       ((UINT64)(1 << 2))
#define SIGNALING_STATE_CREATE         ((UINT64)(1 << 3))
#define SIGNALING_STATE_GET_ENDPOINT   ((UINT64)(1 << 4))
#define SIGNALING_STATE_GET_ICE_CONFIG ((UINT64)(1 << 5))
#define SIGNALING_STATE_READY          ((UINT64)(1 << 6))
#define SIGNALING_STATE_CONNECT        ((UINT64)(1 << 7))
#define SIGNALING_STATE_CONNECTED      ((UINT64)(1 << 8))
#define SIGNALING_STATE_DISCONNECTED   ((UINT64)(1 << 9))
#define SIGNALING_STATE_DELETE         ((UINT64)(1 << 10))
#define SIGNALING_STATE_DELETED        ((UINT64)(1 << 11))

typedef PVOID SignalingFsmHandle;
typedef SignalingFsmHandle* PSignalingFsmHandle;

/******************************************************************************
 * FUNCTION PROTOTYPE
 ******************************************************************************/
/**
 * @brief create the signaling fsm.
 *
 * @param[in] pSignalingClient the context of the signaling client.
 * @param[in, out] pSignalingFsmHandle the handle of the signaling fsm.
 *
 * @return STATUS status of execution.
 */
STATUS signaling_fsm_create(PSignalingClient pSignalingClient, PSignalingFsmHandle pSignalingFsmHandle);
/**
 * @brief free the signaling fsm.
 *
 * @param[in] pSignalingFsmHandle the handle of the signaling fsm.
 *
 * @return STATUS status of execution.
 */
STATUS signaling_fsm_free(SignalingFsmHandle signalingFsmHandle);
/**
 * @brief step the state machine
 *
 * @param[in] pSignalingClient the context of the signaling client.
 * @param[in] status
 *
 * @return STATUS status of execution.
 */
STATUS signaling_fsm_step(PSignalingClient pSignalingClient, STATUS status);
/**
 * @brief check the current state is the required state or not.
 *
 * @param[in] pSignalingClient the context of the signaling client.
 * @param[in] requiredStates
 *
 * @return STATUS status of execution.
 */
STATUS signaling_fsm_accept(PSignalingClient pSignalingClient, UINT64 requiredStates);
STATUS signaling_fsm_resetRetryCount(PSignalingClient pSignalingClient);
STATUS signaling_fsm_setCurrentState(PSignalingClient pSignalingClient, UINT64 state);
UINT64 signaling_fsm_getCurrentState(PSignalingClient pSignalingClient);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_SIGNALING_STATE_MACHINE__ */
