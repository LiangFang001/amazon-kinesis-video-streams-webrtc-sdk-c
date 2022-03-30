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
#ifndef __AWS_KVS_WEBRTC_THREAD_INCLUDE__
#define __AWS_KVS_WEBRTC_THREAD_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif
// thread stack size to use when running on constrained device like raspberry pi
#define THREAD_STACK_SIZE_ON_CONSTRAINED_DEVICE (512 * 1024)

#if defined(KVS_PLAT_RTK_FREERTOS)
#define DEFAULT_THREAD_SIZE 4096
#define DEFAULT_THREAD_NAME "pthread"
#endif

// Max thread name buffer length - similar to Linux platforms
#ifndef MAX_THREAD_NAME
#define MAX_THREAD_NAME 16
#endif

// Thread Id
typedef UINT64 TID;
typedef TID* PTID;

#ifndef INVALID_TID_VALUE
#define INVALID_TID_VALUE ((UINT64) NULL)
#endif

#ifndef IS_VALID_TID_VALUE
#define IS_VALID_TID_VALUE(t) ((t) != INVALID_TID_VALUE)
#endif

//
// Thread library function definitions
//
typedef TID (*getTId)();
typedef STATUS (*getTName)(TID, PCHAR, UINT32);
//
// Thread library function definitions
//
typedef PVOID (*startRoutine)(PVOID);
typedef STATUS (*createThread)(PTID, startRoutine, PVOID);
typedef STATUS (*createThreadEx)(PTID, PCHAR, UINT32, BOOL, startRoutine, PVOID);
typedef STATUS (*joinThread)(TID, PVOID*);
typedef VOID (*threadSleep)(UINT64);
typedef VOID (*threadSleepUntil)(UINT64);
typedef STATUS (*cancelThread)(TID);
typedef STATUS (*detachThread)(TID);
typedef VOID (*exitThread)(PVOID);

//
// Thread related functionality
//
extern getTId globalGetThreadId;
extern getTName globalGetThreadName;

//
// Thread and Mutex related functionality
//
extern createThread globalCreateThread;
extern createThreadEx globalCreateThreadEx;
extern joinThread globalJoinThread;
extern threadSleep globalThreadSleep;
extern threadSleepUntil globalThreadSleepUntil;
extern cancelThread globalCancelThread;
extern detachThread globalDetachThread;
extern exitThread globalExitThread;

//
// Thread functionality
//
#define GETTID   globalGetThreadId
#define GETTNAME globalGetThreadName

//
// Thread functionality
//
#define THREAD_CREATE        globalCreateThread
#define THREAD_CREATE_EX     globalCreateThreadEx
#define THREAD_CREATE_EX_PRI globalCreateThreadExPri
#define THREAD_JOIN          globalJoinThread
#define THREAD_SLEEP         globalThreadSleep
#define THREAD_SLEEP_UNTIL   globalThreadSleepUntil
#define THREAD_CANCEL        globalCancelThread
#define THREAD_DETACH        globalDetachThread
#define THREAD_EXIT          globalExitThread

#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_THREAD_INCLUDE__ */
