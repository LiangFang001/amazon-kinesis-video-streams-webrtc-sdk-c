/*******************************************
Ice Utils internal include file
*******************************************/
#ifndef __KINESIS_VIDEO_WEBRTC_ICE_UTILS__
#define __KINESIS_VIDEO_WEBRTC_ICE_UTILS__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "stun.h"
#include "network.h"

// #TBD, need to review this design.
#define DEFAULT_MAX_STORED_TRANSACTION_ID_COUNT 20
#define MAX_STORED_TRANSACTION_ID_COUNT         100

#define ICE_STUN_DEFAULT_PORT 3478

#define ICE_URL_PREFIX_STUN        "stun:"
#define ICE_URL_PREFIX_TURN        "turn:"
#define ICE_URL_PREFIX_TURN_SECURE "turns:"
#define ICE_URL_TRANSPORT_UDP      "transport=udp"
#define ICE_URL_TRANSPORT_TCP      "transport=tcp"

/**
 * Ring buffer storing transactionIds
 */
typedef struct {
    UINT32 maxTransactionIdsCount; //!< the capacity of this transaction id buffer.
    UINT32 nextTransactionIdIndex; //!< the index of the next available buffer.
    UINT32 earliestTransactionIdIndex;
    UINT32 transactionIdCount;
    PBYTE transactionIds; //!< the buffer of transaction.
} TransactionIdStore, *PTransactionIdStore;
/**
 * @brief create the buffer recording the transaction id.
 *          For current design, ice agent, ice candidate pair, and turn will create one transaction id buffer.
 *
 * @param[in] maxIdCount the maximum number of transaction id.
 * @param[out] ppTransactionIdStore the pointer of buffer.
 * @return STATUS status of execution.
 */
STATUS createTransactionIdStore(UINT32, PTransactionIdStore*);
STATUS freeTransactionIdStore(PTransactionIdStore*);
VOID transactionIdStoreInsert(PTransactionIdStore, PBYTE);

BOOL transactionIdStoreHasId(PTransactionIdStore pTransactionIdStore, PBYTE transactionId);
/**
 * @brief reset the buffer of transaction id.
 *
 * @param[in] pTransactionIdStore the transaction object.
 *
 * @return STATUS status of execution.
 */
VOID transactionIdStoreReset(PTransactionIdStore);
/**
 * @brief generate the transaction id.
 *
 * #TBD, this should be take care. According to rfc5389,
 * As such, the transaction ID MUST be uniformlyand randomly chosen from the interval 0 .. 2**96-1,
 * and SHOULD be cryptographically random.
 *
 */
STATUS iceUtilsGenerateTransactionId(PBYTE, UINT32);

// Stun packaging and sending functions
/**
 * @brief
 *
 * @param[in] pStunPacket
 * @param[in] password
 * @param[in] passwordLen
 * @param[in] pBuffer
 * @param[in] pBufferLen
 *
 * @return STATUS status of execution.
 */
STATUS iceUtilsPackageStunPacket(PStunPacket pStunPacket, PBYTE password, UINT32 passwordLen, PBYTE pBuffer, PUINT32 pBufferLen);
/**
 * @brief
 *
 * @param[in] pStunPacket
 * @param[in] password
 * @param[in] passwordLen
 * @param[in] pDest
 * @param[in] pSocketConnection
 * @param[in] pTurnConnection
 * @param[in] useTurn
 *
 * @return STATUS status of execution
 */
STATUS iceUtilsSendStunPacket(PStunPacket pStunPacket, PBYTE password, UINT32 passwordLen, PKvsIpAddress pDest, PSocketConnection pSocketConnection,
                              struct __TurnConnection* pTurnConnection, BOOL useTurn);
/**
 * @brief   send the packet via the socket of the selected ice candidate.
 */
STATUS iceUtilsSendData(PBYTE, UINT32, PKvsIpAddress, PSocketConnection, struct __TurnConnection*, BOOL);

typedef struct {
    BOOL isTurn;
    BOOL isSecure;
    CHAR url[MAX_ICE_CONFIG_URI_LEN + 1];
    KvsIpAddress ipAddress;
    CHAR username[MAX_ICE_CONFIG_USER_NAME_LEN + 1];
    CHAR credential[MAX_ICE_CONFIG_CREDENTIAL_LEN + 1];
    KVS_SOCKET_PROTOCOL transport;
} IceServer, *PIceServer;
/**
 * @brief #TBD, consider to change this api, but it is not a bottleneck.
 *
 * @param[in] pIceServer
 * @param[in] url
 * @param[in] username
 * @param[in] credential
 *
 * @return STATUS status of execution.
 */
STATUS parseIceServer(PIceServer pIceServer, PCHAR url, PCHAR username, PCHAR credential);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_ICE_UTILS__ */
