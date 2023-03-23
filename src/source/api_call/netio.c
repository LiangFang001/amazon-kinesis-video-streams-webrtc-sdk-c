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
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <sys/time.h>
#include <sys/socket.h>

/* Thirdparty headers */
//#include "azure_c_shared_utility/xlogging.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#if (MBEDTLS_VERSION_NUMBER != 0x03000000 && MBEDTLS_VERSION_NUMBER != 0x03020100)
#include "mbedtls/net.h"
#endif
#include "mbedtls/net_sockets.h"

/* Public headers */
#include "kvs/error.h"
#include "kvs/platform_utils.h"
/* Internal headers */
#include "netio.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
#define DEFAULT_CONNECTION_TIMEOUT_MS (10 * 1000)

typedef struct NetIo {
    /* Basic ssl connection parameters */
    mbedtls_net_context xFd;
    mbedtls_ssl_context xSsl;
    mbedtls_ssl_config xConf;
    mbedtls_ctr_drbg_context xCtrDrbg;
    mbedtls_entropy_context xEntropy;

    /* Variables for IoT credential provider. It's optional feature so we declare them as pointers. */
    mbedtls_x509_crt* pRootCA;
    mbedtls_x509_crt* pCert;
    mbedtls_pk_context* pPrivKey;

    /* Options */
    uint32_t uRecvTimeoutMs;
    uint32_t uSendTimeoutMs;
} NetIo_t;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
static int prvCreateX509Cert(NetIo_t* pxNet)
{
    int xRes = STATUS_SUCCESS;

    if (pxNet == NULL || (pxNet->pRootCA = (mbedtls_x509_crt*) MEMALLOC(sizeof(mbedtls_x509_crt))) == NULL ||
        (pxNet->pCert = (mbedtls_x509_crt*) MEMALLOC(sizeof(mbedtls_x509_crt))) == NULL ||
        (pxNet->pPrivKey = (mbedtls_pk_context*) MEMALLOC(sizeof(mbedtls_pk_context))) == NULL) {
        xRes = STATUS_NULL_ARG;
    } else {
        mbedtls_x509_crt_init(pxNet->pRootCA);
        mbedtls_x509_crt_init(pxNet->pCert);
        mbedtls_pk_init(pxNet->pPrivKey);
    }

    return xRes;
}

#if (MBEDTLS_VERSION_NUMBER == 0x03000000 || MBEDTLS_VERSION_NUMBER == 0x03020100)
static int mbedtls_test_rnd_std_rand(void* rng_state, unsigned char* output, size_t len)
{
#if !defined(__OpenBSD__) && !defined(__NetBSD__)
    size_t i;

    if (rng_state != NULL)
        rng_state = NULL;

    for (i = 0; i < len; ++i)
        output[i] = rand();
#else
    if (rng_state != NULL)
        rng_state = NULL;

    arc4random_buf(output, len);
#endif /* !OpenBSD && !NetBSD */

    return (0);
}
#endif

