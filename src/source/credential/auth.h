/*******************************************
Auth internal include file
*******************************************/
#ifndef __KINESIS_VIDEO_AUTH_INCLUDE_I__
#define __KINESIS_VIDEO_AUTH_INCLUDE_I__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "kvs/common.h"
////////////////////////////////////////////////////
// Function definitions
////////////////////////////////////////////////////
/**
 * Creates an AWS credentials object
 *
 * @param - PCHAR - IN - Access Key Id
 * @param - UINT32 - IN - Access Key Id Length excluding NULL terminator or 0 to calculate
 * @param - PCHAR - IN - Secret Key
 * @param - UINT32 - IN - Secret Key Length excluding NULL terminator or 0 to calculate
 * @param - PCHAR - IN/OPT - Session Token
 * @param - UINT32 - IN/OPT - Session Token Length excluding NULL terminator or 0 to calculate
 * @param - UINT64 - IN - Expiration in 100ns absolute time
 * @param - PAwsCredentials* - OUT - Constructed object
 *
 * @return - STATUS code of the execution
 */
STATUS createAwsCredentials(PCHAR, UINT32, PCHAR, UINT32, PCHAR, UINT32, UINT64, PAwsCredentials*);

/**
 * Deserialize an AWS credentials object, adapt the accessKey/secretKey/sessionToken pointer
 * to offset following the AwsCredential structure
 *
 * @param - PBYTE - IN - Token to be deserialized.
 *
 * @return - STATUS code of the execution
 */
STATUS deserializeAwsCredentials(PBYTE);
/**
 * Frees an Aws credentials object
 *
 * @param - PAwsCredentials* - IN/OUT - Object to be destroyed.
 *
 * @return - STATUS code of the execution
 */
STATUS freeAwsCredentials(PAwsCredentials*);
#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_AUTH_INCLUDE_I__ */
