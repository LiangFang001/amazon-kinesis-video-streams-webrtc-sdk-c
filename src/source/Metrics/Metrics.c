/**
 * Kinesis WebRTC Metrics
 */
#define LOG_CLASS "Metrics"
#include "PeerConnection.h"
#include "Rtp.h"
#include "DataChannel.h"

STATUS metrics_getIceCandidatePairStats(PRtcPeerConnection pRtcPeerConnection, PRtcIceCandidatePairStats pRtcIceCandidatePairStats)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;
    PIceAgent pIceAgent = NULL;

    CHK((pRtcPeerConnection != NULL || pRtcIceCandidatePairStats != NULL), STATUS_METRICS_NULL_ARG);
    pIceAgent = ((PKvsPeerConnection) pRtcPeerConnection)->pIceAgent;

    MUTEX_LOCK(pIceAgent->lock);
    locked = TRUE;
    CHK(pIceAgent->pDataSendingIceCandidatePair != NULL, STATUS_SUCCESS);
    PRtcIceCandidatePairDiagnostics pRtcIceCandidatePairDiagnostics = &pIceAgent->pDataSendingIceCandidatePair->rtcIceCandidatePairDiagnostics;
    STRNCPY(pRtcIceCandidatePairStats->localCandidateId, pRtcIceCandidatePairDiagnostics->localCandidateId, MAX_CANDIDATE_ID_LENGTH);
    STRNCPY(pRtcIceCandidatePairStats->remoteCandidateId, pRtcIceCandidatePairDiagnostics->remoteCandidateId, MAX_CANDIDATE_ID_LENGTH);
    pRtcIceCandidatePairStats->state = pRtcIceCandidatePairDiagnostics->state;
    pRtcIceCandidatePairStats->nominated = pRtcIceCandidatePairDiagnostics->nominated;

    // Note: circuitBreakerTriggerCount This is set to NULL currently
    pRtcIceCandidatePairStats->circuitBreakerTriggerCount = pRtcIceCandidatePairDiagnostics->circuitBreakerTriggerCount;

    pRtcIceCandidatePairStats->packetsDiscardedOnSend = pRtcIceCandidatePairDiagnostics->packetsDiscardedOnSend;
    pRtcIceCandidatePairStats->packetsSent = pRtcIceCandidatePairDiagnostics->packetsSent;
    pRtcIceCandidatePairStats->packetsReceived = pRtcIceCandidatePairDiagnostics->packetsReceived;

    pRtcIceCandidatePairStats->bytesDiscardedOnSend = pRtcIceCandidatePairDiagnostics->bytesDiscardedOnSend;
    pRtcIceCandidatePairStats->bytesSent = pRtcIceCandidatePairDiagnostics->bytesSent;
    pRtcIceCandidatePairStats->bytesReceived = pRtcIceCandidatePairDiagnostics->bytesReceived;

    pRtcIceCandidatePairStats->lastPacketSentTimestamp = pRtcIceCandidatePairDiagnostics->lastPacketSentTimestamp;
    pRtcIceCandidatePairStats->lastPacketReceivedTimestamp = pRtcIceCandidatePairDiagnostics->lastPacketReceivedTimestamp;
    pRtcIceCandidatePairStats->lastRequestTimestamp = pRtcIceCandidatePairDiagnostics->lastRequestTimestamp;
    pRtcIceCandidatePairStats->firstRequestTimestamp = pRtcIceCandidatePairDiagnostics->firstRequestTimestamp;
    pRtcIceCandidatePairStats->lastResponseTimestamp = pRtcIceCandidatePairDiagnostics->lastResponseTimestamp;

    pRtcIceCandidatePairStats->totalRoundTripTime = pRtcIceCandidatePairDiagnostics->totalRoundTripTime;
    pRtcIceCandidatePairStats->currentRoundTripTime = pRtcIceCandidatePairDiagnostics->currentRoundTripTime;

    pRtcIceCandidatePairStats->requestsReceived = pRtcIceCandidatePairDiagnostics->requestsReceived;
    pRtcIceCandidatePairStats->requestsSent = pRtcIceCandidatePairDiagnostics->requestsSent;
    pRtcIceCandidatePairStats->responsesReceived = pRtcIceCandidatePairDiagnostics->responsesReceived;
    pRtcIceCandidatePairStats->responsesSent = pRtcIceCandidatePairDiagnostics->responsesSent;
