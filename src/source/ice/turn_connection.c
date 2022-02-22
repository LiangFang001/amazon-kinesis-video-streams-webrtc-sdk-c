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
#define LOG_CLASS "TurnConnection"
#include "../Include_i.h"
#include "endianness.h"
#include "ice_agent.h"
#include "turn_connection.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
static STATUS turn_connection_updateNonce(PTurnConnection pTurnConnection)
{
    STATUS retStatus = STATUS_SUCCESS;

    // assume holding pTurnConnection->lock

    // update nonce for pre-created packets
    if (pTurnConnection->pTurnPacket != NULL) {
        CHK_STATUS(stun_attribute_updateNonce(pTurnConnection->pTurnPacket, pTurnConnection->turnNonce, pTurnConnection->nonceLen));
    }

    if (pTurnConnection->pTurnAllocationRefreshPacket != NULL) {
        CHK_STATUS(stun_attribute_updateNonce(pTurnConnection->pTurnAllocationRefreshPacket, pTurnConnection->turnNonce, pTurnConnection->nonceLen));
    }

    if (pTurnConnection->pTurnChannelBindPacket != NULL) {
        CHK_STATUS(stun_attribute_updateNonce(pTurnConnection->pTurnChannelBindPacket, pTurnConnection->turnNonce, pTurnConnection->nonceLen));
    }

    if (pTurnConnection->pTurnCreatePermissionPacket != NULL) {
        CHK_STATUS(stun_attribute_updateNonce(pTurnConnection->pTurnCreatePermissionPacket, pTurnConnection->turnNonce, pTurnConnection->nonceLen));
    }

CleanUp:

    CHK_LOG_ERR(retStatus);
    return retStatus;
}
/**
 * @brief handle the stun inbound packet from the turn connection.
 *
 * @param[in] pTurnConnection the context of the turn connection.
 * @param[in] pBuffer the buffer of the packet.
 * @param[in] bufferLen the length of the buffer.
 *
 * @return STATUS status of execution.
 */
static STATUS turn_connection_handleInboundStun(PTurnConnection pTurnConnection, PBYTE pBuffer, UINT32 bufferLen)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT16 stunPacketType = 0;
    PStunAttributeHeader pStunAttr = NULL;
    PStunAttributeAddress pStunAttributeAddress = NULL;
    PStunAttributeLifetime pStunAttributeLifetime = NULL;
    PStunPacket pStunPacket = NULL;
    CHAR ipAddrStr[KVS_IP_ADDRESS_STRING_BUFFER_LEN];
    BOOL locked = FALSE;
    ATOMIC_BOOL hasAllocation = FALSE;
    PTurnPeer pTurnPeer = NULL;
    UINT64 currentTime;
    UINT32 i;

    CHK(pTurnConnection != NULL, STATUS_TURN_NULL_ARG);
    CHK(pBuffer != NULL && bufferLen > 0, STATUS_TURN_INVALID_ARG);
    CHK(IS_STUN_PACKET(pBuffer) && !STUN_PACKET_IS_TYPE_ERROR(pBuffer), retStatus);

    MUTEX_LOCK(pTurnConnection->lock);
    locked = TRUE;

    currentTime = GETTIME();
    // only handling STUN response
    stunPacketType = (UINT16) getInt16(*((PUINT16) pBuffer));

    switch (stunPacketType) {
        case STUN_PACKET_TYPE_ALLOCATE_SUCCESS_RESPONSE:
            /* If shutdown has been initiated, ignore the allocation response */
            CHK(!ATOMIC_LOAD(&pTurnConnection->stopTurnConnection), retStatus);
            // de-serialize the stun packet.
            CHK_STATUS(stun_deserializePacket(pBuffer, bufferLen, pTurnConnection->longTermKey, KVS_MD5_DIGEST_LENGTH, &pStunPacket));
            // get the xor relay address.
            CHK_STATUS(stun_attribute_getByType(pStunPacket, STUN_ATTRIBUTE_TYPE_XOR_RELAYED_ADDRESS, &pStunAttr));
            CHK_WARN(pStunAttr != NULL, retStatus, "No relay address attribute found in TURN allocate response. Dropping Packet");
            // get the lifetime.
            CHK_STATUS(stun_attribute_getByType(pStunPacket, STUN_ATTRIBUTE_TYPE_LIFETIME, (PStunAttributeHeader*) &pStunAttributeLifetime));
            CHK_WARN(pStunAttributeLifetime != NULL, retStatus, "Missing lifetime in Allocation response. Dropping Packet");

            // convert lifetime to 100ns and store it
            pTurnConnection->allocationExpirationTime = (pStunAttributeLifetime->lifetime * HUNDREDS_OF_NANOS_IN_A_SECOND) + currentTime;
            DLOGD("TURN Allocation succeeded. Life time: %u seconds. Allocation expiration epoch %" PRIu64, pStunAttributeLifetime->lifetime,
                  pTurnConnection->allocationExpirationTime / DEFAULT_TIME_UNIT_IN_NANOS);

            pStunAttributeAddress = (PStunAttributeAddress) pStunAttr;
            pTurnConnection->relayAddress = pStunAttributeAddress->address;
            ATOMIC_STORE_BOOL(&pTurnConnection->hasAllocation, TRUE);
            // the callback for notifying the user of the information of xor relay address.
            if (!pTurnConnection->relayAddressReported && pTurnConnection->turnConnectionCallbacks.relayAddressAvailableFn != NULL) {
                pTurnConnection->relayAddressReported = TRUE;

                // release lock early and report relay candidate
                MUTEX_UNLOCK(pTurnConnection->lock);
                locked = FALSE;

                pTurnConnection->turnConnectionCallbacks.relayAddressAvailableFn(pTurnConnection->turnConnectionCallbacks.customData,
                                                                                 &pTurnConnection->relayAddress, pTurnConnection->pControlChannel);
            }
            break;

        case STUN_PACKET_TYPE_REFRESH_SUCCESS_RESPONSE:
            CHK_STATUS(stun_deserializePacket(pBuffer, bufferLen, pTurnConnection->longTermKey, KVS_MD5_DIGEST_LENGTH, &pStunPacket));
            CHK_STATUS(stun_attribute_getByType(pStunPacket, STUN_ATTRIBUTE_TYPE_LIFETIME, (PStunAttributeHeader*) &pStunAttributeLifetime));
            CHK_WARN(pStunAttributeLifetime != NULL, retStatus, "No lifetime attribute found in TURN refresh response. Dropping Packet");

            if (pStunAttributeLifetime->lifetime == 0) {
                hasAllocation = ATOMIC_EXCHANGE_BOOL(&pTurnConnection->hasAllocation, FALSE);
                CHK(hasAllocation, retStatus);
                DLOGD("TURN Allocation freed.");
                CVAR_SIGNAL(pTurnConnection->freeAllocationCvar);
            } else {
                // convert lifetime to 100ns and store it
                pTurnConnection->allocationExpirationTime = (pStunAttributeLifetime->lifetime * HUNDREDS_OF_NANOS_IN_A_SECOND) + currentTime;
                DLOGD("Refreshed TURN allocation lifetime is %u seconds. Allocation expiration epoch %" PRIu64, pStunAttributeLifetime->lifetime,
                      pTurnConnection->allocationExpirationTime / DEFAULT_TIME_UNIT_IN_NANOS);
            }
            break;

        case STUN_PACKET_TYPE_CREATE_PERMISSION_SUCCESS_RESPONSE:
            for (i = 0; i < pTurnConnection->turnPeerCount; ++i) {
                pTurnPeer = &pTurnConnection->turnPeerList[i];
                if (transaction_id_store_isExisted(pTurnPeer->pTransactionIdStore, pBuffer + STUN_PACKET_TRANSACTION_ID_OFFSET)) {
                    if (pTurnPeer->connectionState == TURN_PEER_CONN_STATE_CREATE_PERMISSION) {
                        pTurnPeer->connectionState = TURN_PEER_CONN_STATE_BIND_CHANNEL;
                        CHK_STATUS(net_getIpAddrStr(&pTurnPeer->address, ipAddrStr, ARRAY_SIZE(ipAddrStr)));
                        DLOGD("create permission succeeded for peer %s", ipAddrStr);
                    }

                    pTurnPeer->permissionExpirationTime = TURN_PERMISSION_LIFETIME + currentTime;
                }
            }
            break;

        case STUN_PACKET_TYPE_CHANNEL_BIND_SUCCESS_RESPONSE:
            for (i = 0; i < pTurnConnection->turnPeerCount; ++i) {
                pTurnPeer = &pTurnConnection->turnPeerList[i];
                if (pTurnPeer->connectionState == TURN_PEER_CONN_STATE_BIND_CHANNEL &&
                    transaction_id_store_isExisted(pTurnPeer->pTransactionIdStore, pBuffer + STUN_PACKET_TRANSACTION_ID_OFFSET)) {
                    // pTurnPeer->ready means this peer is ready to receive data. pTurnPeer->connectionState could
                    // change after reaching TURN_PEER_CONN_STATE_READY due to refreshing permission and channel.
                    if (!pTurnPeer->ready) {
                        pTurnPeer->ready = TRUE;
                    }
                    pTurnPeer->connectionState = TURN_PEER_CONN_STATE_READY;

                    CHK_STATUS(net_getIpAddrStr(&pTurnPeer->address, ipAddrStr, ARRAY_SIZE(ipAddrStr)));
                    DLOGD("Channel bind succeeded with peer %s, port: %u, channel number %u", ipAddrStr, (UINT16) getInt16(pTurnPeer->address.port),
                          pTurnPeer->channelNumber);

                    break;
                }
            }
            break;

        case STUN_PACKET_TYPE_DATA_INDICATION:
            DLOGD("Received data indication");
            // no-ops for now, turn server has to use data channel if it is established. client is free to use either.
            break;

        default:
            DLOGD("Drop unsupported TURN message with type 0x%02x", stunPacketType);
            break;
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (locked) {
        MUTEX_UNLOCK(pTurnConnection->lock);
    }

    if (pStunPacket != NULL) {
        stun_freePacket(&pStunPacket);
    }

    LEAVES();
    return retStatus;
}
/**
 * @brief hanlde the stun error response.
 *
 * @param[in] pTurnConnection the context of the turn connection.
 * @param[in] pBuffer the buffer of the stun packet.
 * @param[in] bufferLen the length of the stun packet.
 *
 * @return STATUS status of execution.
 */
