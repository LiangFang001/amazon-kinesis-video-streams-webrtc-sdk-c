/*******************************************
IceAgent internal include file
*******************************************/
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_ICE_AGENT__
#define __KINESIS_VIDEO_WEBRTC_CLIENT_ICE_AGENT__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define KVS_ICE_MAX_CANDIDATE_PAIR_COUNT                       1024
#define KVS_ICE_MAX_REMOTE_CANDIDATE_COUNT                     100
#define KVS_ICE_MAX_LOCAL_CANDIDATE_COUNT                      100
#define KVS_ICE_GATHER_REFLEXIVE_AND_RELAYED_CANDIDATE_TIMEOUT 10 * HUNDREDS_OF_NANOS_IN_A_SECOND
#define KVS_ICE_CONNECTIVITY_CHECK_TIMEOUT                     10 * HUNDREDS_OF_NANOS_IN_A_SECOND
#define KVS_ICE_CANDIDATE_NOMINATION_TIMEOUT                   10 * HUNDREDS_OF_NANOS_IN_A_SECOND
// https://tools.ietf.org/html/rfc5245#section-10
// Agents SHOULD use a Tr value of 15 seconds. Agents MAY use a bigger value but MUST NOT use a value smaller than 15 seconds.
#define KVS_ICE_SEND_KEEP_ALIVE_INTERVAL         15 * HUNDREDS_OF_NANOS_IN_A_SECOND
#define KVS_ICE_TURN_CONNECTION_SHUTDOWN_TIMEOUT 1 * HUNDREDS_OF_NANOS_IN_A_SECOND
#define KVS_ICE_DEFAULT_TIMER_START_DELAY        3 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND
#define KVS_ICE_FSM_TIMER_START_DELAY            3 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND
#define KVS_ICE_GATHERING_TIMER_START_DELAY      3 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND
#define KVS_ICE_SHORT_CHECK_DELAY                (50 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND)

// Ta in https://tools.ietf.org/html/rfc8445
#define KVS_ICE_CONNECTION_CHECK_POLLING_INTERVAL  50 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND
#define KVS_ICE_STATE_READY_TIMER_POLLING_INTERVAL 1 * HUNDREDS_OF_NANOS_IN_A_SECOND
/* Control the calling rate of iceCandidateGatheringTimerTask. Can affect STUN TURN candidate gathering time */
#define KVS_ICE_GATHER_CANDIDATE_TIMER_POLLING_INTERVAL 50 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND

/* ICE should've received at least one keep alive within this period. Since keep alives are send every 15s */
#define KVS_ICE_ENTER_STATE_DISCONNECTION_GRACE_PERIOD 2 * KVS_ICE_SEND_KEEP_ALIVE_INTERVAL
#define KVS_ICE_ENTER_STATE_FAILED_GRACE_PERIOD        15 * HUNDREDS_OF_NANOS_IN_A_SECOND

#define STUN_HEADER_MAGIC_BYTE_OFFSET 4

#define KVS_ICE_MAX_RELAY_CANDIDATE_COUNT                  4
#define KVS_ICE_MAX_NEW_LOCAL_CANDIDATES_TO_REPORT_AT_ONCE 10

// https://tools.ietf.org/html/rfc5245#section-4.1.2.1
#define ICE_PRIORITY_HOST_CANDIDATE_TYPE_PREFERENCE             126
#define ICE_PRIORITY_SERVER_REFLEXIVE_CANDIDATE_TYPE_PREFERENCE 100
#define ICE_PRIORITY_PEER_REFLEXIVE_CANDIDATE_TYPE_PREFERENCE   110
#define ICE_PRIORITY_RELAYED_CANDIDATE_TYPE_PREFERENCE          0
#define ICE_PRIORITY_LOCAL_PREFERENCE                           65535

#define IS_STUN_PACKET(pBuf)       (getInt32(*(PUINT32)((pBuf) + STUN_HEADER_MAGIC_BYTE_OFFSET)) == STUN_HEADER_MAGIC_COOKIE)
#define GET_STUN_PACKET_SIZE(pBuf) ((UINT32) getInt16(*(PINT16)((pBuf) + SIZEOF(UINT16))))

#define IS_CANN_PAIR_SENDING_FROM_RELAYED(p) ((p)->local->iceCandidateType == ICE_CANDIDATE_TYPE_RELAYED)

