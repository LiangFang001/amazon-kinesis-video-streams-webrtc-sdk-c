/*******************************************
HostInfo internal include file
*******************************************/
#ifndef __KINESIS_VIDEO_WEBRTC_CLIENT_NETWORK__
#define __KINESIS_VIDEO_WEBRTC_CLIENT_NETWORK__

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "kvs/common_defs.h"
#include "kvs/WebRTCClient.h"
#include "endianness.h"

#define MAX_LOCAL_NETWORK_INTERFACE_COUNT 128

// string buffer size for ipv4 and ipv6. Null terminator included.
// for ipv6: 0000:0000:0000:0000:0000:0000:0000:0000 = 39
// for ipv4 mapped ipv6: 0000:0000:0000:0000:0000:ffff:192.168.100.228 = 45
#define KVS_IP_ADDRESS_STRING_BUFFER_LEN 46

// 000.000.000.000
#define KVS_MAX_IPV4_ADDRESS_STRING_LEN 15

#define KVS_GET_IP_ADDRESS_PORT(a) ((UINT16) getInt16((a)->port))

#if defined(__MACH__)
#define NO_SIGNAL SO_NOSIGPIPE
#else
#define NO_SIGNAL MSG_NOSIGNAL
#endif

// Some systems such as Windows doesn't have this value
#ifndef EAI_SYSTEM
#define EAI_SYSTEM -11
#endif

// Byte sizes of the IP addresses
#define IPV6_ADDRESS_LENGTH (UINT16) 16
#define IPV4_ADDRESS_LENGTH (UINT16) 4

#define CERTIFICATE_FINGERPRINT_LENGTH 160

#define MAX_UDP_PACKET_SIZE 65507

#define IS_IPV4_ADDR(pAddress) ((pAddress)->family == KVS_IP_FAMILY_TYPE_IPV4)

typedef enum {
    KVS_SOCKET_PROTOCOL_NONE,
    KVS_SOCKET_PROTOCOL_TCP,
    KVS_SOCKET_PROTOCOL_UDP,
} KVS_SOCKET_PROTOCOL;

typedef enum {
    KVS_IP_FAMILY_TYPE_IPV4 = (UINT16) 0x0001,
    KVS_IP_FAMILY_TYPE_IPV6 = (UINT16) 0x0002,
} KVS_IP_FAMILY_TYPE;

typedef struct {
    UINT16 family;
    UINT16 port;                       // port is stored in network byte order
    BYTE address[IPV6_ADDRESS_LENGTH]; // address is stored in network byte order
    BOOL isPointToPoint;
} KvsIpAddress, *PKvsIpAddress;

/**
 * @brief
 *
 * @param[in, out] destIpList array for net_getLocalhostIpAddresses to store any local ips it found. The ip address and port will be in network byte
 * order.
 * @param[in, out] pDestIpListLen length of the array, upon return it will be updated to the actual number of ips in the array
 * @param[in] filter set to custom interface filter callback
 * @param[in] customData Set to custom data that can be used in the callback later
 *
 * @return STATUS status of execution.
 */
STATUS net_getLocalhostIpAddresses(PKvsIpAddress destIpList, PUINT32 pDestIpListLen, IceSetInterfaceFilterFunc filter, UINT64 customData);

/**
 * @param[in] familyType Family for the socket. Must be one of KVS_IP_FAMILY_TYPE
 * @param[in] protocol either tcp or udp
 * @param[in] sendBufSize send buffer size in bytes
 * @param[in] pOutSockFd for the socketfd
 *
 * @return - STATUS status of execution
 */
STATUS net_createSocket(KVS_IP_FAMILY_TYPE familyType, KVS_SOCKET_PROTOCOL protocol, UINT32 sendBufSize, PINT32 pOutSockFd);

/**
 * @param - INT32 - IN - INT32 for the socketfd
 *
 * @return - STATUS status of execution
 */
STATUS net_closeSocket(INT32);

/**
 * @param - PKvsIpAddress - IN - address for the socket to bind. PKvsIpAddress->port will be changed to the actual port number
 * @param - INT32 - IN - valid socket fd
 *
 * @return - STATUS status of execution
 */
STATUS net_bindSocket(PKvsIpAddress, INT32);

/**
 * @param - PKvsIpAddress - IN - address for the socket to connect.
 * @param - INT32 - IN - valid socket fd
 *
 * @return - STATUS status of execution
 */
STATUS net_connectSocket(PKvsIpAddress, INT32);

/**
 * @param - PCHAR - IN - hostname to resolve
 *
 * @param - PKvsIpAddress - OUT - resolved ip address
 *
 * @return - STATUS status of execution
 */
STATUS net_getIpByHostName(PCHAR, PKvsIpAddress);

STATUS net_getIpAddrStr(PKvsIpAddress, PCHAR, UINT32);

BOOL net_compareIpAddress(PKvsIpAddress, PKvsIpAddress, BOOL);

/**
 * @return - INT32 error code
 */
INT32 net_getErrorCode(VOID);

/**
 * @param - INT32 - IN - error code
 *
 * @return - PCHAR string associated with error code
 */
PCHAR net_getErrorString(INT32);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_CLIENT_NETWORK__ */
