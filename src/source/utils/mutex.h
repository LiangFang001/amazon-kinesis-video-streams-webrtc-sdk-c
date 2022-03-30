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
#ifndef __AWS_KVS_WEBRTC_MUTEX_INCLUDE__
#define __AWS_KVS_WEBRTC_MUTEX_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif

#if defined _WIN32 || defined _WIN64 || defined __CYGWIN__

// Definition of the static initializers
#define GLOBAL_MUTEX_INIT           MUTEX_CREATE(FALSE)
#define GLOBAL_MUTEX_INIT_RECURSIVE MUTEX_CREATE(TRUE)
#define GLOBAL_CVAR_INIT            CVAR_CREATE()

#else //!< #if defined _WIN32 || defined _WIN64 || defined __CYGWIN__

// NOTE!!! Some of the libraries don't have a definition of PTHREAD_RECURSIVE_MUTEX_INITIALIZER
#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER

#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP                                                                                                       \
    {                                                                                                                                                \
        {                                                                                                                                            \
            PTHREAD_MUTEX_RECURSIVE                                                                                                                  \
        }                                                                                                                                            \
    }
#endif //!< #ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP

#define GLOBAL_MUTEX_INIT_RECURSIVE PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#else //!< #ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER
#define GLOBAL_MUTEX_INIT_RECURSIVE PTHREAD_RECURSIVE_MUTEX_INITIALIZER
#endif //!< #ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER

#define GLOBAL_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define GLOBAL_CVAR_INIT  PTHREAD_COND_INITIALIZER

#endif //!< #if defined _WIN32 || defined _WIN64 || defined __CYGWIN__

// Conditional variable
#if defined __WINDOWS_BUILD__
typedef PCONDITION_VARIABLE CVAR;
#else
#include <pthread.h>
#include <signal.h>
#if defined(KVS_PLAT_ESP_FREERTOS)
#include <esp_pthread.h>
#include "esp_heap_caps.h"
#include "esp_system.h"
#endif
typedef pthread_cond_t* CVAR;
#endif

#ifndef INVALID_CVAR_VALUE
#define INVALID_CVAR_VALUE ((CVAR) NULL)
#endif

#ifndef IS_VALID_CVAR_VALUE
#define IS_VALID_CVAR_VALUE(c) ((c) != INVALID_CVAR_VALUE)
#endif

// Mutex typedef
typedef UINT64 MUTEX;

#ifndef INVALID_MUTEX_VALUE
#define INVALID_MUTEX_VALUE ((UINT64) NULL)
#endif

#ifndef IS_VALID_MUTEX_VALUE
#define IS_VALID_MUTEX_VALUE(m) ((m) != INVALID_MUTEX_VALUE)
#endif

// Max mutex name
#ifndef MAX_MUTEX_NAME
#define MAX_MUTEX_NAME 32
#endif

typedef MUTEX (*createMutex)(BOOL);
typedef VOID (*lockMutex)(MUTEX);
typedef VOID (*unlockMutex)(MUTEX);
typedef BOOL (*tryLockMutex)(MUTEX);
typedef BOOL (*waitLockMutex)(MUTEX, UINT64);
typedef VOID (*freeMutex)(MUTEX);

typedef CVAR (*createConditionVariable)();
typedef STATUS (*signalConditionVariable)(CVAR);
typedef STATUS (*broadcastConditionVariable)(CVAR);
typedef STATUS (*waitConditionVariable)(CVAR, MUTEX, UINT64);
typedef VOID (*freeConditionVariable)(CVAR);

extern createMutex globalCreateMutex;
extern lockMutex globalLockMutex;
extern unlockMutex globalUnlockMutex;
extern tryLockMutex globalTryLockMutex;
extern waitLockMutex globalWaitLockMutex;
extern freeMutex globalFreeMutex;

extern createConditionVariable globalConditionVariableCreate;
extern signalConditionVariable globalConditionVariableSignal;
extern broadcastConditionVariable globalConditionVariableBroadcast;
extern waitConditionVariable globalConditionVariableWait;
extern freeConditionVariable globalConditionVariableFree;

//
// Mutex functionality
//
#define MUTEX_CREATE   globalCreateMutex
#define MUTEX_LOCK     globalLockMutex
#define MUTEX_UNLOCK   globalUnlockMutex
#define MUTEX_TRYLOCK  globalTryLockMutex
#define MUTEX_WAITLOCK globalWaitLockMutex
#define MUTEX_FREE     globalFreeMutex

//
// Condition variable functionality
//
#define CVAR_CREATE    globalConditionVariableCreate
#define CVAR_SIGNAL    globalConditionVariableSignal
#define CVAR_BROADCAST globalConditionVariableBroadcast
#define CVAR_WAIT      globalConditionVariableWait
#define CVAR_FREE      globalConditionVariableFree

//
// Static initializers
//
#define MUTEX_INIT           GLOBAL_MUTEX_INIT
#define MUTEX_INIT_RECURSIVE GLOBAL_MUTEX_INIT_RECURSIVE
#define CVAR_INIT            GLOBAL_CVAR_INIT

#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_MUTEX_INCLUDE__ */