#define KVS_ICE_DEFAULT_TURN_PROTOCOL KVS_SOCKET_PROTOCOL_TCP

#define ICE_HASH_TABLE_BUCKET_COUNT  50
#define ICE_HASH_TABLE_BUCKET_LENGTH 2

#define ICE_CANDIDATE_ID_LEN 8

#define STATS_NOT_APPLICABLE_STR (PCHAR) "N/A"
typedef enum {
    ICE_CANDIDATE_STATE_NEW,
    ICE_CANDIDATE_STATE_VALID,
    ICE_CANDIDATE_STATE_INVALID,
} ICE_CANDIDATE_STATE;

typedef VOID (*IceInboundPacketFunc)(UINT64, PBYTE, UINT32);
typedef VOID (*IceConnectionStateChangedFunc)(UINT64, UINT64);
typedef VOID (*IceNewLocalCandidateFunc)(UINT64, PCHAR);

typedef struct __IceAgent IceAgent;
typedef struct __IceAgent* PIceAgent;

/**
 * Internal structure tracking ICE server parameters for diagnostics and metrics/stats
 */
typedef struct {
    CHAR url[MAX_STATS_STRING_LENGTH + 1];      //!< STUN/TURN server URL
    CHAR protocol[MAX_STATS_STRING_LENGTH + 1]; //!< Valid values: UDP, TCP
    INT32 port;                                 //!< Port number used by client
    UINT64 totalRequestsSent;                   //!< Total amount of requests that have been sent to the server
    UINT64 totalResponsesReceived;              //!< Total number of responses received from the server
    UINT64 totalRoundTripTime;                  //!< Sum of RTTs of all the requests for which response has been received
} RtcIceServerDiagnostics, *PRtcIceServerDiagnostics;

typedef struct {
    DOMString url; //!< For local candidates this is the URL of the ICE server from which the candidate was obtained
    DOMString transportId[MAX_STATS_STRING_LENGTH + 1]; //!< ID of object that was inspected for RTCTransportStats
    CHAR address[KVS_IP_ADDRESS_STRING_BUFFER_LEN];     //!< IPv4 or IPv6 address of the candidate
    DOMString protocol;                                 //!< Valid values: UDP, TCP
    DOMString relayProtocol;                            //!< Protocol used by endpoint to communicate with TURN server.
                                                        //!< Valid values: UDP, TCP, TLS
    INT32 priority;                                     //!< Computed using the formula in https://tools.ietf.org/html/rfc5245#section-15.1
    INT32 port;                                         //!< Port number of the candidate
    DOMString candidateType;                            //!< Type of local/remote ICE candidate
} RtcIceCandidateDiagnostics, *PRtcIceCandidateDiagnostics;

