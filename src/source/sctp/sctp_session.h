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
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_SCTP_SCTP__
#define __KINESIS_VIDEO_WEBRTC_CLIENT_SCTP_SCTP__

#ifdef ENABLE_DATA_CHANNEL

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#include <usrsctp.h>
#include "kvs/WebRTCClient.h"
/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
/**
 * @brief WebRTC Protocol Layers
 *
 *               +------+------+------+
 *               | DCEP | UTF-8|Binary|
 *               |      | Data | Data |
 *               +------+------+------+
 *               | SCTP               |
 * +----------------------------------+
 * | STUN | SRTP | DTLS               |
 * +----------------------------------+
 * | ICE                              |
 * +----------------------------------+
 * | UDP1 | UDP2 | UDP3 | ...         |
 * +----------------------------------+
 */
// 1200 - 12 (SCTP header Size)
/**
 * @brief https://tools.ietf.org/html/rfc8831, The initial Path
 * MTU at the IP layer SHOULD NOT exceed 1200 bytes for IPv4 and 1280
 * bytes for IPv6.
 */
#define SCTP_MTU                         1188
#define SCTP_ASSOCIATION_DEFAULT_PORT    5000
#define SCTP_DCEP_HEADER_LENGTH          12
#define SCTP_DCEP_LABEL_LEN_OFFSET       8
#define SCTP_DCEP_LABEL_OFFSET           12
#define SCTP_MAX_ALLOWABLE_PACKET_LENGTH (SCTP_DCEP_HEADER_LENGTH + MAX_DATA_CHANNEL_NAME_LEN + MAX_DATA_CHANNEL_PROTOCOL_LEN + 2)

#define SCTP_SESSION_ACTIVE             0
#define SCTP_SESSION_SHUTDOWN_INITIATED 1
#define SCTP_SESSION_SHUTDOWN_COMPLETED 2

#define DEFAULT_SCTP_SHUTDOWN_TIMEOUT 2 * HUNDREDS_OF_NANOS_IN_A_SECOND

#define DEFAULT_USRSCTP_TEARDOWN_POLLING_INTERVAL (10 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND)

enum { SCTP_PPID_DCEP = 50, SCTP_PPID_STRING = 51, SCTP_PPID_BINARY = 53, SCTP_PPID_STRING_EMPTY = 56, SCTP_PPID_BINARY_EMPTY = 57 };

enum {
    DCEP_DATA_CHANNEL_OPEN = 0x03,
};

typedef enum {
    DCEP_DATA_CHANNEL_RELIABLE_ORDERED = (BYTE) 0x00,   //< The Data Channel provides a reliable in-order bi-directional communication
    DCEP_DATA_CHANNEL_RELIABLE_UNORDERED = (BYTE) 0x80, //!< The Data Channel provides a reliable unordered bi-directional communication
    DCEP_DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT_UNORDERED =
        (BYTE) 0x81, //!< The Data Channel provides a partial reliable unordered bi-directional communication.
    DCEP_DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED =
        (BYTE) 0x82,                        //!< The Data Channel provides a partial reliable unordered bi-directional communication.
    DCEP_DATA_CHANNEL_REXMIT = (BYTE) 0x01, //!< The Data Channel provides a partially-reliable in-order bi-directional communication.
    DCEP_DATA_CHANNEL_TIMED = (BYTE) 0x02   //!< The Data Channel provides a partial reliable in-order bi-directional communication.
} DATA_CHANNEL_TYPE;

// Callback that is fired when SCTP Association wishes to send packet
typedef VOID (*SctpSessionOutboundPacketFunc)(UINT64, PBYTE, UINT32);

// Callback that is fired when SCTP has a new DataChannel
// Argument is ChannelID and ChannelName + Len
typedef VOID (*SctpSessionDataChannelOpenFunc)(UINT64, UINT32, PBYTE, UINT32);