CleanUp:
    if (locked) {
        MUTEX_UNLOCK(pIceAgent->lock);
    }
    return retStatus;
}

STATUS metrics_getIceCandidateStats(PRtcPeerConnection pRtcPeerConnection, BOOL isRemote, PRtcIceCandidateStats pRtcIceCandidateStats)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;
    PIceAgent pIceAgent = ((PKvsPeerConnection) pRtcPeerConnection)->pIceAgent;
    CHK((pRtcPeerConnection != NULL || pRtcIceCandidateStats != NULL), STATUS_METRICS_NULL_ARG);
    MUTEX_LOCK(pIceAgent->lock);
    locked = TRUE;
    PRtcIceCandidateDiagnostics pRtcIceCandidateDiagnostics = &pIceAgent->rtcSelectedRemoteIceCandidateDiagnostics;
    if (!isRemote) {
        pRtcIceCandidateDiagnostics = &pIceAgent->rtcSelectedLocalIceCandidateDiagnostics;
        STRNCPY(pRtcIceCandidateStats->relayProtocol, pRtcIceCandidateDiagnostics->relayProtocol, MAX_STATS_STRING_LENGTH);
        STRNCPY(pRtcIceCandidateStats->url, pRtcIceCandidateDiagnostics->url, MAX_STATS_STRING_LENGTH);
    }
    STRNCPY(pRtcIceCandidateStats->address, pRtcIceCandidateDiagnostics->address, IP_ADDR_STR_LENGTH);
    STRNCPY(pRtcIceCandidateStats->candidateType, pRtcIceCandidateDiagnostics->candidateType, MAX_STATS_STRING_LENGTH);
    pRtcIceCandidateStats->port = pRtcIceCandidateDiagnostics->port;
    pRtcIceCandidateStats->priority = pRtcIceCandidateDiagnostics->priority;
    STRNCPY(pRtcIceCandidateStats->protocol, pRtcIceCandidateDiagnostics->protocol, MAX_STATS_STRING_LENGTH);
CleanUp:
    if (locked) {
        MUTEX_UNLOCK(pIceAgent->lock);
    }
    return retStatus;
}

STATUS metrics_getIceServerStats(PRtcPeerConnection pRtcPeerConnection, PRtcIceServerStats pRtcIceServerStats)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;
    PIceAgent pIceAgent = ((PKvsPeerConnection) pRtcPeerConnection)->pIceAgent;
    CHK((pRtcPeerConnection != NULL && pRtcIceServerStats != NULL), STATUS_METRICS_NULL_ARG);
    CHK(pRtcIceServerStats->iceServerIndex < pIceAgent->iceServersCount, STATUS_ICE_SERVER_INDEX_INVALID);

    MUTEX_LOCK(pIceAgent->lock);
    locked = TRUE;
    pRtcIceServerStats->port = pIceAgent->rtcIceServerDiagnostics[pRtcIceServerStats->iceServerIndex].port;
    STRNCPY(pRtcIceServerStats->protocol, pIceAgent->rtcIceServerDiagnostics[pRtcIceServerStats->iceServerIndex].protocol, MAX_STATS_STRING_LENGTH);
    STRNCPY(pRtcIceServerStats->url, pIceAgent->rtcIceServerDiagnostics[pRtcIceServerStats->iceServerIndex].url, MAX_STATS_STRING_LENGTH);
    pRtcIceServerStats->totalRequestsSent = pIceAgent->rtcIceServerDiagnostics[pRtcIceServerStats->iceServerIndex].totalRequestsSent;
    pRtcIceServerStats->totalResponsesReceived = pIceAgent->rtcIceServerDiagnostics[pRtcIceServerStats->iceServerIndex].totalResponsesReceived;
    pRtcIceServerStats->totalRoundTripTime = pIceAgent->rtcIceServerDiagnostics[pRtcIceServerStats->iceServerIndex].totalRoundTripTime;
