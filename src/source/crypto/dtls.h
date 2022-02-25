//
// Dtls
//

#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_DTLS_DTLS__
#define __KINESIS_VIDEO_WEBRTC_CLIENT_DTLS_DTLS__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "timer_queue.h"
#include "crypto.h"
#include "network.h"
#include "io_buffer.h"

#ifdef KVS_USE_OPENSSL
// TBD
#elif KVS_USE_MBEDTLS
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#endif

#define MAX_SRTP_MASTER_KEY_LEN   16
#define MAX_SRTP_SALT_KEY_LEN     14
#define MAX_DTLS_RANDOM_BYTES_LEN 32
#define MAX_DTLS_MASTER_KEY_LEN   48

#define GENERATED_CERTIFICATE_MAX_SIZE 4096
#define GENERATED_CERTIFICATE_BITS     2048
#define GENERATED_CERTIFICATE_SERIAL   1
#define GENERATED_CERTIFICATE_DAYS     365
#define GENERATED_CERTIFICATE_NAME     "KVS-WebRTC-Client"
#define KEYING_EXTRACTOR_LABEL         "EXTRACTOR-dtls_srtp"

/*
 * DTLS transmission interval timer (in 100ns)
 */
#define DTLS_TRANSMISSION_INTERVAL (200 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND)

#define DTLS_SESSION_TIMER_START_DELAY (100 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND)

#define SECONDS_IN_A_DAY (24 * 60 * 60LL)

#define HUNDREDS_OF_NANOS_IN_A_DAY (HUNDREDS_OF_NANOS_IN_AN_HOUR * 24LL)

typedef enum {
    RTC_DTLS_TRANSPORT_STATE_NEW,
    RTC_DTLS_TRANSPORT_STATE_CONNECTING, /* DTLS is in the process of negotiating a secure connection and verifying the remote fingerprint. */
    RTC_DTLS_TRANSPORT_STATE_CONNECTED,  /* DTLS has completed negotiation of a secure connection and verified the remote fingerprint. */
    RTC_DTLS_TRANSPORT_STATE_CLOSED,     /* The transport has been closed intentionally as the result of receipt of a close_notify alert */
    RTC_DTLS_TRANSPORT_STATE_FAILED,     /* The transport has failed as the result of an error */
} RTC_DTLS_TRANSPORT_STATE;

/* Callback that is fired when Dtls Server wishes to send packet */
typedef VOID (*DtlsSessionOutboundPacketFunc)(UINT64, PBYTE, UINT32);

/*  Callback that is fired when Dtls state has changed */
typedef VOID (*DtlsSessionOnStateChange)(UINT64, RTC_DTLS_TRANSPORT_STATE);

typedef struct {
    UINT64 outBoundPacketFnCustomData;
    DtlsSessionOutboundPacketFunc outboundPacketFn; //!< outBoundPacketFn is a required callback to tell DtlsSession how to send outbound packets
    UINT64 stateChangeFnCustomData;
    DtlsSessionOnStateChange stateChangeFn;
} DtlsSessionCallbacks, *PDtlsSessionCallbacks;

// DtlsKeyingMaterial is information extracted via https://tools.ietf.org/html/rfc5705
// also includes the use_srtp value from Handshake
typedef struct {
    BYTE clientWriteKey[MAX_SRTP_MASTER_KEY_LEN + MAX_SRTP_SALT_KEY_LEN];
    BYTE serverWriteKey[MAX_SRTP_MASTER_KEY_LEN + MAX_SRTP_SALT_KEY_LEN];
    UINT8 key_length;

    KVS_SRTP_PROFILE srtpProfile;
} DtlsKeyingMaterial, *PDtlsKeyingMaterial;

#ifdef KVS_USE_OPENSSL
typedef struct {
    BOOL created;
    X509* pCert;
    EVP_PKEY* pKey;
} DtlsSessionCertificateInfo, *PDtlsSessionCertificateInfo;

#elif KVS_USE_MBEDTLS
typedef struct {
    mbedtls_x509_crt cert;
    mbedtls_pk_context privateKey;
    CHAR fingerprint[CERTIFICATE_FINGERPRINT_LENGTH + 1];
} DtlsSessionCertificateInfo, *PDtlsSessionCertificateInfo;

typedef struct {
    UINT64 updatedTime;
    UINT32 intermediateDelay, finalDelay;
} DtlsSessionTimer, *PDtlsSessionTimer;

typedef struct {
    BYTE masterSecret[MAX_DTLS_MASTER_KEY_LEN];
    // client random bytes + server random bytes
    BYTE randBytes[2 * MAX_DTLS_RANDOM_BYTES_LEN];
    mbedtls_tls_prf_types tlsProfile;
} TlsKeys, *PTlsKeys;
#else
#error "A Crypto implementation is required."
#endif

typedef struct __DtlsSession DtlsSession, *PDtlsSession;
struct __DtlsSession {
    volatile ATOMIC_BOOL isStarted;
    volatile ATOMIC_BOOL shutdown;
    UINT32 certificateCount;
    DtlsSessionCallbacks dtlsSessionCallbacks;
    TIMER_QUEUE_HANDLE timerQueueHandle;
    UINT32 timerId;
    UINT64 dtlsSessionStartTime;
    RTC_DTLS_TRANSPORT_STATE state;
    MUTEX nestedDtlsLock;

#ifdef KVS_USE_OPENSSL
    volatile ATOMIC_BOOL sslInitFinished;
    // dtls message must fit into a UDP packet
    BYTE outgoingDataBuffer[MAX_UDP_PACKET_SIZE];
    UINT32 outgoingDataLen;
    CHAR certFingerprints[MAX_RTCCONFIGURATION_CERTIFICATES][CERTIFICATE_FINGERPRINT_LENGTH + 1];
    SSL_CTX* pSslCtx;
    SSL* pSsl;
#elif KVS_USE_MBEDTLS
    DtlsSessionTimer transmissionTimer;
    TlsKeys tlsKeys;
    PIOBuffer pReadBuffer;

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctrDrbg;
    mbedtls_ssl_config sslCtxConfig;
    mbedtls_ssl_context sslCtx;
    DtlsSessionCertificateInfo certificates[MAX_RTCCONFIGURATION_CERTIFICATES];
#else
#error "A Crypto implementation is required."
#endif
};