static STATUS turn_connection_handleInboundStunError(PTurnConnection pTurnConnection, PBYTE pBuffer, UINT32 bufferLen)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT16 stunPacketType = 0;
    PStunAttributeHeader pStunAttr = NULL;
    PStunAttributeErrorCode pStunAttributeErrorCode = NULL;
    PStunAttributeNonce pStunAttributeNonce = NULL;
    PStunAttributeRealm pStunAttributeRealm = NULL;
    PStunPacket pStunPacket = NULL;
    BOOL locked = FALSE, iterate = TRUE;
    PTurnPeer pTurnPeer = NULL;
    UINT32 i;

    CHK(pTurnConnection != NULL, STATUS_TURN_NULL_ARG);
    CHK(pBuffer != NULL && bufferLen > 0, STATUS_TURN_INVALID_ARG);
    CHK(STUN_PACKET_IS_TYPE_ERROR(pBuffer), retStatus);
    DLOGD("stun error packet type:%d", STUN_PACKET_GET_TYPE(pBuffer));

    MUTEX_LOCK(pTurnConnection->lock);
    locked = TRUE;

    /* we could get errors like expired nonce after sending the deallocation packet. The allocate would have been
     * deallocated even if the response is error response, and if we try to deallocate again we would get invalid
     * allocation error. Therefore if we get an error after we've sent the deallocation packet, consider the
     * deallocation process finished. */
    if (pTurnConnection->deallocatePacketSent) {
        ATOMIC_STORE_BOOL(&pTurnConnection->hasAllocation, FALSE);
        CHK(FALSE, retStatus);
    }

    if (pTurnConnection->credentialObtained) {
        retStatus = stun_deserializePacket(pBuffer, bufferLen, pTurnConnection->longTermKey, KVS_MD5_DIGEST_LENGTH, &pStunPacket);
    }
    /* if deserializing with password didnt work, try deserialize without password again */
    if (!pTurnConnection->credentialObtained || STATUS_FAILED(retStatus)) {
        CHK_STATUS(stun_deserializePacket(pBuffer, bufferLen, NULL, 0, &pStunPacket));
        retStatus = STATUS_SUCCESS;
    }

    CHK_STATUS(stun_attribute_getByType(pStunPacket, STUN_ATTRIBUTE_TYPE_ERROR_CODE, &pStunAttr));
    CHK_WARN(pStunAttr != NULL, retStatus, "No error code attribute found in Stun Error response. Dropping Packet");
    pStunAttributeErrorCode = (PStunAttributeErrorCode) pStunAttr;

    switch (pStunAttributeErrorCode->errorCode) {
        case STUN_ERROR_UNAUTHORIZED:
            CHK_STATUS(stun_attribute_getByType(pStunPacket, STUN_ATTRIBUTE_TYPE_NONCE, &pStunAttr));
            CHK_WARN(pStunAttr != NULL, retStatus, "No Nonce attribute found in Allocate Error response. Dropping Packet");
            pStunAttributeNonce = (PStunAttributeNonce) pStunAttr;
            CHK_WARN(pStunAttributeNonce->attribute.length <= STUN_MAX_NONCE_LEN, retStatus,
                     "Invalid Nonce found in Allocate Error response. Dropping Packet");
            pTurnConnection->nonceLen = pStunAttributeNonce->attribute.length;
            // copy the nonce.
            MEMCPY(pTurnConnection->turnNonce, pStunAttributeNonce->nonce, pTurnConnection->nonceLen);
            // get the realm.
            CHK_STATUS(stun_attribute_getByType(pStunPacket, STUN_ATTRIBUTE_TYPE_REALM, &pStunAttr));
            CHK_WARN(pStunAttr != NULL, retStatus, "No Realm attribute found in Allocate Error response. Dropping Packet");
            pStunAttributeRealm = (PStunAttributeRealm) pStunAttr;
            CHK_WARN(pStunAttributeRealm->attribute.length <= STUN_MAX_REALM_LEN, retStatus,
                     "Invalid Realm found in Allocate Error response. Dropping Packet");
            // pStunAttributeRealm->attribute.length does not include null terminator and pStunAttributeRealm->realm is not null terminated
            // copy the realm
            STRNCPY(pTurnConnection->turnRealm, pStunAttributeRealm->realm, pStunAttributeRealm->attribute.length);
            pTurnConnection->turnRealm[pStunAttributeRealm->attribute.length] = '\0';

            pTurnConnection->credentialObtained = TRUE;
            // update the information inside the pre-allocated packets.
            CHK_STATUS(turn_connection_updateNonce(pTurnConnection));
            break;

        case STUN_ERROR_STALE_NONCE:
            DLOGD("Updating stale nonce");
            CHK_STATUS(stun_attribute_getByType(pStunPacket, STUN_ATTRIBUTE_TYPE_NONCE, &pStunAttr));
            CHK_WARN(pStunAttr != NULL, retStatus, "No Nonce attribute found in Refresh Error response. Dropping Packet");
            pStunAttributeNonce = (PStunAttributeNonce) pStunAttr;
            CHK_WARN(pStunAttributeNonce->attribute.length <= STUN_MAX_NONCE_LEN, retStatus,
                     "Invalid Nonce found in Refresh Error response. Dropping Packet");
            pTurnConnection->nonceLen = pStunAttributeNonce->attribute.length;
            MEMCPY(pTurnConnection->turnNonce, pStunAttributeNonce->nonce, pTurnConnection->nonceLen);

            CHK_STATUS(turn_connection_updateNonce(pTurnConnection));
            break;

        default:
            /* Remove peer for any other error */
            DLOGW("Received STUN error response. Error type: 0x%02x, Error Code: %u. attribute len %u, Error detail: %s.", stunPacketType,
                  pStunAttributeErrorCode->errorCode, pStunAttributeErrorCode->attribute.length, pStunAttributeErrorCode->errorPhrase);

            /* Find TurnPeer using transaction Id, then mark it as failed */
            for (i = 0; iterate && i < pTurnConnection->turnPeerCount; ++i) {
                pTurnPeer = &pTurnConnection->turnPeerList[i];
                if (transaction_id_store_isExisted(pTurnPeer->pTransactionIdStore, pBuffer + STUN_PACKET_TRANSACTION_ID_OFFSET)) {
                    pTurnPeer->connectionState = TURN_PEER_CONN_STATE_FAILED;
                    iterate = FALSE;
                }
            }
            break;
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (locked) {
        MUTEX_UNLOCK(pTurnConnection->lock);
    }

    if (pStunPacket != NULL) {
        stun_freePacket(&pStunPacket);
    }

    LEAVES();
    return retStatus;
}

static PTurnPeer turn_connection_getPeerByChannelNum(PTurnConnection pTurnConnection, UINT16 channelNumber)
{
    PTurnPeer pTurnPeer = NULL;
    UINT32 i = 0;

    for (; i < pTurnConnection->turnPeerCount; ++i) {
        if (pTurnConnection->turnPeerList[i].channelNumber == channelNumber) {
            pTurnPeer = &pTurnConnection->turnPeerList[i];
        }
    }

    return pTurnPeer;
}
/**
 * @brief turn_connection_handleTcpChannelData will process a single turn channel data item from pBuffer then return. If there is a complete channel
 * data item in buffer, upon return *pTurnChannelDataCount will be 1, *pTurnChannelData will data details about the parsed channel data.
 * *pProcessedDataLen will be the length of data processed.
 *
 * @param[in] pTurnConnection
 * @param[in] pBuffer
 * @param[in] bufferLen
 * @param[in] pChannelData
 * @param[in] pTurnChannelDataCount
 * @param[in] pProcessedDataLen
 *
 * @return STATUS status of execution.
 */
