
#ifndef __KINESIS_VIDEO_STATIC_CREDENTIAL_PROVIDER_INCLUDE_I__
#define __KINESIS_VIDEO_STATIC_CREDENTIAL_PROVIDER_INCLUDE_I__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/**
 * Creates a Static AWS credential provider object
 *
 * @param[in] PCHAR Access Key Id
 * @param[in] UINT32 Access Key Id Length excluding NULL terminator or 0 to calculate
 * @param[in] PCHAR Secret Key
 * @param[in] UINT32 Secret Key Length excluding NULL terminator or 0 to calculate
 * @param[in, out] PCHAR Session Token
 * @param[in, out] UINT32 Session Token Length excluding NULL terminator or 0 to calculate
 * @param[in] UINT64 Expiration in 100ns absolute time
 * @param[in, out] PAwsCredentialProvider* - OUT - Constructed AWS credentials provider object
 *
 * @return - STATUS code of the execution
 */
STATUS createStaticCredentialProvider(PCHAR, UINT32, PCHAR, UINT32, PCHAR, UINT32, UINT64, PAwsCredentialProvider*);
/**
 * Frees a Static Aws credential provider object
 *
 * @param[in, out] PAwsCredentialProvider* Object to be destroyed.
 *
 * @return - STATUS code of the execution
 */
STATUS freeStaticCredentialProvider(PAwsCredentialProvider*);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_STATIC_CREDENTIAL_PROVIDER_INCLUDE_I__ */
