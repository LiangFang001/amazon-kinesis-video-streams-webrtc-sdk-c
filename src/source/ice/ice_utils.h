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
#ifndef __KINESIS_VIDEO_WEBRTC_ICE_UTILS__
#define __KINESIS_VIDEO_WEBRTC_ICE_UTILS__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include "stun.h"
#include "network.h"
#include "socket_connection.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
// #TBD, need to review this design.
#define DEFAULT_MAX_STORED_TRANSACTION_ID_COUNT 20
#define MAX_STORED_TRANSACTION_ID_COUNT         100

#define ICE_STUN_DEFAULT_PORT 3478

// Max uFrag and uPwd length as documented in https://tools.ietf.org/html/rfc5245#section-15.4
#define ICE_MAX_UFRAG_LEN 256
#define ICE_MAX_UPWD_LEN  256

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

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
/**
 * @brief create the buffer recording the transaction id.
 *          For current design, ice agent, ice candidate pair, and turn will create one transaction id buffer.
 *
 * @param[in] maxIdCount the maximum number of transaction id.
 * @param[out] ppTransactionIdStore the pointer of buffer.
 * @return STATUS status of execution.
 */
STATUS transaction_id_store_create(UINT32, PTransactionIdStore*);
STATUS transaction_id_store_free(PTransactionIdStore*);
/**
 * @brief
 *
 * @param[in] pTransactionIdStore
 * @param[in] transactionId
 *
 * @return VOID
 */
VOID transaction_id_store_insert(PTransactionIdStore pTransactionIdStore, PBYTE transactionId);
/**
 * @brief
 *
 * @param[in] pTransactionIdStore
 * @param[in] transactionId
 *
 * @return VOID
 */
BOOL transaction_id_store_isExisted(PTransactionIdStore pTransactionIdStore, PBYTE transactionId);
/**
 * @brief reset the buffer of transaction id.
 *
 * @param[in] pTransactionIdStore the transaction object.
 *
 * @return STATUS status of execution.
 */
VOID transaction_id_store_reset(PTransactionIdStore);
/**
 * @brief generate the transaction id.
 *
 * #TBD, this should be take care. According to rfc5389,
 * As such, the transaction ID MUST be uniformlyand randomly chosen from the interval 0 .. 2**96-1,
 * and SHOULD be cryptographically random.
 *
 * @param[in, out] pBuffer the transaction object.
 * @param[in] bufferLen the transaction object.
 *
 * @return STATUS status of execution.
 */
STATUS ice_utils_generateTransactionId(PBYTE pBuffer, UINT32 bufferLen);
/**
 * @brief Stun packaging and sending functions
 *
 * @param[in] pStunPacket
 * @param[in] password
 * @param[in] passwordLen
 * @param[in] pBuffer
 * @param[in] pBufferLen
 *
 * @return STATUS status of execution.
 */
STATUS ice_utils_packStunPacket(PStunPacket pStunPacket, PBYTE password, UINT32 passwordLen, PBYTE pBuffer, PUINT32 pBufferLen);
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
STATUS ice_utils_sendStunPacket(PStunPacket pStunPacket, PBYTE password, UINT32 passwordLen, PKvsIpAddress pDest, PSocketConnection pSocketConnection,
                                struct __TurnConnection* pTurnConnection, BOOL useTurn);
/**
 * @brief   send the packet via the socket of the selected ice candidate.
 */
STATUS ice_utils_send(PBYTE, UINT32, PKvsIpAddress, PSocketConnection, struct __TurnConnection*, BOOL);

typedef struct {
    BOOL isTurn;   //!< is turn server or not.
    BOOL isSecure; //!< is secure connection or not.
    CHAR url[MAX_ICE_CONFIG_URI_LEN + 1];
    KvsIpAddress ipAddress;
    CHAR username[MAX_ICE_CONFIG_USER_NAME_LEN + 1];
    CHAR credential[MAX_ICE_CONFIG_CREDENTIAL_LEN + 1];
    KVS_SOCKET_PROTOCOL transport; //!< tcp or udp.
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
STATUS ice_utils_parseIceServer(PIceServer pIceServer, PCHAR url, PCHAR username, PCHAR credential);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_ICE_UTILS__ */