static STATUS turn_connection_handleTcpChannelData(PTurnConnection pTurnConnection, PBYTE pBuffer, UINT32 bufferLen, PTurnChannelData pChannelData,
                                                   PUINT32 pTurnChannelDataCount, PUINT32 pProcessedDataLen)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 bytesToCopy = 0, remainingMsgSize = 0, paddedChannelDataLen = 0, remainingBufLen = 0, channelDataCount = 0;
    PBYTE pCurPos = NULL;
    UINT16 channelNumber = 0;
    PTurnPeer pTurnPeer = NULL;

    CHK(pTurnConnection != NULL && pChannelData != NULL && pTurnChannelDataCount != NULL && pProcessedDataLen != NULL, STATUS_TURN_NULL_ARG);
    CHK(pBuffer != NULL && bufferLen > 0, STATUS_TURN_INVALID_ARG);

    pCurPos = pBuffer;
    remainingBufLen = bufferLen;
    /* process only one channel data and return. Because channel data can be intermixed with STUN packet.
     * need to check remainingBufLen too because channel data could be incomplete. */
    while (remainingBufLen != 0 && channelDataCount == 0) {
        if (pTurnConnection->currRecvDataLen != 0) {
            if (pTurnConnection->currRecvDataLen >= TURN_DATA_CHANNEL_SEND_OVERHEAD) {
                /* pTurnConnection->recvDataBuffer always has channel data start */
                paddedChannelDataLen = ROUND_UP((UINT32) getInt16(*(PINT16)(pTurnConnection->recvDataBuffer + SIZEOF(channelNumber))), 4);
                remainingMsgSize = paddedChannelDataLen - (pTurnConnection->currRecvDataLen - TURN_DATA_CHANNEL_SEND_OVERHEAD);
                bytesToCopy = MIN(remainingMsgSize, remainingBufLen);
                remainingBufLen -= bytesToCopy;

                if (bytesToCopy > (pTurnConnection->recvDataBufferSize - pTurnConnection->currRecvDataLen)) {
                    /* drop current message if it is longer than buffer size. */
                    pTurnConnection->currRecvDataLen = 0;
                    CHK(FALSE, STATUS_TURN_BUFFER_TOO_SMALL);
                }

                MEMCPY(pTurnConnection->recvDataBuffer + pTurnConnection->currRecvDataLen, pCurPos, bytesToCopy);
                pTurnConnection->currRecvDataLen += bytesToCopy;
                pCurPos += bytesToCopy;

                CHECK_EXT(pTurnConnection->currRecvDataLen <= paddedChannelDataLen + TURN_DATA_CHANNEL_SEND_OVERHEAD,
                          "Should not store more than one channel data message in recvDataBuffer");

                /*
                 * once assembled a complete channel data in recvDataBuffer, copy over to completeChannelDataBuffer to
                 * make room for subsequent partial channel data.
                 */
                if (pTurnConnection->currRecvDataLen == (paddedChannelDataLen + TURN_DATA_CHANNEL_SEND_OVERHEAD)) {
                    channelNumber = (UINT16) getInt16(*(PINT16) pTurnConnection->recvDataBuffer);
                    if ((pTurnPeer = turn_connection_getPeerByChannelNum(pTurnConnection, channelNumber)) != NULL) {
                        MEMCPY(pTurnConnection->completeChannelDataBuffer, pTurnConnection->recvDataBuffer, pTurnConnection->currRecvDataLen);
                        pChannelData->data = pTurnConnection->completeChannelDataBuffer + TURN_DATA_CHANNEL_SEND_OVERHEAD;
                        pChannelData->size = GET_STUN_PACKET_SIZE(pTurnConnection->completeChannelDataBuffer);
                        pChannelData->senderAddr = pTurnPeer->address;
                        channelDataCount++;
                    }

                    pTurnConnection->currRecvDataLen = 0;
                }
            } else {
                /* copy just enough to make a complete channel data header */
                bytesToCopy = MIN(remainingMsgSize, TURN_DATA_CHANNEL_SEND_OVERHEAD - pTurnConnection->currRecvDataLen);
                MEMCPY(pTurnConnection->recvDataBuffer + pTurnConnection->currRecvDataLen, pCurPos, bytesToCopy);
                pTurnConnection->currRecvDataLen += bytesToCopy;
                pCurPos += bytesToCopy;
            }
        } else {
            /* new channel message start */
            CHK(*pCurPos == TURN_DATA_CHANNEL_MSG_FIRST_BYTE, STATUS_TURN_MISSING_CHANNEL_DATA_HEADER);

            paddedChannelDataLen = ROUND_UP((UINT32) getInt16(*(PINT16)(pCurPos + SIZEOF(UINT16))), 4);
            if (remainingBufLen >= (paddedChannelDataLen + TURN_DATA_CHANNEL_SEND_OVERHEAD)) {
                channelNumber = (UINT16) getInt16(*(PINT16) pCurPos);
                if ((pTurnPeer = turn_connection_getPeerByChannelNum(pTurnConnection, channelNumber)) != NULL) {
                    pChannelData->data = pCurPos + TURN_DATA_CHANNEL_SEND_OVERHEAD;
                    pChannelData->size = GET_STUN_PACKET_SIZE(pCurPos);
                    pChannelData->senderAddr = pTurnPeer->address;
                    channelDataCount++;
                }

                remainingBufLen -= (paddedChannelDataLen + TURN_DATA_CHANNEL_SEND_OVERHEAD);
                pCurPos += (paddedChannelDataLen + TURN_DATA_CHANNEL_SEND_OVERHEAD);
            } else {
                CHK(pTurnConnection->currRecvDataLen == 0, STATUS_TURN_NEW_DATA_CHANNEL_MSG_HEADER_BEFORE_PREVIOUS_MSG_FINISH);
                CHK(remainingBufLen <= (pTurnConnection->recvDataBufferSize), STATUS_TURN_BUFFER_TOO_SMALL);

                MEMCPY(pTurnConnection->recvDataBuffer, pCurPos, remainingBufLen);
                pTurnConnection->currRecvDataLen += remainingBufLen;
                pCurPos += remainingBufLen;
                remainingBufLen = 0;
            }
        }
    }

    /* return actual channel data count */
    *pTurnChannelDataCount = channelDataCount;
    *pProcessedDataLen = bufferLen - remainingBufLen;

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}
/**
 * @brief
 *
 * @param[in] pTurnConnection the context of the turn connection.
 * @param[in] pBuffer
 * @param[in] bufferLen
 * @param[in] pChannelData
 * @param[in] pTurnChannelDataCount
 * @param[in] pProcessedDataLen
 *
 * @return STATUS status of execution.
 */
static STATUS turn_connection_handleChannelData(PTurnConnection pTurnConnection, PBYTE pBuffer, UINT32 bufferLen, PTurnChannelData pChannelData,
                                                PUINT32 pChannelDataCount, PUINT32 pProcessedDataLen)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;

    UINT32 turnChannelDataCount = 0;
    UINT16 channelNumber = 0;
    PTurnPeer pTurnPeer = NULL;

    CHK(pTurnConnection != NULL && pChannelData != NULL && pChannelDataCount != NULL && pProcessedDataLen != NULL, STATUS_TURN_NULL_ARG);
    CHK(pBuffer != NULL && bufferLen > 0, STATUS_TURN_INVALID_ARG);

    MUTEX_LOCK(pTurnConnection->lock);
    locked = TRUE;

    if (pTurnConnection->protocol == KVS_SOCKET_PROTOCOL_UDP) {
        channelNumber = (UINT16) getInt16(*(PINT16) pBuffer);
        if ((pTurnPeer = turn_connection_getPeerByChannelNum(pTurnConnection, channelNumber)) != NULL) {
            /*
             * Not expecting fragmented channel message in UDP mode.
             * Data channel messages from UDP connection may or may not padded. Thus turn_connection_handleTcpChannelData wont
             * be able to parse it.
             */
            pChannelData->data = pBuffer + TURN_DATA_CHANNEL_SEND_OVERHEAD;
            pChannelData->size = GET_STUN_PACKET_SIZE(pBuffer);
            pChannelData->senderAddr = pTurnPeer->address;
            turnChannelDataCount = 1;

            if (pChannelData->size + TURN_DATA_CHANNEL_SEND_OVERHEAD < bufferLen) {
                DLOGD("Not expecting multiple channel data messages in one UDP packet.");
            }

        } else {
            turnChannelDataCount = 0;
        }
        *pProcessedDataLen = bufferLen;

    } else {
        CHK_STATUS(turn_connection_handleTcpChannelData(pTurnConnection, pBuffer, bufferLen, pChannelData, &turnChannelDataCount, pProcessedDataLen));
    }

    *pChannelDataCount = turnChannelDataCount;

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (locked) {
        MUTEX_UNLOCK(pTurnConnection->lock);
    }

    LEAVES();
    return retStatus;
}

/**
 * @brief   get the context of remote peer according to input ip address.
 *
 * @param[in] pTurnConnection the context of the turn connection.
 * @param[in] pKvsIpAddress the target ip address.
 *
 * @return PTurnPeer the context of remote peer.
 */
static PTurnPeer turn_connection_getPeerByIp(PTurnConnection pTurnConnection, PKvsIpAddress pKvsIpAddress)
{
    PTurnPeer pTurnPeer = NULL;
    UINT32 i = 0;

    for (; i < pTurnConnection->turnPeerCount; ++i) {
        if (net_compareIpAddress(&pTurnConnection->turnPeerList[i].address, pKvsIpAddress, TRUE)) {
            pTurnPeer = &pTurnConnection->turnPeerList[i];
        }
    }

    return pTurnPeer;
}
/**
 * @brief send the stun packet of refresh allocation.
 *
 * @param[in] pTurnConnection the context of the turn connection.
 *
 * @return STATUS status of execution.
 */
static STATUS turn_connection_refreshAllocation(PTurnConnection pTurnConnection)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT64 currTime = 0;
    PStunAttributeLifetime pStunAttributeLifetime = NULL;

    CHK(pTurnConnection != NULL, STATUS_TURN_NULL_ARG);

    currTime = GETTIME();
    // return early if we are not in grace period yet or if we just sent refresh earlier
    CHK(IS_VALID_TIMESTAMP(pTurnConnection->allocationExpirationTime) &&
            currTime + DEFAULT_TURN_ALLOCATION_REFRESH_GRACE_PERIOD >= pTurnConnection->allocationExpirationTime &&
            currTime >= pTurnConnection->nextAllocationRefreshTime,
        retStatus);

    DLOGD("Refresh turn allocation");

    CHK_STATUS(stun_attribute_getByType(pTurnConnection->pTurnAllocationRefreshPacket, STUN_ATTRIBUTE_TYPE_LIFETIME,
                                        (PStunAttributeHeader*) &pStunAttributeLifetime));
    CHK(pStunAttributeLifetime != NULL, STATUS_INTERNAL_ERROR);

    pStunAttributeLifetime->lifetime = DEFAULT_TURN_ALLOCATION_LIFETIME_SECONDS;

    CHK_STATUS(ice_utils_sendStunPacket(pTurnConnection->pTurnAllocationRefreshPacket, pTurnConnection->longTermKey,
                                        ARRAY_SIZE(pTurnConnection->longTermKey), &pTurnConnection->turnServer.ipAddress,
                                        pTurnConnection->pControlChannel, NULL, FALSE));

    pTurnConnection->nextAllocationRefreshTime = currTime + DEFAULT_TURN_SEND_REFRESH_INVERVAL;

CleanUp:

    CHK_LOG_ERR(retStatus);

    return retStatus;
}

static STATUS turn_connection_refreshPermission(PTurnConnection pTurnConnection, PBOOL pNeedRefresh)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT64 currTime = 0;
    PTurnPeer pTurnPeer = NULL;
    BOOL needRefresh = FALSE;
    UINT32 i;

    CHK(pTurnConnection != NULL && pNeedRefresh != NULL, STATUS_TURN_NULL_ARG);

    currTime = GETTIME();

    // refresh all peers whenever one of them expire is close to expiration
    for (i = 0; i < pTurnConnection->turnPeerCount; ++i) {
        pTurnPeer = &pTurnConnection->turnPeerList[i];
        if (IS_VALID_TIMESTAMP(pTurnPeer->permissionExpirationTime) &&
            currTime + DEFAULT_TURN_PERMISSION_REFRESH_GRACE_PERIOD >= pTurnPeer->permissionExpirationTime) {
            DLOGD("Refreshing turn permission");
            needRefresh = TRUE;
        }
    }