typedef struct {
    CHAR localCandidateId[MAX_CANDIDATE_ID_LENGTH + 1];  //!< Local candidate that is inspected in RTCIceCandidateStats
    CHAR remoteCandidateId[MAX_CANDIDATE_ID_LENGTH + 1]; //!< Remote candidate that is inspected in RTCIceCandidateStats
    ICE_CANDIDATE_PAIR_STATE state;                      //!< State of checklist for the local-remote candidate pair
    BOOL nominated; //!< Flag is TRUE if the agent is a controlling agent and FALSE otherwise. The agent role is based on the
                    //!< STUN_ATTRIBUTE_TYPE_USE_CANDIDATE flag
    NullableUint32 circuitBreakerTriggerCount; //!< Represents number of times circuit breaker is triggered during media transmission
                                               //!< It is undefined if the user agent does not use this
    UINT32 packetsDiscardedOnSend;             //!< Total number of packets discarded for candidate pair due to socket errors,
    UINT64 packetsSent;                        //!< Total number of packets sent on this candidate pair;
    UINT64 packetsReceived;                    //!< Total number of packets received on this candidate pair
    UINT64 bytesSent;                          //!< Total number of bytes (minus header and padding) sent on this candidate pair
    UINT64 bytesReceived;                      //!< Total number of bytes (minus header and padding) received on this candidate pair
    UINT64 lastPacketSentTimestamp;            //!< Represents the timestamp at which the last packet was sent on this particular
                                               //!< candidate pair, excluding STUN packets.
    UINT64 lastPacketReceivedTimestamp;        //!< Represents the timestamp at which the last packet was sent on this particular
                                               //!< candidate pair, excluding STUN packets.
    UINT64 firstRequestTimestamp;    //!< Represents the timestamp at which the first STUN request was sent on this particular candidate pair.
    UINT64 lastRequestTimestamp;     //!< Represents the timestamp at which the last STUN request was sent on this particular candidate pair.
                                     //!< The average interval between two consecutive connectivity checks sent can be calculated:
                                     //! (lastRequestTimestamp - firstRequestTimestamp) / requestsSent.
    UINT64 lastResponseTimestamp;    //!< Represents the timestamp at which the last STUN response was received on this particular candidate pair.
    DOUBLE totalRoundTripTime;       //!< The sum of all round trip time (seconds) since the beginning of the session, based
                                     //!< on STUN connectivity check responses (responsesReceived), including those that reply to requests
                                     //!< that are sent in order to verify consent. The average round trip time can be computed from
                                     //!< totalRoundTripTime by dividing it by responsesReceived.
    DOUBLE currentRoundTripTime;     //!< Latest round trip time (seconds)
    DOUBLE availableOutgoingBitrate; //!< TODO: Total available bit rate for all the outgoing RTP streams on this candidate pair. Calculated by
                                     //!< underlying congestion control
    DOUBLE availableIncomingBitrate; //!< TODO: Total available bit rate for all the outgoing RTP streams on this candidate pair. Calculated by
                                     //!< underlying congestion control
    UINT64 requestsReceived;         //!< Total number of connectivity check requests received (including retransmission)
    UINT64 requestsSent;             //!< The total number of connectivity check requests sent (without retransmissions).
    UINT64 responsesReceived;        //!< The total number of connectivity check responses received.
    UINT64 responsesSent;            //!< The total number of connectivity check responses sent.
    UINT64 bytesDiscardedOnSend;     //!< Total number of bytes for this candidate pair discarded due to socket errors
} RtcIceCandidatePairDiagnostics, *PRtcIceCandidatePairDiagnostics;

typedef struct {
    UINT64 customData;
    IceInboundPacketFunc inboundPacketFn;
    IceConnectionStateChangedFunc connectionStateChangedFn; //!< the callback for the state of ice agent is changed.
    IceNewLocalCandidateFunc newLocalCandidateFn;           //!< the callback of new local candidate for the peer connection layer.
} IceAgentCallbacks, *PIceAgentCallbacks;

// https://developer.mozilla.org/en-US/docs/Web/API/RTCIceCandidate/candidate
// https://tools.ietf.org/html/rfc5245#section-15.1
// a=candidate:4234997325 1 udp 2043278322 192.168.0.56 44323 typ host
typedef struct {
    ICE_CANDIDATE_TYPE iceCandidateType;
    BOOL isRemote; //!< remote or local.
    KvsIpAddress ipAddress;
    PSocketConnection pSocketConnection; //!< the socket handler of this ice candidate.
    ICE_CANDIDATE_STATE state;
    UINT32 priority; //!< the priority of ice candidate.
    UINT32 iceServerIndex;
    UINT32 foundation;
    /* If candidate is local and relay, then store the
     * pTurnConnection this candidate is associated to */
    struct __TurnConnection* pTurnConnection; //!< the context of the turn connection.

    /* store pointer to iceAgent to pass it to incomingDataHandler in incomingRelayedDataHandler
     * we pass pTurnConnectionTrack as customData to incomingRelayedDataHandler to avoid look up
     * pTurnConnection every time. */
    PIceAgent pIceAgent;

    /* If candidate is local. Indicate whether candidate
     * has been reported through IceNewLocalCandidateFunc */
    BOOL reported;
    CHAR id[ICE_CANDIDATE_ID_LEN + 1];
} IceCandidate, *PIceCandidate;

/**
 * @brief the information of ice candidate pair.
 */
typedef struct {
    PIceCandidate local;
    PIceCandidate remote;
    BOOL nominated;
    BOOL firstStunRequest; //!< is the first stun req or not.
    UINT64 priority;
    ICE_CANDIDATE_PAIR_STATE state;
    PTransactionIdStore pTransactionIdStore;
    UINT64 lastDataSentTime;    //!< the time of the latest sending packet.
    PHashTable requestSentTime; //!< for the stat
    UINT64 roundTripTime;
    UINT64 responsesReceived;
    RtcIceCandidatePairDiagnostics rtcIceCandidatePairDiagnostics;
} IceCandidatePair, *PIceCandidatePair;

