/*******************************************
Main internal include file
*******************************************/
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_INCLUDE_I__
#define __KINESIS_VIDEO_WEBRTC_CLIENT_INCLUDE_I__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

////////////////////////////////////////////////////
// Project include files
////////////////////////////////////////////////////
#include <kvs/WebRTCClient.h>

// INET/INET6 MUST be defined before usrsctp
// If removed will cause corruption that is hard to determine at runtime
#define INET  1
//#define INET6 1

#define IS_IPV4_ADDR(pAddress) ((pAddress)->family == KVS_IP_FAMILY_TYPE_IPV4)

////////////////////////////////////////////////////
// Project forward declarations
////////////////////////////////////////////////////
struct __TurnConnection;

#define KVS_CONVERT_TIMESCALE(pts, from_timescale, to_timescale) (pts * to_timescale / from_timescale)

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_CLIENT_INCLUDE_I__ */
