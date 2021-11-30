#ifndef __KINESIS_VIDEO_WEBRTC_WSS_API_H__
#define __KINESIS_VIDEO_WEBRTC_WSS_API_H__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "signaling.h"

// Specifies whether to block on the correlation id
#define BLOCK_ON_CORRELATION_ID FALSE
// Max length of the signaling message type string length
#define MAX_SIGNALING_MESSAGE_TYPE_LEN ARRAY_SIZE(SIGNALING_RECONNECT_ICE_SERVER)
#define LWS_MESSAGE_BUFFER_SIZE        (SIZEOF(CHAR) * (MAX_SIGNALING_MESSAGE_LEN))

typedef struct {
    // The first member is the public signaling message structure
    ReceivedSignalingMessage receivedSignalingMessage;

    // The messaging client object
    PSignalingClient pSignalingClient;
} SignalingMessageWrapper, *PSignalingMessageWrapper;

/**
 * @brief   It is a non-blocking call, and it spin off one thread to handle the reception.
 *
 * @param[in]
 * @param[in]
 *
 * @return
 */
STATUS wssConnectSignalingChannel(PSignalingClient pSignalingClient, UINT64 time);
STATUS wssSendMessage(PSignalingClient pSignalingClient, PCHAR pMessageType, PCHAR peerClientId, PCHAR pMessage, UINT32 messageLen,
                      PCHAR pCorrelationId, UINT32 correlationIdLen);
STATUS wssHandleDataMsg(PSignalingClient pSignalingClient, PCHAR pMessage, UINT32 messageLen);
STATUS wssHandleCtrlMsg(PSignalingClient pSignalingClient, UINT8 opcode, PCHAR pMessage, UINT32 messageLen);
// #YC_TBD.
STATUS wss_api_terminateConnection(PSignalingClient pSignalingClient, SERVICE_CALL_RESULT callResult);
STATUS wss_api_getMessageTypeFromString(PCHAR typeStr, UINT32 typeLen, SIGNALING_MESSAGE_TYPE* pMessageType);
// json parser.
STATUS wss_api_rsp_receivedMessage(const CHAR* pResponseStr, UINT32 resultLen, PSignalingMessageWrapper pSignalingMessageWrapper);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_WSS_API_H__ */