struct __IceAgent {
    volatile ATOMIC_BOOL agentStartGathering;      //!< indicate the ice agent is starting gathering or not.
    volatile ATOMIC_BOOL remoteCredentialReceived; //!< true: receive the username/password of remote peer.
    volatile ATOMIC_BOOL candidateGatheringFinished;
    volatile ATOMIC_BOOL shutdown;
    volatile ATOMIC_BOOL restart; //!< indicate the ice agent is restarting or not.
    volatile ATOMIC_BOOL processStun;

    CHAR localUsername[MAX_ICE_CONFIG_USER_NAME_LEN + 1];
    CHAR localPassword[MAX_ICE_CONFIG_CREDENTIAL_LEN + 1];
    CHAR remoteUsername[MAX_ICE_CONFIG_USER_NAME_LEN + 1];
    CHAR remotePassword[MAX_ICE_CONFIG_CREDENTIAL_LEN + 1];
    CHAR combinedUserName[(MAX_ICE_CONFIG_USER_NAME_LEN + 1) << 1]; //!< the combination of remote user name and local user name.

    RtcIceServerDiagnostics rtcIceServerDiagnostics[MAX_ICE_SERVERS_COUNT];
    RtcIceCandidateDiagnostics rtcSelectedLocalIceCandidateDiagnostics;
    RtcIceCandidateDiagnostics rtcSelectedRemoteIceCandidateDiagnostics;

    PHashTable requestTimestampDiagnostics;

    PDoubleList localCandidates; //!< store all the local candidates including host, server-reflexive, and relayed.
    PDoubleList remoteCandidates;
    // store PIceCandidatePair which will be immediately checked for connectivity when the timer is fired.
    // #TBD, receive the stun request, and store the corresponding candidate pair into this queue.
    // will check this connection.
    // this is the connection-check requested by the remote peer.
    // https://tools.ietf.org/html/rfc5245#section-5.8
    // https://tools.ietf.org/html/rfc5245#section-7.2.1.4
    PStackQueue triggeredCheckQueue;
    PDoubleList iceCandidatePairs; //!< the ice candidate pairs.

    PConnectionListener pConnectionListener;

    BOOL isControlling; //!< https://tools.ietf.org/html/rfc5245#section-5.2
                        //!< if we are the controlling ice agent, we need to nominate the ice candidate.
                        //!< basically, if you create the offer, you are the controlling ice agent.
    UINT64 tieBreaker;

    MUTEX lock;

    // timer tasks
    UINT32 iceAgentStateTimerTask;
    UINT32 keepAliveTimerTask;
    UINT32 iceCandidateGatheringTimerTask;

    // Current ice agent state
    UINT64 iceAgentState; //!< used for the setup of ice agent fsm.
    // The state machine
    PStateMachine pStateMachine;
    STATUS iceAgentStatus;
    UINT64 stateEndTime;
    UINT64 candidateGatheringEndTime; //!< the end time of gathering ice candidates.
    PIceCandidatePair pDataSendingIceCandidatePair;

    IceAgentCallbacks iceAgentCallbacks;
    // #TBD, #heap, #memory.
    IceServer iceServers[MAX_ICE_SERVERS_COUNT];
    UINT32 iceServersCount;
    // #TBD, #heap #memory
    KvsIpAddress localNetworkInterfaces[MAX_LOCAL_NETWORK_INTERFACE_COUNT];
    UINT32 localNetworkInterfaceCount;

    UINT32 foundationCounter;

    UINT32 relayCandidateCount; //!< the number of relay candidates.

    TIMER_QUEUE_HANDLE timerQueueHandle;
    UINT64 lastDataReceivedTime;
    BOOL detectedDisconnection;
    UINT64 disconnectionGracePeriodEndTime; //!< if ice agent enters the disconnected state, it will have grace period to recover.

    ICE_TRANSPORT_POLICY iceTransportPolicy;
    KvsRtcConfiguration kvsRtcConfiguration;