CleanUp:

    if (pNeedRefresh != NULL) {
        *pNeedRefresh = needRefresh;
    }

    CHK_LOG_ERR(retStatus);
    return retStatus;
}

static STATUS turn_connection_freePreAllocatedPackets(PTurnConnection pTurnConnection)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pTurnConnection != NULL, STATUS_TURN_NULL_ARG);

    if (pTurnConnection->pTurnPacket != NULL) {
        CHK_STATUS(stun_freePacket(&pTurnConnection->pTurnPacket));
    }

    if (pTurnConnection->pTurnChannelBindPacket != NULL) {
        CHK_STATUS(stun_freePacket(&pTurnConnection->pTurnChannelBindPacket));
    }

    if (pTurnConnection->pTurnCreatePermissionPacket != NULL) {
        CHK_STATUS(stun_freePacket(&pTurnConnection->pTurnCreatePermissionPacket));
    }

    if (pTurnConnection->pTurnAllocationRefreshPacket != NULL) {
        CHK_STATUS(stun_freePacket(&pTurnConnection->pTurnAllocationRefreshPacket));
    }

CleanUp:

    CHK_LOG_ERR(retStatus);
    return retStatus;
}

static VOID turn_connection_throwFatalError(PTurnConnection pTurnConnection, STATUS errorStatus)
{
    if (pTurnConnection == NULL) {
        return;
    }

    /* Assume holding pTurnConnection->lock */
    pTurnConnection->errorStatus = errorStatus;
    pTurnConnection->turnFsmState = TURN_STATE_FAILED;
}
/**
 * @brief the callback for the fsm of the turn connection.
 *
 * @param[in] timerId the timer id.
 * @param[in] currentTime the current time.
 * @param[in] customData the user data.
 *
 * @return STATUS status of execution.
 */
static STATUS turn_connection_timerCallback(UINT32 timerId, UINT64 currentTime, UINT64 customData)
{
    UNUSED_PARAM(timerId);
    UNUSED_PARAM(currentTime);
    STATUS retStatus = STATUS_SUCCESS, sendStatus = STATUS_SUCCESS;
    PTurnConnection pTurnConnection = (PTurnConnection) customData;
    BOOL locked = FALSE, stopScheduling = FALSE;
    PTurnPeer pTurnPeer = NULL;
    PStunAttributeAddress pStunAttributeAddress = NULL;
    PStunAttributeChannelNumber pStunAttributeChannelNumber = NULL;
    PStunAttributeLifetime pStunAttributeLifetime = NULL;
    UINT32 i = 0;

    CHK(pTurnConnection != NULL, STATUS_TURN_NULL_ARG);

    MUTEX_LOCK(pTurnConnection->lock);
    locked = TRUE;

    switch (pTurnConnection->turnFsmState) {
        case TURN_STATE_GET_CREDENTIALS:
            // do not have information of the crendential, so leave the password as blank.
            // when you receive 401 response, you can retrieve the information from it.

            sendStatus = ice_utils_sendStunPacket(pTurnConnection->pTurnPacket, NULL, 0, &pTurnConnection->turnServer.ipAddress,
                                                  pTurnConnection->pControlChannel, NULL, FALSE);
            break;

        case TURN_STATE_ALLOCATION:
            sendStatus =
                ice_utils_sendStunPacket(pTurnConnection->pTurnPacket, pTurnConnection->longTermKey, ARRAY_SIZE(pTurnConnection->longTermKey),
                                         &pTurnConnection->turnServer.ipAddress, pTurnConnection->pControlChannel, NULL, FALSE);
            break;

        case TURN_STATE_CREATE_PERMISSION:
            // explicit fall-through
        case TURN_STATE_BIND_CHANNEL:
            // explicit fall-through
        case TURN_STATE_READY:
            for (i = 0; i < pTurnConnection->turnPeerCount; ++i) {
                pTurnPeer = &pTurnConnection->turnPeerList[i];
                // send the create-permission packets.
                if (pTurnPeer->connectionState == TURN_PEER_CONN_STATE_CREATE_PERMISSION) {
                    // update peer address;
                    CHK_STATUS(stun_attribute_getByType(pTurnConnection->pTurnCreatePermissionPacket, STUN_ATTRIBUTE_TYPE_XOR_PEER_ADDRESS,
                                                        (PStunAttributeHeader*) &pStunAttributeAddress));
                    CHK_WARN(pStunAttributeAddress != NULL, STATUS_INTERNAL_ERROR, "xor peer address attribute not found");
                    pStunAttributeAddress->address = pTurnPeer->address;

                    CHK_STATUS(ice_utils_generateTransactionId(pTurnConnection->pTurnCreatePermissionPacket->header.transactionId,
                                                               ARRAY_SIZE(pTurnConnection->pTurnCreatePermissionPacket->header.transactionId)));

                    CHK(pTurnPeer->pTransactionIdStore != NULL, STATUS_TURN_INVALID_OPERATION);
                    transaction_id_store_insert(pTurnPeer->pTransactionIdStore, pTurnConnection->pTurnCreatePermissionPacket->header.transactionId);
                    // send the packet of create-permission.
                    sendStatus = ice_utils_sendStunPacket(pTurnConnection->pTurnCreatePermissionPacket, pTurnConnection->longTermKey,
                                                          ARRAY_SIZE(pTurnConnection->longTermKey), &pTurnConnection->turnServer.ipAddress,
                                                          pTurnConnection->pControlChannel, NULL, FALSE);
                    // send bind-channel packets.
                } else if (pTurnPeer->connectionState == TURN_PEER_CONN_STATE_BIND_CHANNEL) {
                    // update peer address;
                    CHK_STATUS(stun_attribute_getByType(pTurnConnection->pTurnChannelBindPacket, STUN_ATTRIBUTE_TYPE_XOR_PEER_ADDRESS,
                                                        (PStunAttributeHeader*) &pStunAttributeAddress));
                    CHK_WARN(pStunAttributeAddress != NULL, STATUS_INTERNAL_ERROR, "xor peer address attribute not found");
                    pStunAttributeAddress->address = pTurnPeer->address;

                    // update channel number
                    CHK_STATUS(stun_attribute_getByType(pTurnConnection->pTurnChannelBindPacket, STUN_ATTRIBUTE_TYPE_CHANNEL_NUMBER,
                                                        (PStunAttributeHeader*) &pStunAttributeChannelNumber));
                    CHK_WARN(pStunAttributeChannelNumber != NULL, STATUS_INTERNAL_ERROR, "channel number attribute not found");
                    pStunAttributeChannelNumber->channelNumber = pTurnPeer->channelNumber;

                    CHK_STATUS(ice_utils_generateTransactionId(pTurnConnection->pTurnChannelBindPacket->header.transactionId,
                                                               ARRAY_SIZE(pTurnConnection->pTurnChannelBindPacket->header.transactionId)));

                    CHK(pTurnPeer->pTransactionIdStore != NULL, STATUS_TURN_INVALID_OPERATION);
                    transaction_id_store_insert(pTurnPeer->pTransactionIdStore, pTurnConnection->pTurnChannelBindPacket->header.transactionId);
                    sendStatus = ice_utils_sendStunPacket(pTurnConnection->pTurnChannelBindPacket, pTurnConnection->longTermKey,
                                                          ARRAY_SIZE(pTurnConnection->longTermKey), &pTurnConnection->turnServer.ipAddress,
                                                          pTurnConnection->pControlChannel, NULL, FALSE);
                }
            }

            CHK(turn_connection_refreshAllocation(pTurnConnection) == STATUS_SUCCESS, STATUS_TURN_FSM_REFRESH_ALLOCATION);
            break;

        case TURN_STATE_CLEAN_UP:
            if (ATOMIC_LOAD_BOOL(&pTurnConnection->hasAllocation)) {
                CHK_STATUS(stun_attribute_getByType(pTurnConnection->pTurnAllocationRefreshPacket, STUN_ATTRIBUTE_TYPE_LIFETIME,
                                                    (PStunAttributeHeader*) &pStunAttributeLifetime));
                CHK(pStunAttributeLifetime != NULL, STATUS_INTERNAL_ERROR);
                pStunAttributeLifetime->lifetime = 0;
                sendStatus = ice_utils_sendStunPacket(pTurnConnection->pTurnAllocationRefreshPacket, pTurnConnection->longTermKey,
                                                      ARRAY_SIZE(pTurnConnection->longTermKey), &pTurnConnection->turnServer.ipAddress,
                                                      pTurnConnection->pControlChannel, NULL, FALSE);
                pTurnConnection->deallocatePacketSent = TRUE;
            }

            break;

        case TURN_STATE_FAILED:
            stopScheduling = ATOMIC_LOAD_BOOL(&pTurnConnection->shutdownComplete);
            break;

        default:
            break;
    }

    if (sendStatus == STATUS_SOCKET_CONN_CLOSED_ALREADY) {
        DLOGE("TurnConnection socket %d closed unexpectedly", pTurnConnection->pControlChannel->localSocket);
        turn_connection_throwFatalError(pTurnConnection, sendStatus);
    }

    /* drive the state machine. */
    CHK_STATUS(turn_connection_fsm_step(pTurnConnection));

    /* after turn_connection_fsm_step(), turn state is TURN_STATE_NEW only if TURN_STATE_CLEAN_UP is completed. Thus
     * we can stop the timer. */
    if (pTurnConnection->turnFsmState == TURN_STATE_NEW) {
        stopScheduling = TRUE;
    }

CleanUp:
    CHK_LOG_ERR(sendStatus);
    CHK_LOG_ERR(retStatus);

    if (locked) {
        MUTEX_UNLOCK(pTurnConnection->lock);
    }

    if (stopScheduling) {
        retStatus = STATUS_TIMER_QUEUE_STOP_SCHEDULING;
        if (pTurnConnection != NULL) {
            ATOMIC_STORE(&pTurnConnection->timerCallbackId, MAX_UINT32);
        }
    }

    return retStatus;
}
/**
 * @brief   get the long term key according to the md5 generation of the username, realm, and password.
 *
 * @param[in] username
 * @param[in] realm
 * @param[in] password
 * @param[in, out] pBuffer
 * @param[in] bufferLen
 *
 * @return STATUS status of execution.
 */