CleanUp:
    if (locked) {
        MUTEX_UNLOCK(pIceAgent->lock);
    }
    return retStatus;
}

STATUS metrics_getTransportStats(PRtcPeerConnection pRtcPeerConnection, PRtcTransportStats pRtcTransportStats)
{
    STATUS retStatus = STATUS_SUCCESS;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pRtcPeerConnection;
    UNUSED_PARAM(pKvsPeerConnection);
    CHK(pRtcPeerConnection != NULL || pRtcTransportStats != NULL, STATUS_METRICS_NULL_ARG);
    CHK(FALSE, STATUS_NOT_IMPLEMENTED);
CleanUp:
    return retStatus;
}

STATUS metrics_getRtpRemoteInboundStats(PRtcPeerConnection pRtcPeerConnection, PRtcRtpTransceiver pTransceiver,
                                        PRtcRemoteInboundRtpStreamStats pRtcRemoteInboundRtpStreamStats)
{
    STATUS retStatus = STATUS_SUCCESS;
#ifdef ENABLE_STREAMING
    PDoubleListNode node = NULL;
    UINT64 hashValue = 0;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pRtcPeerConnection;
    CHK(pRtcPeerConnection != NULL || pRtcRemoteInboundRtpStreamStats != NULL, STATUS_METRICS_NULL_ARG);
    PKvsRtpTransceiver pKvsRtpTransceiver = (PKvsRtpTransceiver) pTransceiver;
    if (pKvsRtpTransceiver == NULL) {
        CHK_STATUS(double_list_getHeadNode(pKvsPeerConnection->pTransceivers, &node));
        CHK_STATUS(double_list_getNodeData(node, &hashValue));
        pKvsRtpTransceiver = (PKvsRtpTransceiver) hashValue;
        CHK(pKvsRtpTransceiver != NULL, STATUS_NOT_FOUND);
    }
    // check if specified transceiver belongs to this connection
    CHK_STATUS(rtp_findTransceiverByssrc(pKvsPeerConnection, pKvsRtpTransceiver->sender.ssrc));
    MUTEX_LOCK(pKvsRtpTransceiver->statsLock);
    *pRtcRemoteInboundRtpStreamStats = pKvsRtpTransceiver->remoteInboundStats;
    MUTEX_UNLOCK(pKvsRtpTransceiver->statsLock);
CleanUp:
#endif
    return retStatus;
}

STATUS metrics_getRtpOutboundStats(PRtcPeerConnection pRtcPeerConnection, PRtcRtpTransceiver pTransceiver,
                                   PRtcOutboundRtpStreamStats pRtcOutboundRtpStreamStats)
{
    STATUS retStatus = STATUS_SUCCESS;
#ifdef ENABLE_STREAMING
    PDoubleListNode node = NULL;
    UINT64 hashValue = 0;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pRtcPeerConnection;
    CHK(pRtcPeerConnection != NULL || pRtcOutboundRtpStreamStats != NULL, STATUS_METRICS_NULL_ARG);
    PKvsRtpTransceiver pKvsRtpTransceiver = (PKvsRtpTransceiver) pTransceiver;

    if (pKvsRtpTransceiver == NULL) {
        CHK_STATUS(double_list_getHeadNode(pKvsPeerConnection->pTransceivers, &node));
        CHK_STATUS(double_list_getNodeData(node, &hashValue));
        pKvsRtpTransceiver = (PKvsRtpTransceiver) hashValue;
        CHK(pKvsRtpTransceiver != NULL, STATUS_NOT_FOUND);
    }

    // check if specified transceiver belongs to this connection
    CHK_STATUS(rtp_findTransceiverByssrc(pKvsPeerConnection, pKvsRtpTransceiver->sender.ssrc));
    MUTEX_LOCK(pKvsRtpTransceiver->statsLock);
    *pRtcOutboundRtpStreamStats = pKvsRtpTransceiver->outboundStats;
    MUTEX_UNLOCK(pKvsRtpTransceiver->statsLock);
CleanUp:
#endif
    return retStatus;
}