    // Pre-allocated stun packets
    PStunPacket pBindingIndication;
    PStunPacket pBindingRequest; //!< the packet of binding request.

    // store transaction ids for stun binding request.
    PTransactionIdStore pStunBindingRequestTransactionIdStore;
};

//////////////////////////////////////////////
// internal functions
//////////////////////////////////////////////

/**
 * @brief allocate the IceAgent struct and store username and password
 *
 * @param[in] PCHAR username
 * @param[in] PCHAR password
 * @param[in] PIceAgentCallbacks callback for inbound packets
 * @param[in] PRtcConfiguration RtcConfig
 * @param[in][out] PIceAgent* the created IceAgent struct
 *
 * @return STATUS status of execution
 */
STATUS createIceAgent(PCHAR, PCHAR, PIceAgentCallbacks, PRtcConfiguration, TIMER_QUEUE_HANDLE, PConnectionListener, PIceAgent*);

/**
 * @brief deallocate the PIceAgent object and all its resources.
 * @param[in, out] ppIceAgent
 * @return STATUS status of execution
 */
STATUS freeIceAgent(PIceAgent* ppIceAgent);

/**
 * @brief if PIceCandidate doesnt exist already in remoteCandidates, create a copy and add to remoteCandidates
 *
 * @param[in] PIceAgent IceAgent object
 * @param[in] PIceCandidate new remote candidate to add
 *
 * @return STATUS status of execution
 */
STATUS iceAgentAddRemoteCandidate(PIceAgent, PCHAR);

/**
 * @brief Initiates stun communication with remote candidates.
 *
 * @param[in] PIceAgent IceAgent object
 * @param[in] PCHAR remote username
 * @param[in] PCHAR remote password
 * @param[in] BOOL is controlling agent
 *
 * @return STATUS status of execution
 */
STATUS iceAgentStartAgent(PIceAgent, PCHAR, PCHAR, BOOL);

/**
 * @brief Initiates candidate gathering.
 * #TBD, original sdk starts gathering ice candidate after setting local description, or receiving remote description.
 *
 * @param[in] pIceAgent IceAgent object
 *
 * @return STATUS status of execution
 */
STATUS iceAgentStartGathering(PIceAgent pIceAgent);

/**
 * @brief Serialize a candidate for Trickle ICE or exchange via SDP
 *
 * @param[in] PIceAgent IceAgent object
 * @param[in] PCHAR OUT Destination buffer
 * @param[in] UINT32 OUT Size of destination buffer
 *
 * @return STATUS status of execution
 */
STATUS iceCandidateSerialize(PIceCandidate, PCHAR, PUINT32);

/**
 * @brief Send data through selected connection. PIceAgent has to be in ICE_AGENT_CONNECTION_STATE_CONNECTED state.
 *
 * @param[in] PIceAgent IceAgent object
 * @param[in] PBYTE buffer storing the data to be sent
 * @param[in] UINT32 length of data
 *
 * @return STATUS status of execution
 */
STATUS iceAgentSendPacket(PIceAgent, PBYTE, UINT32);

/**
 * @brief Starting from given index, fillout PSdpMediaDescription->sdpAttributes with serialize local candidate strings.
 *
 * @param[in] PIceAgent IceAgent object
 * @param[in] PSdpMediaDescription PSdpMediaDescription object whose sdpAttributes will be filled with local candidate strings
 * @param[in] UINT32 buffer length of pSdpMediaDescription->sdpAttributes[index].attributeValue
 * @param[in] PUINT32 starting index in sdpAttributes
 *
 * @return STATUS status of execution
 */
STATUS iceAgentPopulateSdpMediaDescriptionCandidates(PIceAgent, PSdpMediaDescription, UINT32, PUINT32);

/**
 * @brief Start shutdown sequence for IceAgent. Once the function returns Ice will not deliver anymore data and
 * IceAgent is ready to be freed. User should stop calling iceAgentSendPacket after iceAgentShutdown returns.
 * iceAgentShutdown is idempotent.
 *
 * @param[in] PIceAgent IceAgent object
 *
 * @return STATUS status of execution
 */
STATUS iceAgentShutdown(PIceAgent);