static STATUS turn_connection_getLongTermKey(PCHAR username, PCHAR realm, PCHAR password, PBYTE pBuffer, UINT32 bufferLen)
{
    STATUS retStatus = STATUS_SUCCESS;
    CHAR stringBuffer[STUN_MAX_USERNAME_LEN + MAX_ICE_CONFIG_CREDENTIAL_LEN + STUN_MAX_REALM_LEN + 2]; // 2 for two ":" between each value

    CHK(username != NULL && realm != NULL && password != NULL && pBuffer != NULL, STATUS_TURN_NULL_ARG);
    CHK(username[0] != '\0' && realm[0] != '\0' && password[0] != '\0' && bufferLen >= KVS_MD5_DIGEST_LENGTH, STATUS_TURN_INVALID_ARG);
    CHK((STRLEN(username) + STRLEN(realm) + STRLEN(password)) <= ARRAY_SIZE(stringBuffer) - 2, STATUS_TURN_INVALID_ARG);

    SPRINTF(stringBuffer, "%s:%s:%s", username, realm, password);

    // TODO: Return back the error check
    KVS_MD5_DIGEST((PBYTE) stringBuffer, STRLEN(stringBuffer), pBuffer);

CleanUp:

    return retStatus;
}
/**
 * @brief   generate the stun packet of turn allocation.
 *          https://tools.ietf.org/html/rfc5766#section-2.2
 *
 * @param[in] username
 * @param[in] realm
 * @param[in] nonce
 * @param[in] nonceLen
 * @param[in] lifetime  the life time of this turn allocation.
 * @param[out] ppStunPacket the pointer of this stun packet.
 *
 * @return STATUS status of execution
 */
static STATUS turn_connection_packAllocationRequest(PCHAR username, PCHAR realm, PBYTE nonce, UINT16 nonceLen, UINT32 lifetime,
                                                    PStunPacket* ppStunPacket)
{
    STATUS retStatus = STATUS_SUCCESS;
    PStunPacket pTurnAllocateRequest = NULL;

    CHK(ppStunPacket != NULL, STATUS_TURN_NULL_ARG);
    CHK((username == NULL && realm == NULL && nonce == NULL) || (username != NULL && realm != NULL && nonce != NULL && nonceLen > 0),
        STATUS_TURN_INVALID_ARG);

    CHK_STATUS(stun_createPacket(STUN_PACKET_TYPE_ALLOCATE, NULL, &pTurnAllocateRequest));
    CHK_STATUS(stun_attribute_appendLifetime(pTurnAllocateRequest, lifetime));
    CHK_STATUS(stun_attribute_appendRequestedTransport(pTurnAllocateRequest, (UINT8) TURN_REQUEST_TRANSPORT_UDP));

    // either username, realm and nonce are all null or all not null
    if (username != NULL) {
        CHK_STATUS(stun_attribute_appendUsername(pTurnAllocateRequest, username));
        CHK_STATUS(stun_attribute_appendRealm(pTurnAllocateRequest, realm));
        CHK_STATUS(stun_attribute_appendNonce(pTurnAllocateRequest, nonce, nonceLen));
    }

CleanUp:

    if (STATUS_FAILED(retStatus) && pTurnAllocateRequest != NULL) {
        stun_freePacket(&pTurnAllocateRequest);
        pTurnAllocateRequest = NULL;
    }

    if (pTurnAllocateRequest != NULL && ppStunPacket != NULL) {
        *ppStunPacket = pTurnAllocateRequest;
    }

    return retStatus;
}

static PCHAR turn_connection_getStateStr(TURN_CONNECTION_STATE state)
{
    switch (state) {
        case TURN_STATE_NEW:
            return TURN_STATE_NEW_STR;
        case TURN_STATE_CHECK_SOCKET_CONNECTION:
            return TURN_STATE_CHECK_SOCKET_CONNECTION_STR;
        case TURN_STATE_GET_CREDENTIALS:
            return TURN_STATE_GET_CREDENTIALS_STR;
        case TURN_STATE_ALLOCATION:
            return TURN_STATE_ALLOCATION_STR;
        case TURN_STATE_CREATE_PERMISSION:
            return TURN_STATE_CREATE_PERMISSION_STR;
        case TURN_STATE_BIND_CHANNEL:
            return TURN_STATE_BIND_CHANNEL_STR;
        case TURN_STATE_READY:
            return TURN_STATE_READY_STR;
        case TURN_STATE_CLEAN_UP:
            return TURN_STATE_CLEAN_UP_STR;
        case TURN_STATE_FAILED:
            return TURN_STATE_FAILED_STR;
    }
    return TURN_STATE_UNKNOWN_STR;
}

STATUS turn_connection_create(PIceServer pTurnServer, TIMER_QUEUE_HANDLE timerQueueHandle, TURN_CONNECTION_DATA_TRANSFER_MODE dataTransferMode,
                              KVS_SOCKET_PROTOCOL protocol, PTurnConnectionCallbacks pTurnConnectionCallbacks, PSocketConnection pTurnSocket,
                              PConnectionListener pConnectionListener, PTurnConnection* ppTurnConnection)
{
    UNUSED_PARAM(dataTransferMode);
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PTurnConnection pTurnConnection = NULL;

    CHK(pTurnServer != NULL && ppTurnConnection != NULL && pTurnSocket != NULL, STATUS_TURN_NULL_ARG);
    CHK(IS_VALID_TIMER_QUEUE_HANDLE(timerQueueHandle), STATUS_TURN_INVALID_ARG);
    CHK(pTurnServer->isTurn && !IS_EMPTY_STRING(pTurnServer->url) && !IS_EMPTY_STRING(pTurnServer->credential) &&
            !IS_EMPTY_STRING(pTurnServer->username),
        STATUS_TURN_INVALID_ARG);

    // #TBD, #heap, #memory.
    pTurnConnection = (PTurnConnection) MEMCALLOC(
        1, SIZEOF(TurnConnection) + DEFAULT_TURN_MESSAGE_RECV_CHANNEL_DATA_BUFFER_LEN * 2 + DEFAULT_TURN_MESSAGE_SEND_CHANNEL_DATA_BUFFER_LEN);
    CHK(pTurnConnection != NULL, STATUS_NOT_ENOUGH_MEMORY);

    pTurnConnection->lock = MUTEX_CREATE(FALSE);
    pTurnConnection->sendLock = MUTEX_CREATE(FALSE);
    pTurnConnection->freeAllocationCvar = CVAR_CREATE();
    pTurnConnection->timerQueueHandle = timerQueueHandle;
    pTurnConnection->turnServer = *pTurnServer;
    pTurnConnection->turnFsmState = TURN_STATE_NEW;
    pTurnConnection->stateTimeoutTime = INVALID_TIMESTAMP_VALUE;
    pTurnConnection->errorStatus = STATUS_SUCCESS;
    pTurnConnection->timerCallbackId = MAX_UINT32;
    pTurnConnection->pTurnPacket = NULL;
    pTurnConnection->pTurnChannelBindPacket = NULL;
    pTurnConnection->pConnectionListener = pConnectionListener;
    pTurnConnection->dataTransferMode =
        TURN_CONNECTION_DATA_TRANSFER_MODE_DATA_CHANNEL; //!< only TURN_CONNECTION_DATA_TRANSFER_MODE_DATA_CHANNEL for now
    pTurnConnection->protocol = protocol;
    pTurnConnection->relayAddressReported = FALSE;
    pTurnConnection->pControlChannel = pTurnSocket;

    ATOMIC_STORE_BOOL(&pTurnConnection->stopTurnConnection, FALSE);
    ATOMIC_STORE_BOOL(&pTurnConnection->hasAllocation, FALSE);
    ATOMIC_STORE_BOOL(&pTurnConnection->shutdownComplete, FALSE);

    if (pTurnConnectionCallbacks != NULL) {
        pTurnConnection->turnConnectionCallbacks = *pTurnConnectionCallbacks;
    }
    // #TBD, #memory, #heap.
    pTurnConnection->recvDataBufferSize = DEFAULT_TURN_MESSAGE_RECV_CHANNEL_DATA_BUFFER_LEN;
    pTurnConnection->dataBufferSize = DEFAULT_TURN_MESSAGE_SEND_CHANNEL_DATA_BUFFER_LEN;
    pTurnConnection->sendDataBuffer = (PBYTE)(pTurnConnection + 1);
    pTurnConnection->recvDataBuffer = pTurnConnection->sendDataBuffer + pTurnConnection->dataBufferSize;
    pTurnConnection->completeChannelDataBuffer =
        pTurnConnection->sendDataBuffer + pTurnConnection->dataBufferSize + pTurnConnection->recvDataBufferSize;
    pTurnConnection->currRecvDataLen = 0;
    pTurnConnection->allocationExpirationTime = INVALID_TIMESTAMP_VALUE;
    pTurnConnection->nextAllocationRefreshTime = 0;
    pTurnConnection->currentTimerCallingPeriod = DEFAULT_TURN_TIMER_INTERVAL_BEFORE_READY;

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (STATUS_FAILED(retStatus) && pTurnConnection != NULL) {
        turn_connection_free(&pTurnConnection);
    }

    if (ppTurnConnection != NULL) {
        *ppTurnConnection = pTurnConnection;
    }

    LEAVES();
    return retStatus;
}

