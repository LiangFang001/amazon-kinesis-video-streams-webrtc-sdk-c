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
#ifndef __AWS_KVS_WEBRTC_STATE_MACHINE__
#define __AWS_KVS_WEBRTC_STATE_MACHINE__

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
 * General defines and data structures
 ******************************************************************************/
/**
 * Service call retry timeout - 100ms
 */
#define SERVICE_CALL_RETRY_TIMEOUT (100 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND)

/**
 * Indicates infinite retries
 */
#define INFINITE_RETRY_COUNT_SENTINEL 0

/**
 * State machine current version
 */
#define STATE_MACHINE_CURRENT_VERSION 0

/**
 * Calculates the next service call retry time
 */
#define NEXT_SERVICE_CALL_RETRY_DELAY(r) (((UINT64) 1 << (r)) * SERVICE_CALL_RETRY_TIMEOUT)
/**
 * State transition function definitions
 *
 * @param[in] UINT64 Custom data passed in
 * @param[in, out] PUINT64 Returned next state on success
 *
 * @return Status of the callback
 */
typedef STATUS (*GetNextStateFunc)(UINT64, PUINT64);

/**
 * State execution function definitions
 *
 * @param[in] UINT64 Custom data passed in
 * @param[in] UINT64 Delay time Time after which to execute the function
 *
 * @return Status of the callback
 */
typedef STATUS (*ExecuteStateFunc)(UINT64, UINT64);

/**
 * State Machine state
 */
typedef struct __StateMachineState {
    UINT64 state;                    //!< the state of this state machine.
    UINT64 acceptStates;             //!< the previous accepted states.
    GetNextStateFunc getNextStateFn; //!< the transition function of state machine.
    ExecuteStateFunc executeStateFn; //!< the main executor function.
    UINT32 retry;                    //!< the maximum number of retry.
    STATUS status;                   //!< the error code for the failure of this state machine.
} StateMachineState, *PStateMachineState;

/**
 * State Machine definition
 */
typedef struct __StateMachine {
    UINT32 version;
} StateMachine, *PStateMachine;

/**
 * State Machine context
 */
typedef struct __StateMachineContext {
    PStateMachineState pCurrentState; //!< the context of the current state.
    UINT32 retryCount;                //!< the current number of retry in this state.
    UINT64 time;                      //!< the current time of step the state machine.
    UINT64 statusCode;
} StateMachineContext, *PStateMachineContext;

/**
 * State Machine definition
 */
typedef struct __StateMachineImpl {
    StateMachine stateMachine;             //!< First member should be the public state machine
    GetCurrentTimeFunc getCurrentTimeFunc; //!< Current time functionality
    UINT64 getCurrentTimeFuncCustomData;   //!< Custom data to be passed with the time function
    UINT64 customData;                     //!< Custom data to be passed with the state callbacks
    StateMachineContext context;           //!< State machine context
    UINT32 stateCount;                     //!< State machine state count
    PStateMachineState states;             //!< State machine states following the main structure
} StateMachineImpl, *PStateMachineImpl;

/**
 * @brief Creates a new state machine
 *
 * @param[in] pStates the array of the states.
 * @param[in] stateCount the number of the states.
 * @param[in] customData the user data.
 * @param[in] getCurrentTimeFunc the function pointer of getting current time.
 * @param[in] getCurrentTimeFuncCustomData the user data passed to the function of getting current time.
 * @param[in] ppStateMachine the context of this state machine.
 *
 * @return STATUS status of execution.
 */
STATUS state_machine_create(PStateMachineState pStates, UINT32 stateCount, UINT64 customData, GetCurrentTimeFunc getCurrentTimeFunc,
                            UINT64 getCurrentTimeFuncCustomData, PStateMachine* ppStateMachine);
/**
 * @brief Frees the state machine object
 *
 * @param[in] pStateMachine the context of this state machine.
 *
 * @return STATUS status of execution.
 */
STATUS state_machine_free(PStateMachine pStateMachine);
/**
 * @brief Transition the state machine given it's context
 *
 * @param[in] pStateMachine the context of this state machine.
 *
 * @return STATUS status of execution.
 */
STATUS state_machine_step(PStateMachine pStateMachine);
/**
 * @brief Checks whether the state machine state is accepted states
 *
 * @param[in] pStateMachine the context of this state machine.
 * @param[in] requiredStates
 *
 * @return STATUS status of execution.
 */
STATUS state_machine_accept(PStateMachine pStateMachine, UINT64 requiredStates);
/**
 * @brief Gets a pointer to the state object given it's state
 *
 * @param[in] pStateMachine the context of this state machine.
 * @param[in] state
 * @param[in, out] ppState
 *
 * @return STATUS status of execution.
 */
STATUS state_machine_getState(PStateMachine pStateMachine, UINT64 state, PStateMachineState* ppState);
/**
 * @brief Gets a pointer to the current state object.
 *
 * @param[in] pStateMachine the context of this state machine.
 * @param[in, out] ppState
 *
 * @return STATUS status of execution.
 */
STATUS state_machine_getCurrentState(PStateMachine pStateMachine, PStateMachineState* ppState);
/**
 * @brief Force sets the state machine state
 *
 * @param[in] pStateMachine the context of this state machine.
 * @param[in] state
 *
 * @return STATUS status of execution.
 */
STATUS state_machine_setCurrentState(PStateMachine pStateMachine, UINT64 state);
/**
 * @brief Resets the state machine retry count.
 *
 * @param[in] pStateMachine the context of this state machine.
 *
 * @return STATUS status of execution.
 */
STATUS state_machine_resetRetryCount(PStateMachine pStateMachine);

#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_STATE_MACHINE__ */
