/*******************************************
RTCP Rolling Buffer include file
*******************************************/
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_RTCP_RTP_ROLLING_BUFFER_H
#define __KINESIS_VIDEO_WEBRTC_CLIENT_RTCP_RTP_ROLLING_BUFFER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "RtpPacket.h"
#include "RollingBuffer.h"

typedef struct {
    PRollingBuffer pRollingBuffer;
    // index of last rtp packet in rolling buffer
    UINT64 lastIndex;
} RtpRollingBuffer, *PRtpRollingBuffer;

STATUS rtp_rolling_buffer_create(UINT32, PRtpRollingBuffer*);
STATUS rtp_rolling_buffer_free(PRtpRollingBuffer*);
STATUS rtp_rolling_buffer_freeData(PUINT64);
STATUS rtp_rolling_buffer_addRtpPacket(PRtpRollingBuffer, PRtpPacket);
STATUS rtp_rolling_buffer_getValidSeqIndexList(PRtpRollingBuffer, PUINT16, UINT32, PUINT64, PUINT32);

#ifdef __cplusplus
}
#endif
#endif //__KINESIS_VIDEO_WEBRTC_CLIENT_RTCP_RTP_ROLLING_BUFFER_H
