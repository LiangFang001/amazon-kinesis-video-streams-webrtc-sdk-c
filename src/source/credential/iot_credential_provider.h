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
#ifndef __KINESIS_VIDEO_IOT_CREDENTIAL_PROVIDER_INCLUDE_I__
#define __KINESIS_VIDEO_IOT_CREDENTIAL_PROVIDER_INCLUDE_I__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#define IOT_REQUEST_CONNECTION_TIMEOUT (3 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define IOT_REQUEST_COMPLETION_TIMEOUT (5 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define ROLE_ALIASES_PATH              ((PCHAR) "/role-aliases")
#define CREDENTIAL_SERVICE             ((PCHAR) "/credentials")
#define IOT_THING_NAME_HEADER          "x-amzn-iot-thingname"
/**
 * Grace period which is added to the current time to determine whether the extracted credentials are still valid
 */
#define IOT_CREDENTIAL_FETCH_GRACE_PERIOD                                                                                                            \
    (5 * HUNDREDS_OF_NANOS_IN_A_SECOND + MIN_STREAMING_TOKEN_EXPIRATION_DURATION + STREAMING_TOKEN_EXPIRATION_GRACE_PERIOD)

/**
 * Service call callback functionality
 */
typedef STATUS (*BlockingServiceCallFunc)(PRequestInfo, PCallInfo);

typedef struct __IotCredentialProvider {
    // First member should be the abstract credential provider
    AwsCredentialProvider credentialProvider;

    // Current time functionality - optional
    GetCurrentTimeFunc getCurrentTimeFn;

    // Custom data supplied to time function
    UINT64 customData;

    // IoT credential endpoint
    CHAR iotGetCredentialEndpoint[MAX_URI_CHAR_LEN + 1];

    // IoT certificate file path
    CHAR certPath[MAX_PATH_LEN + 1];

    // IoT private key file path
    CHAR privateKeyPath[MAX_PATH_LEN + 1];

    // CA certificate file path
    CHAR caCertPath[MAX_PATH_LEN + 1];

    // IoT role alias
    CHAR roleAlias[MAX_ROLE_ALIAS_LEN + 1];

    // String name is used as IoT thing-name
    CHAR thingName[MAX_IOT_THING_NAME_LEN + 1];

    // Static Aws Credentials structure with the pointer following the main allocation
    PAwsCredentials pAwsCredentials;

    // Service call functionality
    BlockingServiceCallFunc serviceCallFn;
} IotCredentialProvider, *PIotCredentialProvider;

/**
 * Creates an IoT based AWS credential provider object using libWebSockets
 *
 * @param[in] PCHAR IoT endpoint
 * @param[in] PCHAR Cert file path
 * @param[in] PCHAR Private key file path
 * @param[in, out] PCHAR CA cert file path
 * @param[in] PCHAR Role alias
 * @param[in] PCHAR IoT thing name
 * @param[in, out] PAwsCredentialProvider* Constructed AWS credentials provider object
 *
 * @return STATUS code of the execution.
 */
STATUS iot_credential_provider_create(PCHAR, PCHAR, PCHAR, PCHAR, PCHAR, PCHAR, PAwsCredentialProvider*);

/**
 * Frees an IoT based Aws credential provider object
 *
 * @param[in, out] PAwsCredentialProvider* Object to be destroyed.
 *
 * @return STATUS code of the execution.
 */
STATUS iot_credential_provider_free(PAwsCredentialProvider*);
#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_IOT_CREDENTIAL_PROVIDER_INCLUDE_I__ */