STATUS turn_connection_free(PTurnConnection* ppTurnConnection)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PTurnConnection pTurnConnection = NULL;
    PTurnPeer pTurnPeer = NULL;
    SIZE_T timerCallbackId;
    UINT32 i;

    CHK(ppTurnConnection != NULL, STATUS_TURN_NULL_ARG);
    // free is idempotent
    CHK(*ppTurnConnection != NULL, retStatus);

    pTurnConnection = *ppTurnConnection;

    timerCallbackId = ATOMIC_EXCHANGE(&pTurnConnection->timerCallbackId, MAX_UINT32);
    if (timerCallbackId != MAX_UINT32) {
        CHK_LOG_ERR(timer_queue_cancelTimer(pTurnConnection->timerQueueHandle, (UINT32) timerCallbackId, (UINT64) pTurnConnection));
    }

    // shutdown control channel
    if (pTurnConnection->pControlChannel) {
        CHK_LOG_ERR(connection_listener_remove(pTurnConnection->pConnectionListener, pTurnConnection->pControlChannel));
        CHK_LOG_ERR(socket_connection_free(&pTurnConnection->pControlChannel));
    }

    // free transactionId store for each turn peer
    for (i = 0; i < pTurnConnection->turnPeerCount; ++i) {
        pTurnPeer = &pTurnConnection->turnPeerList[i];
        transaction_id_store_free(&pTurnPeer->pTransactionIdStore);
    }

    if (IS_VALID_MUTEX_VALUE(pTurnConnection->lock)) {
        /* in case some thread is in the middle of a turn api call. */
        MUTEX_LOCK(pTurnConnection->lock);
        MUTEX_UNLOCK(pTurnConnection->lock);
        MUTEX_FREE(pTurnConnection->lock);
    }

    if (IS_VALID_MUTEX_VALUE(pTurnConnection->sendLock)) {
        /* in case some thread is in the middle of a turn api call. */
        MUTEX_LOCK(pTurnConnection->sendLock);
        MUTEX_UNLOCK(pTurnConnection->sendLock);
        MUTEX_FREE(pTurnConnection->sendLock);
    }

    if (IS_VALID_CVAR_VALUE(pTurnConnection->freeAllocationCvar)) {
        CVAR_FREE(pTurnConnection->freeAllocationCvar);
    }

    turn_connection_freePreAllocatedPackets(pTurnConnection);

    MEMFREE(pTurnConnection);

    *ppTurnConnection = NULL;

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS turn_connection_handleInboundData(PTurnConnection pTurnConnection, PBYTE pBuffer, UINT32 bufferLen, PKvsIpAddress pSrc, PKvsIpAddress pDest,
                                         PTurnChannelData channelDataList, PUINT32 pChannelDataCount)
{
    ENTERS();
    UNUSED_PARAM(pSrc);
    UNUSED_PARAM(pDest);
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 remainingDataSize = 0, processedDataLen, channelDataCount = 0, totalChannelDataCount = 0, channelDataListSize;
    PBYTE pCurrent = NULL;

    CHK(pTurnConnection != NULL && channelDataList != NULL && pChannelDataCount != NULL, STATUS_TURN_NULL_ARG);
    CHK_WARN(bufferLen > 0 && pBuffer != NULL, retStatus, "Got empty buffer");

    /* initially pChannelDataCount contains size of channelDataList */
    channelDataListSize = *pChannelDataCount;
    remainingDataSize = bufferLen;
    pCurrent = pBuffer;
    while (remainingDataSize > 0 && totalChannelDataCount < channelDataListSize) {
        processedDataLen = 0;
        channelDataCount = 0;
        // stun packets.
        if (IS_STUN_PACKET(pCurrent)) {
            processedDataLen = GET_STUN_PACKET_SIZE(pCurrent) + STUN_HEADER_LEN; /* size of entire STUN packet */
            // error packets.
            if (STUN_PACKET_IS_TYPE_ERROR(pCurrent)) {
                CHK_STATUS(turn_connection_handleInboundStunError(pTurnConnection, pCurrent, processedDataLen));
            } else {
                // normal packets.
                CHK_STATUS(turn_connection_handleInboundStun(pTurnConnection, pCurrent, processedDataLen));
            }
        } else {
            /* must be channel data if not stun */
            CHK_STATUS(turn_connection_handleChannelData(pTurnConnection, pCurrent, remainingDataSize, &channelDataList[totalChannelDataCount],
                                                         &channelDataCount, &processedDataLen));
        }

        CHK(remainingDataSize >= processedDataLen, STATUS_INVALID_ARG_LEN);
        pCurrent += processedDataLen;
        remainingDataSize -= processedDataLen;
        /* channelDataCount will be either 1 or 0 */
        totalChannelDataCount += channelDataCount;
    }

    *pChannelDataCount = totalChannelDataCount;

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS turn_connection_addPeer(PTurnConnection pTurnConnection, PKvsIpAddress pPeerAddress)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PTurnPeer pTurnPeer = NULL;
    BOOL locked = FALSE;

    CHK(pTurnConnection != NULL && pPeerAddress != NULL, STATUS_TURN_NULL_ARG);
    CHK(pTurnConnection->turnServer.ipAddress.family == pPeerAddress->family, STATUS_TURN_INVALID_ARG);
    CHK_WARN(IS_IPV4_ADDR(pPeerAddress), retStatus, "Drop IPv6 turn peer because only IPv4 turn peer is supported right now");

    MUTEX_LOCK(pTurnConnection->lock);
    locked = TRUE;

    // check for duplicate
    CHK(turn_connection_getPeerByIp(pTurnConnection, pPeerAddress) == NULL, retStatus);
    CHK_WARN(pTurnConnection->turnPeerCount < DEFAULT_TURN_MAX_PEER_COUNT, STATUS_TURN_INVALID_OPERATION, "Add peer failed. Max peer count reached");

    pTurnPeer = &pTurnConnection->turnPeerList[pTurnConnection->turnPeerCount++];

    pTurnPeer->connectionState = TURN_PEER_CONN_STATE_CREATE_PERMISSION;
    pTurnPeer->address = *pPeerAddress;
    pTurnPeer->xorAddress = *pPeerAddress;
    /* safe to down cast because DEFAULT_TURN_MAX_PEER_COUNT is enforced */
    pTurnPeer->channelNumber = (UINT16) pTurnConnection->turnPeerCount + TURN_CHANNEL_BIND_CHANNEL_NUMBER_BASE;
    pTurnPeer->permissionExpirationTime = INVALID_TIMESTAMP_VALUE;
    pTurnPeer->ready = FALSE;

    CHK_STATUS(stun_xorIpAddress(&pTurnPeer->xorAddress, NULL)); /* only work for IPv4 for now */
    CHK_STATUS(transaction_id_store_create(DEFAULT_MAX_STORED_TRANSACTION_ID_COUNT, &pTurnPeer->pTransactionIdStore));
    pTurnPeer = NULL;

CleanUp:

    if (STATUS_FAILED(retStatus) && pTurnPeer != NULL) {
        transaction_id_store_free(&pTurnPeer->pTransactionIdStore);
        pTurnConnection->turnPeerCount--;
    }

    if (locked) {
        MUTEX_UNLOCK(pTurnConnection->lock);
    }

    LEAVES();
    return retStatus;
}

STATUS turn_connection_send(PTurnConnection pTurnConnection, PBYTE pBuf, UINT32 bufLen, PKvsIpAddress pDestIp)
{
    STATUS retStatus = STATUS_SUCCESS;
    PTurnPeer pSendPeer = NULL;
    UINT32 paddedDataLen = 0;
    CHAR ipAddrStr[KVS_IP_ADDRESS_STRING_BUFFER_LEN];
    BOOL locked = FALSE;
    BOOL sendLocked = FALSE;

    CHK(pTurnConnection != NULL && pDestIp != NULL, STATUS_TURN_NULL_ARG);
    CHK(pBuf != NULL && bufLen > 0, STATUS_TURN_INVALID_ARG);

    MUTEX_LOCK(pTurnConnection->lock);
    locked = TRUE;
    // check the status of turn connection.
    if (!(pTurnConnection->turnFsmState == TURN_STATE_CREATE_PERMISSION || pTurnConnection->turnFsmState == TURN_STATE_BIND_CHANNEL ||
          pTurnConnection->turnFsmState == TURN_STATE_READY)) {
        DLOGV("TurnConnection not ready to send data");

        // If turn is not ready yet. Drop the send since ice will retry.
        CHK(FALSE, retStatus);
    }
    // get the turn peer with the ip.
    pSendPeer = turn_connection_getPeerByIp(pTurnConnection, pDestIp);

    CHK_STATUS(net_getIpAddrStr(pDestIp, ipAddrStr, ARRAY_SIZE(ipAddrStr)));
    if (pSendPeer == NULL) {
        DLOGV("Unable to send data through turn because peer with address %s:%u is not found", ipAddrStr, KVS_GET_IP_ADDRESS_PORT(pDestIp));
        CHK(FALSE, retStatus);
    } else if (pSendPeer->connectionState == TURN_PEER_CONN_STATE_FAILED) {
        CHK(FALSE, STATUS_TURN_PEER_NOT_USABLE);
    } else if (!pSendPeer->ready) {
        DLOGV("Unable to send data through turn because turn channel is not established with peer with address %s:%u", ipAddrStr,
              KVS_GET_IP_ADDRESS_PORT(pDestIp));
        CHK(FALSE, retStatus);
    }

    MUTEX_UNLOCK(pTurnConnection->lock);
    locked = FALSE;

    /* need to serialize send because every send load data into the same buffer pTurnConnection->sendDataBuffer */
    MUTEX_LOCK(pTurnConnection->sendLock);
    sendLocked = TRUE;

    CHK(pTurnConnection->dataBufferSize - TURN_DATA_CHANNEL_SEND_OVERHEAD >= bufLen, STATUS_TURN_BUFFER_TOO_SMALL);
    /**
     * Over TCP and TLS-over-TCP, the ChannelData message MUST be padded to
     * a multiple of four bytes in order to ensure the alignment of
     * subsequent messages. The padding is not reflected in the length
     * field of the ChannelData message, so the actual size of a ChannelData
     * message (including padding) is (4 + Length) rounded up to the nearest
     * multiple of 4
     * https://tools.ietf.org/html/rfc5766#section-11.5
     */
    paddedDataLen = (UINT32) ROUND_UP(TURN_DATA_CHANNEL_SEND_OVERHEAD + bufLen, 4);

    /* generate data channel TURN message */
    putInt16((PINT16)(pTurnConnection->sendDataBuffer), pSendPeer->channelNumber);
    putInt16((PINT16)(pTurnConnection->sendDataBuffer + 2), (UINT16) bufLen);
    MEMCPY(pTurnConnection->sendDataBuffer + TURN_DATA_CHANNEL_SEND_OVERHEAD, pBuf, bufLen);
    // send the packet after re-mapping.
    retStatus = ice_utils_send(pTurnConnection->sendDataBuffer, paddedDataLen, &pTurnConnection->turnServer.ipAddress,
                               pTurnConnection->pControlChannel, NULL, FALSE);

    if (STATUS_FAILED(retStatus)) {
        DLOGW("ice_utils_send failed with 0x%08x", retStatus);
        retStatus = STATUS_SUCCESS;
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (sendLocked) {
        MUTEX_UNLOCK(pTurnConnection->sendLock);
    }

    if (locked) {
        MUTEX_UNLOCK(pTurnConnection->lock);
    }

    return retStatus;
}

STATUS turn_connection_start(PTurnConnection pTurnConnection)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;
    SIZE_T timerCallbackId;

    CHK(pTurnConnection != NULL, STATUS_TURN_NULL_ARG);

    MUTEX_LOCK(pTurnConnection->lock);
    locked = TRUE;

    /* only execute when turnConnection is in TURN_STATE_NEW */
    CHK(pTurnConnection->turnFsmState == TURN_STATE_NEW, retStatus);

    MUTEX_UNLOCK(pTurnConnection->lock);
    locked = FALSE;

    timerCallbackId = ATOMIC_EXCHANGE(&pTurnConnection->timerCallbackId, MAX_UINT32);
    if (timerCallbackId != MAX_UINT32) {
        CHK_STATUS(timer_queue_cancelTimer(pTurnConnection->timerQueueHandle, (UINT32) timerCallbackId, (UINT64) pTurnConnection));
    }

    /* schedule the timer, which will drive the state machine. */
    CHK_STATUS(timer_queue_addTimer(pTurnConnection->timerQueueHandle, KVS_ICE_DEFAULT_TIMER_START_DELAY, pTurnConnection->currentTimerCallingPeriod,
                                    turn_connection_timerCallback, (UINT64) pTurnConnection, (PUINT32) &timerCallbackId));

    ATOMIC_STORE(&pTurnConnection->timerCallbackId, timerCallbackId);

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (locked) {
        MUTEX_UNLOCK(pTurnConnection->lock);
    }

    return retStatus;
}