STATUS metrics_getRtpInboundStats(PRtcPeerConnection pRtcPeerConnection, PRtcRtpTransceiver pTransceiver,
                                  PRtcInboundRtpStreamStats pRtcInboundRtpStreamStats)
{
    STATUS retStatus = STATUS_SUCCESS;
#ifdef ENABLE_STREAMING
    PDoubleListNode node = NULL;
    UINT64 hashValue;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pRtcPeerConnection;
    CHK(pRtcPeerConnection != NULL || pRtcInboundRtpStreamStats != NULL, STATUS_METRICS_NULL_ARG);
    PKvsRtpTransceiver pKvsRtpTransceiver = (PKvsRtpTransceiver) pTransceiver;

    if (pKvsRtpTransceiver == NULL) {
        CHK_STATUS(double_list_getHeadNode(pKvsPeerConnection->pTransceivers, &node));
        CHK_STATUS(double_list_getNodeData(node, &hashValue));
        pKvsRtpTransceiver = (PKvsRtpTransceiver) hashValue;
        CHK(pKvsRtpTransceiver != NULL, STATUS_NOT_FOUND);
    }

    // check if specified transceiver belongs to this connection
    CHK_STATUS(rtp_findTransceiverByssrc(pKvsPeerConnection, pKvsRtpTransceiver->jitterBufferSsrc));

    MUTEX_LOCK(pKvsRtpTransceiver->statsLock);
    *pRtcInboundRtpStreamStats = pKvsRtpTransceiver->inboundStats;
    MUTEX_UNLOCK(pKvsRtpTransceiver->statsLock);
CleanUp:
#endif
    return retStatus;
}

#ifdef ENABLE_DATA_CHANNEL
STATUS metrics_getDataChannelStats(PRtcPeerConnection pRtcPeerConnection, PRtcDataChannelStats pRtcDataChannelStats)
{
    STATUS retStatus = STATUS_SUCCESS;
    PKvsDataChannel pKvsDataChannel = NULL;
    UINT64 hashValue = 0;
    PKvsPeerConnection pKvsPeerConnection = (PKvsPeerConnection) pRtcPeerConnection;
    CHK(pRtcPeerConnection != NULL && pRtcDataChannelStats != NULL, STATUS_METRICS_NULL_ARG);
    CHK_STATUS(hash_table_get(pKvsPeerConnection->pDataChannels, pRtcDataChannelStats->dataChannelIdentifier, &hashValue));
    pKvsDataChannel = (PKvsDataChannel) hashValue;
    pRtcDataChannelStats->bytesReceived = pKvsDataChannel->rtcDataChannelDiagnostics.bytesReceived;
    pRtcDataChannelStats->bytesSent = pKvsDataChannel->rtcDataChannelDiagnostics.bytesSent;
    STRNCPY(pRtcDataChannelStats->label, pKvsDataChannel->rtcDataChannelDiagnostics.label, MAX_STATS_STRING_LENGTH);
    pRtcDataChannelStats->messagesReceived = pKvsDataChannel->rtcDataChannelDiagnostics.messagesReceived;
    pRtcDataChannelStats->messagesSent = pKvsDataChannel->rtcDataChannelDiagnostics.messagesSent;
    pRtcDataChannelStats->state = pKvsDataChannel->rtcDataChannelDiagnostics.state;
CleanUp:
    return retStatus;
}
#endif