// Callback that is fired when SCTP has a DataChannel Message.
// Argument is ChannelID and Message + Len
typedef VOID (*SctpSessionDataChannelMessageFunc)(UINT64, UINT32, BOOL, PBYTE, UINT32);

typedef struct __SctpSessionCallbacks {
    UINT64 customData;
    SctpSessionOutboundPacketFunc outboundPacketFunc; //!< the outbound callback.
    SctpSessionDataChannelOpenFunc dataChannelOpenFunc;
    SctpSessionDataChannelMessageFunc dataChannelMessageFunc;
} SctpSessionCallbacks, *PSctpSessionCallbacks;

typedef struct __SctpSession {
    volatile SIZE_T shutdownStatus;
    struct socket* socket;
    struct sctp_sendv_spa spa;
    BYTE packet[SCTP_MAX_ALLOWABLE_PACKET_LENGTH];
    UINT32 packetSize;
    SctpSessionCallbacks sctpSessionCallbacks;
} SctpSession, *PSctpSession;

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
/**
 * @brief initialize the sctp context.
 *
 * @return STATUS status of execution
 */
STATUS sctp_session_init(VOID);
/**
 * @brief destroy the sctp context.
 */
VOID sctp_session_deinit(VOID);
/**
 * @brief create the context of sctp session.
 *
 * @param[in] pSctpSessionCallbacks the sctp callback.
 * @param[in, out] ppSctpSession return the context of the sctp session.
 *
 * @return STATUS status of execution
 */
STATUS sctp_session_create(PSctpSessionCallbacks pSctpSessionCallbacks, PSctpSession* ppSctpSession);
/**
 * @brief free the context of sctp session.
 *
 * @param[in, out] ppSctpSession the context of the sctp session.
 *
 * @return STATUS status of execution
 */
STATUS sctp_session_free(PSctpSession* ppSctpSession);
/**
 * @brief put the inbound packet into usrsctp.
 *
 * @param[in] pSctpSession the context of sctp session.
 * @param[in] buf the address of the buffer.
 * @param[in] bufLen the length of the buffer.
 *
 * @return STATUS status of execution
 */
STATUS sctp_session_putInboundPacket(PSctpSession pSctpSession, PBYTE buf, UINT32 bufLen);
/**
 * @brief send the sctp messages.
 * https://tools.ietf.org/html/rfc6458#page-35
 *
 * @param[in] pSctpSession the context of sctp session.
 * @param[in] streamId This value holds the stream number to which the application wishes to send this message.
 * @param[in] isBinary binary message or string message.
 * @param[in] pMessage the address of message.
 * @param[in] pMessageLen the length of message.
 *
 * @return STATUS status of execution
 */
STATUS sctp_session_sendMsg(PSctpSession pSctpSession, UINT32 streamId, BOOL isBinary, PBYTE pMessage, UINT32 pMessageLen);

// https://tools.ietf.org/html/draft-ietf-rtcweb-data-protocol-09#section-5.1
//      0                   1                   2                   3
//      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//     |  Message Type |  Channel Type |            Priority           |
//     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//     |                    Reliability Parameter                      |
//     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//     |         Label Length          |       Protocol Length         |
//     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//     |                                                               |
//     |                             Label                             |
//     |                                                               |
//     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//     |                                                               |
//     |                            Protocol                           |
//     |                                                               |
//     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
/**
 * @brief send the sctp dcep packet.
 *
 * @param[in] pSctpSession the context of sctp session.
 * @param[in] streamId This value holds the stream number to which the application wishes to send this message.
 * @param[in] pChannelName
 * @param[in] pChannelNameLen
 * @param[in] pRtcDataChannelInit
 *
 * @return STATUS status of execution
 */
STATUS sctp_session_sendDcepMsg(PSctpSession pSctpSession, UINT32 streamId, PCHAR pChannelName, UINT32 pChannelNameLen,
                                PRtcDataChannelInit pRtcDataChannelInit);

#ifdef __cplusplus
}
#endif
#endif
#endif //__KINESIS_VIDEO_WEBRTC_CLIENT_SCTP_SCTP__