STATUS turn_connection_fsm_step(PTurnConnection pTurnConnection)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 readyPeerCount = 0, channelWithPermissionCount = 0;
    UINT64 currentTime = GETTIME();
    CHAR ipAddrStr[KVS_IP_ADDRESS_STRING_BUFFER_LEN];
    TURN_CONNECTION_STATE previousState = TURN_STATE_NEW;
    BOOL refreshPeerPermission = FALSE;
    UINT32 i = 0;

    CHK(pTurnConnection != NULL, STATUS_TURN_NULL_ARG);

    previousState = pTurnConnection->turnFsmState;

    switch (pTurnConnection->turnFsmState) {
        case TURN_STATE_NEW:
            // create empty turn allocation request
            CHK_STATUS(
                turn_connection_packAllocationRequest(NULL, NULL, NULL, 0, DEFAULT_TURN_ALLOCATION_LIFETIME_SECONDS, &pTurnConnection->pTurnPacket));

            pTurnConnection->turnFsmState = TURN_STATE_CHECK_SOCKET_CONNECTION;
            pTurnConnection->stateTimeoutTime = currentTime + DEFAULT_TURN_SOCKET_CONNECT_TIMEOUT;
            break;

        case TURN_STATE_CHECK_SOCKET_CONNECTION:
            if (socket_connection_isConnected(pTurnConnection->pControlChannel)) {
                /* initialize TLS once tcp connection is established */
                /* Start receiving data for TLS handshake */
                ATOMIC_STORE_BOOL(&pTurnConnection->pControlChannel->receiveData, TRUE);

                /* We dont support DTLS and TCP, so only options are TCP/TLS and UDP. */
                /* TODO: add plain TCP once it becomes available. */
                if (pTurnConnection->protocol == KVS_SOCKET_PROTOCOL_TCP && pTurnConnection->pControlChannel->pTlsSession == NULL) {
                    CHK_STATUS(socket_connection_initSecureConnection(pTurnConnection->pControlChannel, FALSE));
                }

                pTurnConnection->turnFsmState = TURN_STATE_GET_CREDENTIALS;
                pTurnConnection->stateTimeoutTime = currentTime + DEFAULT_TURN_GET_CREDENTIAL_TIMEOUT;
            } else {
                CHK(currentTime < pTurnConnection->stateTimeoutTime, STATUS_TURN_CHECK_CONN_TIMEOUT);
            }

        case TURN_STATE_GET_CREDENTIALS:

            if (pTurnConnection->credentialObtained) {
                DLOGV("Updated turn allocation request credential after receiving 401");

                // update turn allocation packet with credentials
                CHK_STATUS(stun_freePacket(&pTurnConnection->pTurnPacket));
                CHK_STATUS(turn_connection_getLongTermKey(pTurnConnection->turnServer.username, pTurnConnection->turnRealm,
                                                          pTurnConnection->turnServer.credential, pTurnConnection->longTermKey,
                                                          SIZEOF(pTurnConnection->longTermKey)));
                // we extract the realm and nonce fromt the 401 response of the first turn allocation packet, and
                // send the turn allocation packet again.
                // DLOGD("username: %s, turnRealm: %s, turnNonce: %s, nonceLen: %d", pTurnConnection->turnServer.username,
                //                                                                    pTurnConnection->turnRealm,
                //                                                                    pTurnConnection->turnNonce,
                //                                                                    pTurnConnection->nonceLen);
                CHK_STATUS(turn_connection_packAllocationRequest(pTurnConnection->turnServer.username, pTurnConnection->turnRealm,
                                                                 pTurnConnection->turnNonce, pTurnConnection->nonceLen,
                                                                 DEFAULT_TURN_ALLOCATION_LIFETIME_SECONDS, &pTurnConnection->pTurnPacket));

                pTurnConnection->turnFsmState = TURN_STATE_ALLOCATION;
                pTurnConnection->stateTimeoutTime = currentTime + DEFAULT_TURN_ALLOCATION_TIMEOUT;
            } else {
                CHK(currentTime < pTurnConnection->stateTimeoutTime, STATUS_TURN_GET_CREDENTIALS_TIMEOUT);
            }
            break;

        case TURN_STATE_ALLOCATION:
            // already get the allocation response.
            if (ATOMIC_LOAD_BOOL(&pTurnConnection->hasAllocation)) {
                CHK_STATUS(net_getIpAddrStr(&pTurnConnection->relayAddress, ipAddrStr, ARRAY_SIZE(ipAddrStr)));
                DLOGD("Relay address received: %s, port: %u", ipAddrStr, (UINT16) getInt16(pTurnConnection->relayAddress.port));

                if (pTurnConnection->pTurnCreatePermissionPacket != NULL) {
                    CHK_STATUS(stun_freePacket(&pTurnConnection->pTurnCreatePermissionPacket));
                }
                // create the packet of permsssion.
                CHK_STATUS(stun_createPacket(STUN_PACKET_TYPE_CREATE_PERMISSION, NULL, &pTurnConnection->pTurnCreatePermissionPacket));
                // use host address as placeholder. hostAddress should have the same family as peer address
                // host address.
                CHK_STATUS(stun_attribute_appendAddress(pTurnConnection->pTurnCreatePermissionPacket, STUN_ATTRIBUTE_TYPE_XOR_PEER_ADDRESS,
                                                        &pTurnConnection->hostAddress));
                // server username.
                CHK_STATUS(stun_attribute_appendUsername(pTurnConnection->pTurnCreatePermissionPacket, pTurnConnection->turnServer.username));
                // realm
                CHK_STATUS(stun_attribute_appendRealm(pTurnConnection->pTurnCreatePermissionPacket, pTurnConnection->turnRealm));
                // nonce
                CHK_STATUS(
                    stun_attribute_appendNonce(pTurnConnection->pTurnCreatePermissionPacket, pTurnConnection->turnNonce, pTurnConnection->nonceLen));

                // create channel bind packet here too so for each peer as soon as permission is created, it can start
                // sending chaneel bind request
                if (pTurnConnection->pTurnChannelBindPacket != NULL) {
                    CHK_STATUS(stun_freePacket(&pTurnConnection->pTurnChannelBindPacket));
                }
                CHK_STATUS(stun_createPacket(STUN_PACKET_TYPE_CHANNEL_BIND_REQUEST, NULL, &pTurnConnection->pTurnChannelBindPacket));
                // use host address as placeholder
                CHK_STATUS(stun_attribute_appendAddress(pTurnConnection->pTurnChannelBindPacket, STUN_ATTRIBUTE_TYPE_XOR_PEER_ADDRESS,
                                                        &pTurnConnection->hostAddress));
                CHK_STATUS(stun_attribute_appendChannelNumber(pTurnConnection->pTurnChannelBindPacket, 0));
                CHK_STATUS(stun_attribute_appendUsername(pTurnConnection->pTurnChannelBindPacket, pTurnConnection->turnServer.username));
                CHK_STATUS(stun_attribute_appendRealm(pTurnConnection->pTurnChannelBindPacket, pTurnConnection->turnRealm));
                CHK_STATUS(
                    stun_attribute_appendNonce(pTurnConnection->pTurnChannelBindPacket, pTurnConnection->turnNonce, pTurnConnection->nonceLen));

                if (pTurnConnection->pTurnAllocationRefreshPacket != NULL) {
                    CHK_STATUS(stun_freePacket(&pTurnConnection->pTurnAllocationRefreshPacket));
                }
                CHK_STATUS(stun_createPacket(STUN_PACKET_TYPE_REFRESH, NULL, &pTurnConnection->pTurnAllocationRefreshPacket));
                CHK_STATUS(stun_attribute_appendLifetime(pTurnConnection->pTurnAllocationRefreshPacket, DEFAULT_TURN_ALLOCATION_LIFETIME_SECONDS));
                CHK_STATUS(stun_attribute_appendUsername(pTurnConnection->pTurnAllocationRefreshPacket, pTurnConnection->turnServer.username));
                CHK_STATUS(stun_attribute_appendRealm(pTurnConnection->pTurnAllocationRefreshPacket, pTurnConnection->turnRealm));
                CHK_STATUS(
                    stun_attribute_appendNonce(pTurnConnection->pTurnAllocationRefreshPacket, pTurnConnection->turnNonce, pTurnConnection->nonceLen));

                pTurnConnection->turnFsmState = TURN_STATE_CREATE_PERMISSION;
                pTurnConnection->stateTimeoutTime = currentTime + DEFAULT_TURN_CREATE_PERMISSION_TIMEOUT;

            } else {
                CHK(currentTime < pTurnConnection->stateTimeoutTime, STATUS_TURN_ALLOCATION_TIMEOUT);
            }
            break;

        case TURN_STATE_CREATE_PERMISSION:

            for (i = 0; i < pTurnConnection->turnPeerCount; ++i) {
                // As soon as create permission succeeded, we start sending channel bind message.
                // So connectionState could've already advanced to ready state.
                if (pTurnConnection->turnPeerList[i].connectionState == TURN_PEER_CONN_STATE_BIND_CHANNEL ||
                    pTurnConnection->turnPeerList[i].connectionState == TURN_PEER_CONN_STATE_READY) {
                    channelWithPermissionCount++;
                }
            }

            // push back timeout if no peer is available yet
            if (pTurnConnection->turnPeerCount == 0) {
                pTurnConnection->stateTimeoutTime = currentTime + DEFAULT_TURN_CREATE_PERMISSION_TIMEOUT;
                CHK(FALSE, retStatus);
            }
            // if timeout is done,
            if (currentTime >= pTurnConnection->stateTimeoutTime) {
                CHK(channelWithPermissionCount > 0, STATUS_TURN_FAILED_TO_CREATE_PERMISSION);

                // go to next state if we have at least one ready peer
                pTurnConnection->turnFsmState = TURN_STATE_BIND_CHANNEL;
                pTurnConnection->stateTimeoutTime = currentTime + DEFAULT_TURN_BIND_CHANNEL_TIMEOUT;
            }
            break;

        case TURN_STATE_BIND_CHANNEL:
            // check the stae of each peers.
            for (i = 0; i < pTurnConnection->turnPeerCount; ++i) {
                if (pTurnConnection->turnPeerList[i].connectionState == TURN_PEER_CONN_STATE_READY) {
                    readyPeerCount++;
                }
            }
            // if timeout or the number of ready peer is reached, we switch to ready state.
            if (currentTime >= pTurnConnection->stateTimeoutTime || readyPeerCount == pTurnConnection->turnPeerCount) {
                CHK(readyPeerCount > 0, STATUS_TURN_FAILED_TO_BIND_CHANNEL);
                // go to next state if we have at least one ready peer
                pTurnConnection->turnFsmState = TURN_STATE_READY;
            }
            break;

        case TURN_STATE_READY:

            CHK_STATUS(turn_connection_refreshPermission(pTurnConnection, &refreshPeerPermission));
            // need to refresh permission.
            if (refreshPeerPermission) {
                // reset pTurnPeer->connectionState to make them go through create permission and channel bind again
                for (i = 0; i < pTurnConnection->turnPeerCount; ++i) {
                    pTurnConnection->turnPeerList[i].connectionState = TURN_PEER_CONN_STATE_CREATE_PERMISSION;
                }

                pTurnConnection->currentTimerCallingPeriod = DEFAULT_TURN_TIMER_INTERVAL_BEFORE_READY;
                CHK_STATUS(timer_queue_updateTimerPeriod(pTurnConnection->timerQueueHandle, (UINT64) pTurnConnection,
                                                         (UINT32) ATOMIC_LOAD(&pTurnConnection->timerCallbackId),
                                                         pTurnConnection->currentTimerCallingPeriod));
                pTurnConnection->turnFsmState = TURN_STATE_CREATE_PERMISSION;
                pTurnConnection->stateTimeoutTime = currentTime + DEFAULT_TURN_CREATE_PERMISSION_TIMEOUT;
            } else if (pTurnConnection->currentTimerCallingPeriod != DEFAULT_TURN_TIMER_INTERVAL_AFTER_READY) {
                // use longer timer interval as now it just needs to check disconnection and permission expiration.
                pTurnConnection->currentTimerCallingPeriod = DEFAULT_TURN_TIMER_INTERVAL_AFTER_READY;
                CHK_STATUS(timer_queue_updateTimerPeriod(pTurnConnection->timerQueueHandle, (UINT64) pTurnConnection,
                                                         (UINT32) ATOMIC_LOAD(&pTurnConnection->timerCallbackId),
                                                         pTurnConnection->currentTimerCallingPeriod));
            }

            break;

        case TURN_STATE_CLEAN_UP:
            /* start cleanning up even if we dont receive allocation freed response in time, or if connection is already closed,
             * since we already sent multiple STUN refresh packets with 0 lifetime. */
            if (socket_connection_isClosed(pTurnConnection->pControlChannel) || !ATOMIC_LOAD_BOOL(&pTurnConnection->hasAllocation) ||
                currentTime >= pTurnConnection->stateTimeoutTime) {
                // clean transactionId store for each turn peer, preserving the peers
                for (i = 0; i < pTurnConnection->turnPeerCount; ++i) {
                    transaction_id_store_reset(pTurnConnection->turnPeerList[i].pTransactionIdStore);
                }

                CHK_STATUS(turn_connection_freePreAllocatedPackets(pTurnConnection));
                CHK_STATUS(socket_connection_close(pTurnConnection->pControlChannel));
                pTurnConnection->turnFsmState = STATUS_SUCCEEDED(pTurnConnection->errorStatus) ? TURN_STATE_NEW : TURN_STATE_FAILED;
                ATOMIC_STORE_BOOL(&pTurnConnection->shutdownComplete, TRUE);
            }

            break;

        case TURN_STATE_FAILED:
            DLOGW("TurnConnection in TURN_STATE_FAILED due to 0x%08x. Aborting TurnConnection", pTurnConnection->errorStatus);
            /* Since we are aborting, not gonna do cleanup */
            ATOMIC_STORE_BOOL(&pTurnConnection->hasAllocation, FALSE);
            /* If we haven't done cleanup, go to cleanup state which will do the cleanup then go to failed state again. */
            if (!ATOMIC_LOAD_BOOL(&pTurnConnection->shutdownComplete)) {
                pTurnConnection->turnFsmState = TURN_STATE_CLEAN_UP;
                pTurnConnection->stateTimeoutTime = currentTime + DEFAULT_TURN_CLEAN_UP_TIMEOUT;
            }

            break;

        default:
            break;
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (STATUS_SUCCEEDED(retStatus) && ATOMIC_LOAD_BOOL(&pTurnConnection->stopTurnConnection) &&
        pTurnConnection->turnFsmState != TURN_STATE_CLEAN_UP && pTurnConnection->turnFsmState != TURN_STATE_NEW) {
        pTurnConnection->turnFsmState = TURN_STATE_CLEAN_UP;
        pTurnConnection->stateTimeoutTime = currentTime + DEFAULT_TURN_CLEAN_UP_TIMEOUT;
    }

    /* move to failed state if retStatus is failed status and state is not yet TURN_STATE_FAILED */
    if (STATUS_FAILED(retStatus) && pTurnConnection->turnFsmState != TURN_STATE_FAILED) {
        pTurnConnection->errorStatus = retStatus;
        pTurnConnection->turnFsmState = TURN_STATE_FAILED;
        /* fix up state to trigger transition into TURN_STATE_FAILED  */
        retStatus = STATUS_SUCCESS;
    }

    if (pTurnConnection != NULL && previousState != pTurnConnection->turnFsmState) {
        DLOGD("TurnConnection state changed from %s to %s", turn_connection_getStateStr(previousState),
              turn_connection_getStateStr(pTurnConnection->turnFsmState));
    }

    LEAVES();
    return retStatus;
}

