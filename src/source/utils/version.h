/*******************************************
Auth internal include file
*******************************************/
#ifndef __KINESIS_VIDEO_VERSION_INCLUDE_I__
#define __KINESIS_VIDEO_VERSION_INCLUDE_I__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * IMPORTANT!!! This is the current version of the SDK which needs to be maintained
 */
#define AWS_SDK_KVS_PRODUCER_VERSION_STRING (PCHAR) "3.0.0"

/**
 * Default user agent string
 */
#define USER_AGENT_NAME (PCHAR) "AWS-SDK-KVS"

////////////////////////////////////////////////////
// Function definitions
////////////////////////////////////////////////////
/**
 * Creates a user agent string
 *
 * @param - PCHAR - IN - User agent name
 * @param - PCHAR - IN - Custom user agent string
 * @param - UINT32 - IN - Length of the string
 * @param - PCHAR - OUT - Combined user agent string
 *
 * @return - STATUS code of the execution
 */
STATUS getUserAgentString(PCHAR, PCHAR, UINT32, PCHAR);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_VERSION_INCLUDE_I__ */