/**
 * @brief Restart IceAgent. IceAgent is reset back to the same state when it was first created. Once iceAgentRestart() return,
 * call iceAgentStartGathering() to start gathering and call iceAgentStartAgent() to give iceAgent the new remote uFrag
 * and uPwd. While Ice is restarting, iceAgentSendPacket can still be called to send data if a connected pair exists.
 *
 * @param[in] PIceAgent IceAgent object
 * @param[in] PCHAR new local uFrag
 * @param[in] PCHAR new local uPwd
 *
 * @return STATUS status of execution
 */
STATUS iceAgentRestart(PIceAgent, PCHAR, PCHAR);
/**
 * @brief   report the new local candidate to upper layer.
 *
 * @param[in] pIceAgent the context of ice agent.
 * @param[in] pIceCandidate the new local candidate.
 *
 * @return STATUS status of execution
 */
STATUS iceAgentReportNewLocalCandidate(PIceAgent, PIceCandidate);
STATUS iceAgentValidateKvsRtcConfig(PKvsRtcConfiguration);

// Incoming data handling functions

/**
 * @brief handle the incoming packets from the sockete of ice candidate.
 *
 * @param[in] pIceAgent
 * @param[in] pIceAgent
 * @param[in] pIceAgent
 * @param[in] pIceAgent
 * @param[in] pIceAgent
 * @param[in] pIceAgent
 *
 * @return STATUS status of execution.
 */
STATUS incomingDataHandler(UINT64 customData, PSocketConnection pSocketConnection, PBYTE pBuffer, UINT32 bufferLen, PKvsIpAddress pSrc,
                           PKvsIpAddress pDest);
/**
 * @brief the callback of the socket connection of relay candidates.
 */
STATUS incomingRelayedDataHandler(UINT64 customData, PSocketConnection pSocketConnection, PBYTE pBuffer, UINT32 bufferLen, PKvsIpAddress pSrc,
                                  PKvsIpAddress pDest);

/**
 * @brief handle the incoming stun packets.
 *
 * @param[in] pIceAgent
 * @param[in] pBuffer
 * @param[in] bufferLen
 * @param[in] pSocketConnection
 * @param[in] pSrcAddr
 * @param[in] pDestAddr
 *
 * @return STATUS status of execution.
 */
STATUS handleStunPacket(PIceAgent pIceAgent, PBYTE pBuffer, UINT32 bufferLen, PSocketConnection pSocketConnection, PKvsIpAddress pSrcAddr,
                        PKvsIpAddress pDestAddr);

// IceCandidate functions
/**
 * @brief   update the ip address of ice candidate and set the state of the ice candidate as valid.
 *
 * @param[in] pIceCandidate
 * @param[in] pIpAddr
 *
 * @return STATUS status of execution.
 */
STATUS updateCandidateAddress(PIceCandidate, PKvsIpAddress);
STATUS findCandidateWithIp(PKvsIpAddress, PDoubleList, PIceCandidate*);
STATUS findCandidateWithSocketConnection(PSocketConnection, PDoubleList, PIceCandidate*);

// IceCandidatePair functions
/**
 * @brief Need to acquire pIceAgent->lock first
 *          createt the ice candidate pair.
 */
STATUS createIceCandidatePairs(PIceAgent pIceAgent, PIceCandidate pIceCandidate, BOOL isRemoteCandidate);
STATUS freeIceCandidatePair(PIceCandidatePair*);
/**
 * @brief insert the ice candidate pair according to the priority of ice candidate pair.
 */
STATUS insertIceCandidatePair(PDoubleList, PIceCandidatePair);
STATUS findIceCandidatePairWithLocalSocketConnectionAndRemoteAddr(PIceAgent, PSocketConnection, PKvsIpAddress, BOOL, PIceCandidatePair*);
STATUS pruneUnconnectedIceCandidatePair(PIceAgent);
/**
 * @brief there are two state in the state machine, one is check-connection and another is nomination, sending the stun packet to
 *          confirm the connection between remote ice candidate and local ice candidate.
 *
 * @param[in] pStunBindingRequest the context of the stun packet.
 * @param[in] pIceAgent the context of the ice agent.
 * @param[in] pIceCandidatePair the context of the candidate pair.
 *
 * @return STATUS status of execution.
 */
STATUS iceCandidatePairCheckConnection(PStunPacket pStunBindingRequest, PIceAgent pIceAgent, PIceCandidatePair pIceCandidatePair);

/**
 * @brief send the request of server reflex.
 */