static int prvInitConfig(NetIo_t* pxNet, const char* pcRootCA, const char* pcCert, const char* pcPrivKey, bool bFilePath)
{
    int xRes = STATUS_SUCCESS;

    if (pxNet == NULL) {
        xRes = STATUS_NULL_ARG;
    } else {
        mbedtls_ssl_set_bio(&(pxNet->xSsl), &(pxNet->xFd), mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
        if (mbedtls_ssl_config_defaults(&(pxNet->xConf), MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
            DLOGE("Failed to config ssl");
            xRes = STATUS_NULL_ARG;
        } else {
            mbedtls_ssl_conf_rng(&(pxNet->xConf), mbedtls_ctr_drbg_random, &(pxNet->xCtrDrbg));
            mbedtls_ssl_conf_read_timeout(&(pxNet->xConf), pxNet->uRecvTimeoutMs);
            NetIo_setSendTimeout(pxNet, pxNet->uSendTimeoutMs);

            if (pcRootCA != NULL && pcCert != NULL && pcPrivKey != NULL) {
                if (bFilePath == false &&
                    (mbedtls_x509_crt_parse(pxNet->pRootCA, (void*) pcRootCA, strlen(pcRootCA) + 1) != 0 ||
                     mbedtls_x509_crt_parse(pxNet->pCert, (void*) pcCert, strlen(pcCert) + 1) != 0 ||
#if (MBEDTLS_VERSION_NUMBER == 0x03000000 || MBEDTLS_VERSION_NUMBER == 0x03020100)
                     mbedtls_pk_parse_key(pxNet->pPrivKey, (void*) pcPrivKey, strlen(pcPrivKey) + 1, NULL, 0, mbedtls_test_rnd_std_rand, NULL) !=
                         0)) {
#else
                     mbedtls_pk_parse_key(pxNet->pPrivKey, (void*) pcPrivKey, strlen(pcPrivKey) + 1, NULL, 0) != 0)) {
#endif
                    DLOGE("Failed to parse x509");
                    xRes = STATUS_NULL_ARG;
                } else if (mbedtls_x509_crt_parse_file(pxNet->pRootCA, (void*) pcRootCA) != 0 ||
                           mbedtls_x509_crt_parse_file(pxNet->pCert, (void*) pcCert) != 0 ||
#if (MBEDTLS_VERSION_NUMBER == 0x03000000 || MBEDTLS_VERSION_NUMBER == 0x03020100)
                           mbedtls_pk_parse_keyfile(pxNet->pPrivKey, (void*) pcPrivKey, NULL, mbedtls_test_rnd_std_rand, NULL) != 0) {
#else
                           mbedtls_pk_parse_keyfile(pxNet->pPrivKey, (void*) pcPrivKey, NULL) != 0) {
#endif
                } else {
                    mbedtls_ssl_conf_authmode(&(pxNet->xConf), MBEDTLS_SSL_VERIFY_REQUIRED);
                    mbedtls_ssl_conf_ca_chain(&(pxNet->xConf), pxNet->pRootCA, NULL);

                    if (mbedtls_ssl_conf_own_cert(&(pxNet->xConf), pxNet->pCert, pxNet->pPrivKey) != 0) {
                        DLOGE("Failed to conf own cert");
                        xRes = STATUS_NULL_ARG;
                    }
                }
            } else {
                mbedtls_ssl_conf_authmode(&(pxNet->xConf), MBEDTLS_SSL_VERIFY_OPTIONAL);
            }
        }
    }

    if (xRes == STATUS_SUCCESS) {
        if (mbedtls_ssl_setup(&(pxNet->xSsl), &(pxNet->xConf)) != 0) {
            DLOGE("Failed to setup ssl");
            xRes = STATUS_NULL_ARG;
        }
    }

    return xRes;
}

static int prvConnect(NetIo_t* pxNet, const char* pcHost, const char* pcPort, const char* pcRootCA, const char* pcCert, const char* pcPrivKey,
                      bool bFilePath)
{
    int xRes = STATUS_SUCCESS;
    int ret = 0;

    if (pxNet == NULL || pcHost == NULL || pcPort == NULL) {
        DLOGE("Invalid argument");
        xRes = STATUS_NULL_ARG;
    } else if ((pcRootCA != NULL && pcCert != NULL && pcPrivKey != NULL) && prvCreateX509Cert(pxNet) != STATUS_SUCCESS) {
        DLOGE("Failed to init x509");
        xRes = STATUS_NULL_ARG;
    } else if ((ret = mbedtls_net_connect(&(pxNet->xFd), pcHost, pcPort, MBEDTLS_NET_PROTO_TCP)) != 0) {
        DLOGE("Failed to connect to %s:%s", pcHost, pcPort);
        xRes = STATUS_NULL_ARG;
    } else if ((ret = mbedtls_ssl_set_hostname(&(pxNet->xSsl), pcHost)) != 0) {
        DLOGE("Failed to set hostname %s", pcHost);
        xRes = STATUS_NULL_ARG;
    } else if (prvInitConfig(pxNet, pcRootCA, pcCert, pcPrivKey, bFilePath) != STATUS_SUCCESS) {
        DLOGE("Failed to config ssl");
        xRes = STATUS_NULL_ARG;
    } else if ((ret = mbedtls_ssl_handshake(&(pxNet->xSsl))) != 0) {
        DLOGE("ssl handshake err (%d)", ret);
        xRes = STATUS_NULL_ARG;
    } else {
        /* nop */
    }
    return xRes;
}

NetIoHandle NetIo_create(void)
{
    NetIo_t* pxNet = NULL;

    if ((pxNet = (NetIo_t*) MEMALLOC(sizeof(NetIo_t))) != NULL) {
        memset(pxNet, 0, sizeof(NetIo_t));

        mbedtls_net_init(&(pxNet->xFd));
        mbedtls_ssl_init(&(pxNet->xSsl));
        mbedtls_ssl_config_init(&(pxNet->xConf));
        mbedtls_ctr_drbg_init(&(pxNet->xCtrDrbg));
        mbedtls_entropy_init(&(pxNet->xEntropy));

        pxNet->uRecvTimeoutMs = DEFAULT_CONNECTION_TIMEOUT_MS;
        pxNet->uSendTimeoutMs = DEFAULT_CONNECTION_TIMEOUT_MS;

        if (mbedtls_ctr_drbg_seed(&(pxNet->xCtrDrbg), mbedtls_entropy_func, &(pxNet->xEntropy), NULL, 0) != 0) {
            NetIo_terminate(pxNet);
            pxNet = NULL;
        }
    }

    return pxNet;
}

void NetIo_terminate(NetIoHandle xNetIoHandle)
{
    NetIo_t* pxNet = (NetIo_t*) xNetIoHandle;

    if (pxNet != NULL) {
        mbedtls_ctr_drbg_free(&(pxNet->xCtrDrbg));
        mbedtls_entropy_free(&(pxNet->xEntropy));
        mbedtls_net_free(&(pxNet->xFd));
        mbedtls_ssl_free(&(pxNet->xSsl));
        mbedtls_ssl_config_free(&(pxNet->xConf));

        if (pxNet->pRootCA != NULL) {
            mbedtls_x509_crt_free(pxNet->pRootCA);
            SAFE_MEMFREE(pxNet->pRootCA);
            pxNet->pRootCA = NULL;
        }

        if (pxNet->pCert != NULL) {
            mbedtls_x509_crt_free(pxNet->pCert);
            SAFE_MEMFREE(pxNet->pCert);
            pxNet->pCert = NULL;
        }

        if (pxNet->pPrivKey != NULL) {
            mbedtls_pk_free(pxNet->pPrivKey);
            SAFE_MEMFREE(pxNet->pPrivKey);
            pxNet->pPrivKey = NULL;
        }
        SAFE_MEMFREE(pxNet);
    }
}

int NetIo_connect(NetIoHandle xNetIoHandle, const char* pcHost, const char* pcPort)
{
    return prvConnect(xNetIoHandle, pcHost, pcPort, NULL, NULL, NULL, false);
}

int NetIo_connectWithX509(NetIoHandle xNetIoHandle, const char* pcHost, const char* pcPort, const char* pcRootCA, const char* pcCert,
                          const char* pcPrivKey)
{
    return prvConnect(xNetIoHandle, pcHost, pcPort, pcRootCA, pcCert, pcPrivKey, false);
}

int NetIo_connectWithX509Path(NetIoHandle xNetIoHandle, const char* pcHost, const char* pcPort, const char* pcRootCA, const char* pcCert,
                              const char* pcPrivKey)
{
    return prvConnect(xNetIoHandle, pcHost, pcPort, pcRootCA, pcCert, pcPrivKey, true);
}

void NetIo_disconnect(NetIoHandle xNetIoHandle)
{
    NetIo_t* pxNet = (NetIo_t*) xNetIoHandle;

    if (pxNet != NULL) {
        mbedtls_ssl_close_notify(&(pxNet->xSsl));
    }
}

int NetIo_send(NetIoHandle xNetIoHandle, const unsigned char* pBuffer, size_t uBytesToSend)
{
    int n = 0;
    int xRes = STATUS_SUCCESS;
    NetIo_t* pxNet = (NetIo_t*) xNetIoHandle;
    size_t uBytesRemaining = uBytesToSend;
    char* pIndex = (char*) pBuffer;

    if (pxNet == NULL || pBuffer == NULL) {
        xRes = -1;
        DLOGB("NetIo_send null arg");
    } else {
        do {
            n = mbedtls_ssl_write(&(pxNet->xSsl), (const unsigned char*) pIndex, uBytesRemaining);
            if (n < 0 || n > uBytesRemaining) {
                DLOGE("SSL send error %d", n);
                xRes = n;
                break;
            }
            uBytesRemaining -= n;
            pIndex += n;
        } while (uBytesRemaining > 0);
    }

    return xRes;
}

int NetIo_recv(NetIoHandle xNetIoHandle, unsigned char* pBuffer, size_t uBufferSize, size_t* puBytesReceived)
{
    int n;
    int xRes = STATUS_SUCCESS;
    NetIo_t* pxNet = (NetIo_t*) xNetIoHandle;

    if (pxNet == NULL || pBuffer == NULL || puBytesReceived == NULL) {
        xRes = STATUS_NULL_ARG;
    } else {
        n = mbedtls_ssl_read(&(pxNet->xSsl), pBuffer, uBufferSize);
        if (n < 0 || n > uBufferSize) {
            // DLOGE("SSL recv error %d", n);
            xRes = n;
            *puBytesReceived = 0;
        } else {
            *puBytesReceived = n;
        }
    }

    return xRes;
}

bool NetIo_isDataAvailable(NetIoHandle xNetIoHandle)
{
    int xRes = STATUS_SUCCESS;
    NetIo_t* pxNet = (NetIo_t*) xNetIoHandle;
    bool bDataAvailable = false;
    struct timeval tv = {0};
    fd_set read_fds = {0};
    int fd = 0;

    if (pxNet == NULL) {
        xRes = STATUS_NULL_ARG;
    } else {
        fd = pxNet->xFd.fd;
        if (fd >= 0) {
            FD_ZERO(&read_fds);
            FD_SET(fd, &read_fds);

            tv.tv_sec = 0;
            tv.tv_usec = 0;

            if (select(fd + 1, &read_fds, NULL, NULL, &tv) >= 0) {
                if (FD_ISSET(fd, &read_fds)) {
                    bDataAvailable = true;
                }
            }
        }
    }

    return bDataAvailable;
}

int NetIo_setRecvTimeout(NetIoHandle xNetIoHandle, unsigned int uRecvTimeoutMs)
{
    int xRes = STATUS_SUCCESS;
    NetIo_t* pxNet = (NetIo_t*) xNetIoHandle;

    if (pxNet == NULL) {
        xRes = STATUS_NULL_ARG;
    } else {
        pxNet->uRecvTimeoutMs = (uint32_t) uRecvTimeoutMs;
        mbedtls_ssl_conf_read_timeout(&(pxNet->xConf), pxNet->uRecvTimeoutMs);
    }

    return xRes;
}

int NetIo_setSendTimeout(NetIoHandle xNetIoHandle, unsigned int uSendTimeoutMs)
{
    int xRes = STATUS_SUCCESS;
    NetIo_t* pxNet = (NetIo_t*) xNetIoHandle;
    int fd = 0;
    struct timeval tv = {0};

    if (pxNet == NULL) {
        xRes = STATUS_NULL_ARG;
    } else {
        pxNet->uSendTimeoutMs = (uint32_t) uSendTimeoutMs;
        fd = pxNet->xFd.fd;
        tv.tv_sec = uSendTimeoutMs / 1000;
        tv.tv_usec = (uSendTimeoutMs % 1000) * 1000;

        if (fd < 0) {
            /* Do nothing when connection hasn't established. */
        } else if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (void*) &tv, sizeof(tv)) != 0) {
            xRes = STATUS_NULL_ARG;
        } else {
            /* nop */
        }
    }

    return xRes;
}

uint32_t NetIo_getSocket(NetIoHandle xNetIoHandle)
{
    return xNetIoHandle->xFd.fd;
}
