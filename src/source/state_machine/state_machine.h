/*******************************************
Main internal include file
*******************************************/
#ifndef __CONTENT_STATE_INCLUDE_I__
#define __CONTENT_STATE_INCLUDE_I__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

////////////////////////////////////////////////////
// Project include files
////////////////////////////////////////////////////
#include "kvs/common_defs.h"
#include <kvs/platform_utils.h>

// IMPORTANT! Some of the headers are not tightly packed!
////////////////////////////////////////////////////
// Public headers
////////////////////////////////////////////////////
#include "kvs/error.h"

////////////////////////////////////////////////////
// General defines and data structures
////////////////////////////////////////////////////
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
 * @param 1 UINT64 - IN - Custom data passed in
 * @param 2 PUINT64 - OUT - Returned next state on success
 *
 * @return Status of the callback
 */
typedef STATUS (*GetNextStateFunc)(UINT64, PUINT64);

/**
 * State execution function definitions
 *
 * @param 1 - IN - Custom data passed in
 * @param 2 - IN - Delay time Time after which to execute the function
 *
 * @return Status of the callback
 */
typedef STATUS (*ExecuteStateFunc)(UINT64, UINT64);

/**
 * State Machine state
 */
typedef struct __StateMachineState StateMachineState;
struct __StateMachineState {
    UINT64 state;
    UINT64 acceptStates;
    GetNextStateFunc getNextStateFn;
    ExecuteStateFunc executeStateFn;
    UINT32 retry;
    STATUS status;
};
typedef struct __StateMachineState* PStateMachineState;

/**
 * State Machine definition
 */
typedef struct __StateMachine StateMachine;
struct __StateMachine {
    UINT32 version;
};
typedef struct __StateMachine* PStateMachine;

/**
 * State Machine context
 */
typedef struct __StateMachineContext StateMachineContext;
struct __StateMachineContext {
    PStateMachineState pCurrentState;
    UINT32 retryCount;
    UINT64 time;
};
typedef struct __StateMachineContext* PStateMachineContext;

/**
 * State Machine definition
 */
typedef struct __StateMachineImpl StateMachineImpl;
struct __StateMachineImpl {
    // First member should be the public state machine
    StateMachine stateMachine;

    // Current time functionality
    GetCurrentTimeFunc getCurrentTimeFunc;

    // Custom data to be passed with the time function
    UINT64 getCurrentTimeFuncCustomData;

    // Custom data to be passed with the state callbacks
    UINT64 customData;

    // State machine context
    StateMachineContext context;

    // State machine state count
    UINT32 stateCount;

    // State machine states following the main structure
    PStateMachineState states;
};
typedef struct __StateMachineImpl* PStateMachineImpl;

STATUS state_machine_create(PStateMachineState, UINT32, UINT64, GetCurrentTimeFunc, UINT64, PStateMachine*);
STATUS state_machine_free(PStateMachine);
STATUS state_machine_step(PStateMachine);
STATUS state_machine_accept(PStateMachine pStateMachine, UINT64 requiredStates);
/**
 * @brief Gets a pointer to the state object given it's state
 *
 * @param[in] pStateMachine
 * @param[in] state
 * @param[in, out] ppState
 *
 * @return STATUS status of execution.
 */
STATUS state_machine_getState(PStateMachine pStateMachine, UINT64 state, PStateMachineState* ppState);
/**
 * @brief Gets a pointer to the current state object.
 *
 * @param[in] pStateMachine
 * @param[in, out] ppState
 *
 * @return STATUS status of execution.
 */
STATUS state_machine_getCurrentState(PStateMachine pStateMachine, PStateMachineState* ppState);
/**
 * @brief Force sets the state machine state
 *
 * @param[in] pStateMachine
 * @param[in] state
 *
 * @return STATUS status of execution.
 */
STATUS state_machine_setCurrentState(PStateMachine pStateMachine, UINT64 state);
/**
 * @brief Resets the state machine retry count.
 *
 * @param[in] pStateMachine
 *
 * @return STATUS status of execution.
 */
STATUS state_machine_resetRetryCount(PStateMachine pStateMachine);

#ifdef __cplusplus
}
#endif
#endif /* __CONTENT_STATE_INCLUDE_I__ */