STATUS turn_connection_shutdown(PTurnConnection pTurnConnection, UINT64 waitUntilAllocationFreedTimeout)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT64 currentTime = 0, timeoutTime = 0;
    BOOL locked = FALSE;
    CHK(pTurnConnection != NULL, STATUS_TURN_NULL_ARG);

    ATOMIC_STORE(&pTurnConnection->stopTurnConnection, TRUE);

    MUTEX_LOCK(pTurnConnection->lock);
    locked = TRUE;

    CHK(ATOMIC_LOAD_BOOL(&pTurnConnection->hasAllocation), retStatus);

    if (waitUntilAllocationFreedTimeout > 0) {
        currentTime = GETTIME();
        timeoutTime = currentTime + waitUntilAllocationFreedTimeout;

        while (ATOMIC_LOAD_BOOL(&pTurnConnection->hasAllocation) && currentTime < timeoutTime) {
            CVAR_WAIT(pTurnConnection->freeAllocationCvar, pTurnConnection->lock, waitUntilAllocationFreedTimeout);
            currentTime = GETTIME();
        }

        if (ATOMIC_LOAD_BOOL(&pTurnConnection->hasAllocation)) {
            DLOGD("Failed to free turn allocation within timeout of %" PRIu64 " milliseconds",
                  waitUntilAllocationFreedTimeout / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
        }
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (locked) {
        MUTEX_UNLOCK(pTurnConnection->lock);
    }

    return retStatus;
}

BOOL turn_connection_isShutdownCompleted(PTurnConnection pTurnConnection)
{
    if (pTurnConnection == NULL) {
        return TRUE;
    } else {
        return ATOMIC_LOAD_BOOL(&pTurnConnection->shutdownComplete);
    }
}

BOOL turn_connection_getRelayAddress(PTurnConnection pTurnConnection, PKvsIpAddress pKvsIpAddress)
{
    if (pTurnConnection == NULL || !ATOMIC_LOAD_BOOL(&pTurnConnection->hasAllocation)) {
        return FALSE;
    } else if (pKvsIpAddress != NULL) {
        MUTEX_LOCK(pTurnConnection->lock);
        *pKvsIpAddress = pTurnConnection->relayAddress;
        MUTEX_UNLOCK(pTurnConnection->lock);
        return TRUE;
    }

    return FALSE;
}
