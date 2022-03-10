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
#define LOG_CLASS "State"
#include "state_machine.h"

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS state_machine_create(PStateMachineState pStates, UINT32 stateCount, UINT64 customData, GetCurrentTimeFunc getCurrentTimeFunc,
                            UINT64 getCurrentTimeFuncCustomData, PStateMachine* ppStateMachine)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PStateMachineImpl pStateMachine = NULL;
    UINT32 allocationSize = 0;

    CHK(pStates != NULL && ppStateMachine != NULL && getCurrentTimeFunc != NULL, STATUS_STATE_MACHINE_NULL_ARG);
    CHK(stateCount > 0, STATUS_INVALID_ARG);

    // Allocate the main struct with an array of stream pointers at the end
    // NOTE: The calloc will Zero the fields
    allocationSize = SIZEOF(StateMachineImpl) + SIZEOF(StateMachineState) * stateCount;
    pStateMachine = (PStateMachineImpl) MEMCALLOC(1, allocationSize);
    CHK(pStateMachine != NULL, STATUS_NOT_ENOUGH_MEMORY);

    // Set the values
    pStateMachine->stateMachine.version = STATE_MACHINE_CURRENT_VERSION;
    pStateMachine->getCurrentTimeFunc = getCurrentTimeFunc;
    pStateMachine->getCurrentTimeFuncCustomData = getCurrentTimeFuncCustomData;
    pStateMachine->stateCount = stateCount;
    pStateMachine->customData = customData;

    // Set the states pointer and copy the globals
    pStateMachine->states = (PStateMachineState)(pStateMachine + 1);

    // Copy the states over
    MEMCPY(pStateMachine->states, pStates, SIZEOF(StateMachineState) * stateCount);

    // NOTE: Set the initial state as the first state.
    pStateMachine->context.pCurrentState = pStateMachine->states;

    // Assign the created object
    *ppStateMachine = (PStateMachine) pStateMachine;

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        state_machine_free((PStateMachine) pStateMachine);
    }

    LEAVES();
    return retStatus;
}

STATUS state_machine_free(PStateMachine pStateMachine)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    // Call is idempotent
    CHK(pStateMachine != NULL, retStatus);

    // Release the object
    MEMFREE(pStateMachine);

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS state_machine_getState(PStateMachine pStateMachine, UINT64 state, PStateMachineState* ppState)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PStateMachineState pState = NULL;
    PStateMachineImpl pStateMachineImpl = (PStateMachineImpl) pStateMachine;
    UINT32 i;

    CHK(pStateMachineImpl != NULL && ppState, STATUS_STATE_MACHINE_NULL_ARG);

    // Iterate over and find the first state
    for (i = 0; pState == NULL && i < pStateMachineImpl->stateCount; i++) {
        if (pStateMachineImpl->states[i].state == state) {
            pState = &pStateMachineImpl->states[i];
        }
    }

    // Check if found
    CHK(pState != NULL, STATUS_STATE_MACHINE_STATE_NOT_FOUND);

    // Assign the object which might be NULL if we didn't find any
    *ppState = pState;

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS state_machine_getCurrentState(PStateMachine pStateMachine, PStateMachineState* ppState)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PStateMachineImpl pStateMachineImpl = (PStateMachineImpl) pStateMachine;

    CHK(pStateMachineImpl != NULL && ppState, STATUS_STATE_MACHINE_NULL_ARG);

    *ppState = pStateMachineImpl->context.pCurrentState;

CleanUp:
    LEAVES();
    return retStatus;
}

STATUS state_machine_step(PStateMachine pStateMachine)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PStateMachineState pState = NULL;
    UINT64 nextState, time;
    UINT64 customData;
    PStateMachineImpl pStateMachineImpl = (PStateMachineImpl) pStateMachine;

    CHK(pStateMachineImpl != NULL, STATUS_STATE_MACHINE_NULL_ARG);
    customData = pStateMachineImpl->customData;

    // Get the next state
    CHK(pStateMachineImpl->context.pCurrentState->getNextStateFn != NULL, STATUS_STATE_MACHINE_NULL_ARG);
    CHK_STATUS(pStateMachineImpl->context.pCurrentState->getNextStateFn(pStateMachineImpl->customData, &nextState));

    // Validate if the next state can accept the current state before transitioning
    CHK_STATUS(state_machine_getState(pStateMachine, nextState, &pState));
    CHK_STATUS(state_machine_accept((PStateMachine) pStateMachineImpl, pState->acceptStates));

    // Clear the iteration info if a different state and transition the state
    time = pStateMachineImpl->getCurrentTimeFunc(pStateMachineImpl->getCurrentTimeFuncCustomData);

    // Check if we are changing the state
    if (pState->state != pStateMachineImpl->context.pCurrentState->state) {
        // Clear the iteration data
        pStateMachineImpl->context.retryCount = 0;
        pStateMachineImpl->context.time = time;

        DLOGV("State Machine - Current state: 0x%016" PRIx64 ", Next state: 0x%016" PRIx64, pStateMachineImpl->context.pCurrentState->state,
              nextState);
    } else {
        // Increment the state retry count
        pStateMachineImpl->context.retryCount++;
        pStateMachineImpl->context.time = time + NEXT_SERVICE_CALL_RETRY_DELAY(pStateMachineImpl->context.retryCount);

        // Check if we have tried enough times
        if (pState->retry != INFINITE_RETRY_COUNT_SENTINEL) {
            CHK(pStateMachineImpl->context.retryCount <= pState->retry, pState->status);
        }
    }

    pStateMachineImpl->context.pCurrentState = pState;

    // Execute the state function if specified
    if (pStateMachineImpl->context.pCurrentState->executeStateFn != NULL) {
        CHK_STATUS(pStateMachineImpl->context.pCurrentState->executeStateFn(pStateMachineImpl->customData, pStateMachineImpl->context.time));
    }

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS state_machine_accept(PStateMachine pStateMachine, UINT64 requiredStates)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PStateMachineImpl pStateMachineImpl = (PStateMachineImpl) pStateMachine;

    CHK(pStateMachineImpl != NULL, STATUS_STATE_MACHINE_NULL_ARG);

    // Check the current state
    CHK((requiredStates & pStateMachineImpl->context.pCurrentState->state) == pStateMachineImpl->context.pCurrentState->state,
        STATUS_STATE_MACHINE_INVALID_STATE);

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS state_machine_setCurrentState(PStateMachine pStateMachine, UINT64 state)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PStateMachineImpl pStateMachineImpl = (PStateMachineImpl) pStateMachine;
    PStateMachineState pState = NULL;

    CHK(pStateMachineImpl != NULL, STATUS_STATE_MACHINE_NULL_ARG);
    CHK_STATUS(state_machine_getState(pStateMachine, state, &pState));

    // Force set the state
    pStateMachineImpl->context.pCurrentState = pState;

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS state_machine_resetRetryCount(PStateMachine pStateMachine)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PStateMachineImpl pStateMachineImpl = (PStateMachineImpl) pStateMachine;

    CHK(pStateMachineImpl != NULL, STATUS_STATE_MACHINE_NULL_ARG);

    // Reset the state
    pStateMachineImpl->context.retryCount = 0;
    pStateMachineImpl->context.time = 0;

CleanUp:

    LEAVES();
    return retStatus;
}
