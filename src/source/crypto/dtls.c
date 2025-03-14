#define LOG_CLASS "DTLS"

#include "dtls.h"

STATUS dtls_session_onOutBoundData(PDtlsSession pDtlsSession, UINT64 customData, DtlsSessionOutboundPacketFunc callbackFn)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pDtlsSession != NULL && callbackFn != NULL, STATUS_DTLS_NULL_ARG);

    MUTEX_LOCK(pDtlsSession->nestedDtlsLock);
    pDtlsSession->dtlsSessionCallbacks.dtlsOutboundPacketFn = callbackFn;
    pDtlsSession->dtlsSessionCallbacks.dtlsOutBoundPacketFnCustomData = customData;
    MUTEX_UNLOCK(pDtlsSession->nestedDtlsLock);

CleanUp:
    return retStatus;
}

STATUS dtls_session_onStateChange(PDtlsSession pDtlsSession, UINT64 customData, DtlsSessionOnStateChange callbackFn)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pDtlsSession != NULL && callbackFn != NULL, STATUS_DTLS_NULL_ARG);

    MUTEX_LOCK(pDtlsSession->nestedDtlsLock);
    pDtlsSession->dtlsSessionCallbacks.dtlsStateChangeFn = callbackFn;
    pDtlsSession->dtlsSessionCallbacks.dtlsStateChangeFnCustomData = customData;
    MUTEX_UNLOCK(pDtlsSession->nestedDtlsLock);

CleanUp:
    LEAVES();
    return retStatus;
}

STATUS dtlsValidateRtcCertificates(PRtcCertificate pRtcCertificates, PUINT32 pCount)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 i;

    CHK(pRtcCertificates != NULL && pCount != NULL, retStatus);

    for (i = 0, *pCount = 0; pRtcCertificates[i].pCertificate != NULL && i < MAX_RTCCONFIGURATION_CERTIFICATES; i++) {
        CHK(pRtcCertificates[i].privateKeySize == 0 || pRtcCertificates[i].pPrivateKey != NULL, STATUS_DTLS_INVALID_CERTIFICATE_BITS);
    }

    *pCount = i;

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS dtls_session_changeState(PDtlsSession pDtlsSession, RTC_DTLS_TRANSPORT_STATE newState)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pDtlsSession != NULL, STATUS_DTLS_NULL_ARG);
    CHK(pDtlsSession->state != newState, retStatus);

    if (pDtlsSession->state == RTC_DTLS_TRANSPORT_STATE_CONNECTING && newState == RTC_DTLS_TRANSPORT_STATE_CONNECTED) {
        DLOGD("DTLS init completed. Time taken %" PRIu64 " ms",
              (GETTIME() - pDtlsSession->dtlsSessionStartTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }
    pDtlsSession->state = newState;
    if (pDtlsSession->dtlsSessionCallbacks.dtlsStateChangeFn != NULL) {
        pDtlsSession->dtlsSessionCallbacks.dtlsStateChangeFn(pDtlsSession->dtlsSessionCallbacks.dtlsStateChangeFnCustomData, newState);
    }

CleanUp:

    LEAVES();
    return retStatus;
}
