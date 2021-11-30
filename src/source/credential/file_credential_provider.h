
#ifndef __KINESIS_VIDEO_FILE_CREDENTIAL_PROVIDER_INCLUDE_I__
#define __KINESIS_VIDEO_FILE_CREDENTIAL_PROVIDER_INCLUDE_I__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Grace period which is added to the current time to determine whether the extracted credentials are still valid
 */
#define CREDENTIAL_FILE_READ_GRACE_PERIOD                                                                                                            \
    (5 * HUNDREDS_OF_NANOS_IN_A_SECOND + MIN_STREAMING_TOKEN_EXPIRATION_DURATION + STREAMING_TOKEN_EXPIRATION_GRACE_PERIOD)

/**
 * Creates a File based AWS credential provider object
 *
 * @param - PCHAR - IN - Credentials file path
 * @param - PAwsCredentialProvider* - OUT - Constructed AWS credentials provider object
 *
 * @return - STATUS code of the execution
 */
STATUS createFileCredentialProvider(PCHAR, PAwsCredentialProvider*);
/**
 * Creates a File based AWS credential provider object
 *
 * @param - PCHAR - IN - Credentials file path
 * @param - GetCurrentTimeFunc - IN - Current time function
 * @param - UINT64 - IN - Time function custom data
 * @param - PAwsCredentialProvider* - OUT - Constructed AWS credentials provider object
 *
 * @return - STATUS code of the execution
 */
STATUS createFileCredentialProviderWithTime(PCHAR, GetCurrentTimeFunc, UINT64, PAwsCredentialProvider*);
/**
 * Frees a File based Aws credential provider object
 *
 * @param - PAwsCredentialProvider* - IN/OUT - Object to be destroyed.
 *
 * @return - STATUS code of the execution
 */
STATUS freeFileCredentialProvider(PAwsCredentialProvider*);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_FILE_CREDENTIAL_PROVIDER_INCLUDE_I__ */
