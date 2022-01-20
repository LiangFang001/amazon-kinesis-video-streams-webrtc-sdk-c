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

#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_THREAD_INCLUDE__ */