STATUS metrics_get(PRtcPeerConnection pRtcPeerConnection, PRtcRtpTransceiver pRtcRtpTransceiver, PRtcStats pRtcMetrics)
{
    STATUS retStatus = STATUS_SUCCESS;
    CHK(pRtcPeerConnection != NULL && pRtcMetrics != NULL, STATUS_METRICS_NULL_ARG);
    pRtcMetrics->timestamp = GETTIME();
    switch (pRtcMetrics->requestedTypeOfStats) {
        case RTC_STATS_TYPE_CANDIDATE_PAIR:
            CHK_STATUS(metrics_getIceCandidatePairStats(pRtcPeerConnection, &pRtcMetrics->rtcStatsObject.iceCandidatePairStats));
            break;
        case RTC_STATS_TYPE_LOCAL_CANDIDATE:
            CHK_STATUS(metrics_getIceCandidateStats(pRtcPeerConnection, FALSE, &pRtcMetrics->rtcStatsObject.localIceCandidateStats));
            DLOGD("ICE local candidate Stats requested at %" PRIu64, pRtcMetrics->timestamp);
            break;
        case RTC_STATS_TYPE_REMOTE_CANDIDATE:
            CHK_STATUS(metrics_getIceCandidateStats(pRtcPeerConnection, TRUE, &pRtcMetrics->rtcStatsObject.remoteIceCandidateStats));
            DLOGD("ICE remote candidate Stats requested at %" PRIu64, pRtcMetrics->timestamp);
            break;
        case RTC_STATS_TYPE_TRANSPORT:
            CHK_STATUS(metrics_getTransportStats(pRtcPeerConnection, &pRtcMetrics->rtcStatsObject.transportStats));
            break;
        case RTC_STATS_TYPE_REMOTE_INBOUND_RTP:
            CHK_STATUS(
                metrics_getRtpRemoteInboundStats(pRtcPeerConnection, pRtcRtpTransceiver, &pRtcMetrics->rtcStatsObject.remoteInboundRtpStreamStats));
            break;
        case RTC_STATS_TYPE_OUTBOUND_RTP:
            CHK_STATUS(metrics_getRtpOutboundStats(pRtcPeerConnection, pRtcRtpTransceiver, &pRtcMetrics->rtcStatsObject.outboundRtpStreamStats));
            break;
        case RTC_STATS_TYPE_INBOUND_RTP:
            CHK_STATUS(metrics_getRtpInboundStats(pRtcPeerConnection, pRtcRtpTransceiver, &pRtcMetrics->rtcStatsObject.inboundRtpStreamStats));
            break;
        case RTC_STATS_TYPE_ICE_SERVER:
            CHK_STATUS(metrics_getIceServerStats(pRtcPeerConnection, &pRtcMetrics->rtcStatsObject.iceServerStats));
            DLOGD("ICE Server Stats requested at %" PRIu64, pRtcMetrics->timestamp);
            break;
        case RTC_STATS_TYPE_DATA_CHANNEL:
            pRtcMetrics->timestamp = GETTIME();
#ifdef ENABLE_DATA_CHANNEL
            CHK_STATUS(metrics_getDataChannelStats(pRtcPeerConnection, &pRtcMetrics->rtcStatsObject.rtcDataChannelStats));
#endif
            DLOGD("RTC Data Channel Stats requested at %" PRIu64, pRtcMetrics->timestamp);
            break;
        case RTC_STATS_TYPE_CERTIFICATE:
        case RTC_STATS_TYPE_CSRC:
        case RTC_STATS_TYPE_REMOTE_OUTBOUND_RTP:
        case RTC_STATS_TYPE_PEER_CONNECTION:
        case RTC_STATS_TYPE_RECEIVER:
        case RTC_STATS_TYPE_SENDER:
        case RTC_STATS_TYPE_TRACK:
        case RTC_STATS_TYPE_CODEC:
        case RTC_STATS_TYPE_SCTP_TRANSPORT:
        case RTC_STATS_TYPE_TRANSCEIVER:
        case RTC_STATS_TYPE_RTC_ALL:
        default:
            CHK(FALSE, STATUS_NOT_IMPLEMENTED);
    }
CleanUp:
    CHK_LOG_ERR(retStatus);
    return retStatus;
}