STATUS iceAgentSendSrflxCandidateRequest(PIceAgent);
/**
 * @brief fsm::check-connection and fsm::nominating will use this api to check the connection of  the ice candidate pair.
 *
 * @param[in] pIceAgent the context of the ice agent.
 *
 * @return STATUS status of execution.
 */
STATUS iceAgentCheckCandidatePairConnection(PIceAgent pIceAgent);
/**
 * @brief controlling ice agent sends the use-candidate stun packet out.
 */
STATUS iceAgentSendCandidateNomination(PIceAgent);
STATUS iceAgentSendStunPacket(PStunPacket pStunPacket, PBYTE password, UINT32 passwordLen, PIceAgent pIceAgent, PIceCandidate pLocalCandidate,
                              PKvsIpAddress pDestAddr);

STATUS iceAgentSetupFsmCheckConnection(PIceAgent pIceAgent);
/**
 * @brief   the handler of state machine when it is at connnected state..
 *          select the pDataSendingIceCandidatePair according to sequence of ice candidate pair,
 *          and start the timer of keeping alive.
 *
 * @param[in] PIceAgent IceAgent object
 *
 * @return STATUS status of execution
 */
STATUS iceAgentSetupFsmConnected(PIceAgent);
/**
 * @brief nominating one ice candidate if we are a controlling ice agent.
 *
 * @param[in] PIceAgent IceAgent object
 *
 * @return STATUS status of execution
 */
STATUS iceAgentSetupFsmNominating(PIceAgent);
STATUS iceAgentSetupFsmReady(PIceAgent);

// timer callbacks. timer callbacks are interlocked by time queue lock.
/**
 * @brief timer queue callbacks are interlocked by time queue lock.
 *
 * #static
 *
 * @param timerId - timer queue task id
 * @param currentTime
 * @param customData - custom data passed to timer queue when task was added
 *
 * @return STATUS status of execution
 */
STATUS iceAgentFsmTimerCallback(UINT32, UINT64, UINT64);
/**
 * @brief This is one callback which is used by timer. After ice agent enters the connected state, we need to send keep alive packet.
 * And we set the interval of keeping alive as 15 seconds.
 *
 * When STUN is being used for keepalives, a STUN Binding Indication is used [RFC5389].
 * The Indication MUST NOT utilize any authentication mechanism.
 * It SHOULD contain the FINGERPRINT attribute to aid in demultiplexing, but it SHOULD NOT contain any other attributes.
 *
 * #TBD, need to change this behavior. #fsm.
 *
 * @return STATUS status of execution
 */
STATUS iceAgentKeepAliveTimerCallback(UINT32, UINT64, UINT64);
STATUS iceAgentGatheringTimerCallback(UINT32 timerId, UINT64 currentTime, UINT64 customData);

// Default time callback for the state machine
UINT64 iceAgentGetCurrentTime(UINT64);

/**
 * @brief   nominating one first connected candidate pair and move other candidate pair to frozen state.
 *          only controlling ice agent can nominate the ice candidate.
 */
STATUS iceAgentNominateCandidatePair(PIceAgent);
/**
 * @brief   invalidate the ice candidate pair if the local ice candidate is invalid.
 *
 * @param[in] the context of the ice agent.
 *
 * @return STATUS status of execution.
 */
STATUS iceAgentInvalidateCandidatePair(PIceAgent pIceAgent);

/**
 * @brief receive one stun packet can not match the ip and port of local/remote, so it may be one reflexive candidate.
 */
STATUS iceAgentCheckPeerReflexiveCandidate(PIceAgent, PKvsIpAddress, UINT32, BOOL, PSocketConnection);
STATUS iceAgentFatalError(PIceAgent, STATUS);
VOID iceAgentLogNewCandidate(PIceCandidate);

UINT32 computeCandidatePriority(PIceCandidate);
UINT64 computeCandidatePairPriority(PIceCandidatePair, BOOL);
/**
 * @brief   get the corrsponding string of ice candidate type.
 *
 * @param[in] candidateType
 *
 * @return the string of the ice candidate type.
 */
PCHAR iceAgentGetCandidateTypeStr(ICE_CANDIDATE_TYPE);
STATUS updateSelectedLocalRemoteCandidateStats(PIceAgent);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_CLIENT_ICE_AGENT__ */