/**
 * @brief Create DTLS session. Not thread safe.
 *
 * @param[in] PDtlsSessionCallbacks - callbacks
 * @param[in] TIMER_QUEUE_HANDLE - timer handle to schedule timer task with
 * @param[in] INT32 - size of generated certificate
 * @param[in] BOOL - whether to generate certificate or not
 * @param[in] PRtcCertificate - user provided certificate
 * @param[in] PDtlsSession* - pointer to created DtlsSession object
 *
 * @return STATUS - status of operation
 */
STATUS dtls_session_create(PDtlsSessionCallbacks, TIMER_QUEUE_HANDLE, INT32, BOOL, PRtcCertificate, PDtlsSession*);

/**
 * Free DTLS session. Not thread safe.
 * @param PDtlsSession - DtlsSession object to free
 * @return STATUS - status of operation
 */
STATUS dtls_session_free(PDtlsSession*);

/**
 * Start DTLS handshake. Not thread safe.
 * @param PDtlsSession - DtlsSession object
 * @param BOOL - is server
 * @return STATUS - status of operation
 */
STATUS dtls_session_start(PDtlsSession, BOOL);
STATUS dtls_session_read(PDtlsSession, PBYTE, PINT32);
STATUS dtls_session_isInitFinished(PDtlsSession, PBOOL);
STATUS dtls_session_populateKeyingMaterial(PDtlsSession, PDtlsKeyingMaterial);
STATUS dtls_session_getLocalCertificateFingerprint(PDtlsSession, PCHAR, UINT32);
STATUS dtls_session_verifyRemoteCertificateFingerprint(PDtlsSession, PCHAR);
/**
 * @brief  it is used for the outbound packet of sctp session.
 *
 * @param[in] pDtlsSession the handler of the dtls session
 * @param[in] pData the buffer
 * @param[in] dataLen the length of the buffer
 *
 * @return STATUS_SUCCESS
 */
STATUS dtls_session_send(PDtlsSession, PBYTE, INT32);
STATUS dtls_session_shutdown(PDtlsSession);

STATUS dtls_session_onOutBoundData(PDtlsSession, UINT64, DtlsSessionOutboundPacketFunc);
STATUS dtls_session_onStateChange(PDtlsSession, UINT64, DtlsSessionOnStateChange);

/******** Internal Functions **********/
STATUS dtlsValidateRtcCertificates(PRtcCertificate, PUINT32);
STATUS dtls_session_changeState(PDtlsSession, RTC_DTLS_TRANSPORT_STATE);

#ifdef KVS_USE_OPENSSL
STATUS dtlsCheckOutgoingDataBuffer(PDtlsSession);
STATUS dtls_session_calculateCertificateFingerprint(X509*, PCHAR);
STATUS dtlsGenerateCertificateFingerprints(PDtlsSession, PDtlsSessionCertificateInfo);
STATUS certificate_key_create(INT32, BOOL, X509** ppCert, EVP_PKEY** ppPkey);
STATUS certificate_key_free(X509** ppCert, EVP_PKEY** ppPkey);
STATUS dtlsValidateRtcCertificates(PRtcCertificate, PUINT32);
STATUS createSslCtx(PDtlsSessionCertificateInfo, UINT32, SSL_CTX**);
#elif KVS_USE_MBEDTLS
STATUS dtls_session_calculateCertificateFingerprint(mbedtls_x509_crt*, PCHAR);
STATUS certificate_key_copy(mbedtls_x509_crt*, mbedtls_pk_context*, PDtlsSessionCertificateInfo);
STATUS certificate_key_create(INT32, BOOL, mbedtls_x509_crt*, mbedtls_pk_context*);
STATUS certificate_key_free(mbedtls_x509_crt*, mbedtls_pk_context*);

// following are required callbacks for mbedtls
// NOTE: const is not a pure C qualifier, they're here because there's no way to type cast
//       a callback signature.
/**
 * @brief   the callback of outboud for ssl library.
 *
 * @param[in] customData the custom data
 * @param[in] pBuf the buffer to be sent
 * @param[in] len the length of the buffer
 *
 * @return error code. 0: success.
 */
INT32 dtls_session_sendCallback(PVOID, const unsigned char*, ULONG);
INT32 dtls_session_receiveCallback(PVOID, unsigned char*, ULONG);
VOID dtls_session_setTimerCallback(PVOID, UINT32, UINT32);
INT32 dtls_session_getTimerCallback(PVOID);
INT32 dtls_session_deriveKeyCallback(PVOID, const unsigned char*, const unsigned char*, ULONG, ULONG, ULONG,
                                     const unsigned char[MAX_DTLS_RANDOM_BYTES_LEN], const unsigned char[MAX_DTLS_RANDOM_BYTES_LEN],
                                     mbedtls_tls_prf_types);
#else
#error "A Crypto implementation is required."
#endif

#ifdef __cplusplus
}
#endif
#endif //__KINESIS_VIDEO_WEBRTC_CLIENT_DTLS_DTLS__
