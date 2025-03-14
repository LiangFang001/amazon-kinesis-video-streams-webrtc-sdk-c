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
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_CRYPTO_CRYPTO__
#define __KINESIS_VIDEO_WEBRTC_CLIENT_CRYPTO_CRYPTO__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#ifdef KVS_USE_OPENSSL
// TBD
#elif KVS_USE_MBEDTLS
#include <mbedtls/ssl.h>
#include <mbedtls/sha256.h>
#include <mbedtls/md5.h>
#include <mbedtls/error.h>
#if (MBEDTLS_VERSION_NUMBER==0x03000000 || MBEDTLS_VERSION_NUMBER==0x03020100)
#include <mbedtls/compat-2.x.h>
#endif
#endif

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
#ifdef KVS_USE_OPENSSL
#define KVS_RSA_F4                  RSA_F4
#define KVS_MD5_DIGEST_LENGTH       MD5_DIGEST_LENGTH
#define KVS_SHA1_DIGEST_LENGTH      SHA_DIGEST_LENGTH
#define KVS_MD5_DIGEST(m, mlen, ob) MD5((m), (mlen), (ob));
#define KVS_HMAC(k, klen, m, mlen, ob, plen)                                                                                                         \
    CHK(NULL != HMAC(EVP_sha256(), (k), (INT32)(klen), (m), (mlen), (ob), (plen)), STATUS_HMAC_GENERATION_ERROR);
#define KVS_SHA1_HMAC(k, klen, m, mlen, ob, plen)                                                                                                    \
    CHK(NULL != HMAC(EVP_sha1(), (k), (INT32)(klen), (m), (mlen), (ob), (plen)), STATUS_HMAC_GENERATION_ERROR);
#define KVS_SHA256(m, mlen, ob) SHA256((m), (mlen), (ob));
#define KVS_CRYPTO_INIT()                                                                                                                            \
    do {                                                                                                                                             \
        OpenSSL_add_ssl_algorithms();                                                                                                                \
        SSL_load_error_strings();                                                                                                                    \
        SSL_library_init();                                                                                                                          \
    } while (0)
#define LOG_OPENSSL_ERROR(s)                                                                                                                         \
    while ((sslErr = ERR_get_error()) != 0) {                                                                                                        \
        if (sslErr != SSL_ERROR_WANT_WRITE && sslErr != SSL_ERROR_WANT_READ) {                                                                       \
            DLOGW("%s failed with %s", (s), ERR_error_string(sslErr, NULL));                                                                         \
        }                                                                                                                                            \
    }

typedef enum {
    KVS_SRTP_PROFILE_AES128_CM_HMAC_SHA1_80 = SRTP_AES128_CM_SHA1_80,
    KVS_SRTP_PROFILE_AES128_CM_HMAC_SHA1_32 = SRTP_AES128_CM_SHA1_32,
} KVS_SRTP_PROFILE;
#elif KVS_USE_MBEDTLS
#define KVS_RSA_F4                  0x10001L
#define KVS_MD5_DIGEST_LENGTH       16
#define KVS_SHA1_DIGEST_LENGTH      20
#define KVS_MD5_DIGEST(m, mlen, ob) mbedtls_md5_ret((m), (mlen), (ob));
#define KVS_HMAC(k, klen, m, mlen, ob, plen)                                                                                                         \
    CHK(0 == mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (k), (klen), (m), (mlen), (ob)), STATUS_HMAC_GENERATION_ERROR);           \
    *(plen) = mbedtls_md_get_size(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256));
#define KVS_SHA1_HMAC(k, klen, m, mlen, ob, plen)                                                                                                    \
    CHK(0 == mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), (k), (klen), (m), (mlen), (ob)), STATUS_HMAC_GENERATION_ERROR);             \
    *(plen) = mbedtls_md_get_size(mbedtls_md_info_from_type(MBEDTLS_MD_SHA1));
#define KVS_SHA256(m, mlen, ob) mbedtls_sha256((m), (mlen), (ob), 0);
#define KVS_CRYPTO_INIT()                                                                                                                            \
    do {                                                                                                                                             \
    } while (0)
#define LOG_MBEDTLS_ERROR(s, ret)                                                                                                                    \
    do {                                                                                                                                             \
        CHAR __mbedtlsErr[1024];                                                                                                                     \
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {                                                                 \
            mbedtls_strerror(ret, __mbedtlsErr, SIZEOF(__mbedtlsErr));                                                                               \
            DLOGW("%s failed with %s", (s), __mbedtlsErr);                                                                                           \
        }                                                                                                                                            \
    } while (0)

typedef enum {
#if (MBEDTLS_VERSION_NUMBER==0x03000000 || MBEDTLS_VERSION_NUMBER==0x03020100)
    KVS_SRTP_PROFILE_AES128_CM_HMAC_SHA1_80 = MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80,
    KVS_SRTP_PROFILE_AES128_CM_HMAC_SHA1_32 = MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32,
#else
    KVS_SRTP_PROFILE_AES128_CM_HMAC_SHA1_80 = MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_80,
    KVS_SRTP_PROFILE_AES128_CM_HMAC_SHA1_32 = MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_32,
#endif
} KVS_SRTP_PROFILE;
#else
#error "A Crypto implementation is required."
#endif

#ifdef __cplusplus
}
#endif
#endif //__KINESIS_VIDEO_WEBRTC_CLIENT_CRYPTO_CRYPTO__
