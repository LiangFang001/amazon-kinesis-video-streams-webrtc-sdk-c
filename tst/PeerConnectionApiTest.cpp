#include "WebRTCClientTestFixture.h"

namespace com {
namespace amazonaws {
namespace kinesis {
namespace video {
namespace webrtcclient {

class PeerConnectionApiTest : public WebRtcClientTestBase {
};

TEST_F(PeerConnectionApiTest, sdp_deserializeRtcIceCandidateInit)
{
    RtcIceCandidateInit rtcIceCandidateInit;

    MEMSET(&rtcIceCandidateInit, 0x00, SIZEOF(rtcIceCandidateInit));

    auto notAnObject = "helloWorld";
    EXPECT_EQ(sdp_deserializeRtcIceCandidateInit((PCHAR) notAnObject, STRLEN(notAnObject), &rtcIceCandidateInit), STATUS_JSON_API_CALL_INVALID_RETURN);

    auto emptyObject = "{}";
    EXPECT_EQ(sdp_deserializeRtcIceCandidateInit((PCHAR) emptyObject, STRLEN(emptyObject), &rtcIceCandidateInit), STATUS_JSON_API_CALL_INVALID_RETURN);

    auto noCandidate = "{randomKey: \"randomValue\"}";
    EXPECT_EQ(sdp_deserializeRtcIceCandidateInit((PCHAR) noCandidate, STRLEN(noCandidate), &rtcIceCandidateInit), STATUS_SDP_ICE_CANDIDATE_MISSING_CANDIDATE);

    auto keyNoValue = "{1,2,3,4,5}candidate";
    EXPECT_EQ(sdp_deserializeRtcIceCandidateInit((PCHAR) keyNoValue, STRLEN(keyNoValue), &rtcIceCandidateInit), STATUS_SDP_ICE_CANDIDATE_MISSING_CANDIDATE);

    auto validCandidate = "{candidate: \"foobar\"}";
    EXPECT_EQ(sdp_deserializeRtcIceCandidateInit((PCHAR) validCandidate, STRLEN(validCandidate), &rtcIceCandidateInit), STATUS_SUCCESS);
    EXPECT_STREQ(rtcIceCandidateInit.candidate, "foobar");

    auto validCandidate2 = "{candidate: \"candidate: 1 2 3\", \"sdpMid\": 0}";
    EXPECT_EQ(sdp_deserializeRtcIceCandidateInit((PCHAR) validCandidate2, STRLEN(validCandidate2), &rtcIceCandidateInit), STATUS_SUCCESS);
    EXPECT_STREQ(rtcIceCandidateInit.candidate, "candidate: 1 2 3");
}

TEST_F(PeerConnectionApiTest, sdp_serializeInit)
{
    RtcSessionDescriptionInit rtcSessionDescriptionInit;
    UINT32 sessionDescriptionJSONLen = 0;
    CHAR sessionDescriptionJSON[500] = {0};

    MEMSET(&rtcSessionDescriptionInit, 0x00, SIZEOF(RtcSessionDescriptionInit));

    EXPECT_EQ(sdp_serializeInit(NULL, sessionDescriptionJSON, &sessionDescriptionJSONLen), STATUS_NULL_ARG);
    EXPECT_EQ(sdp_serializeInit(&rtcSessionDescriptionInit, sessionDescriptionJSON, NULL), STATUS_NULL_ARG);

    STRCPY(rtcSessionDescriptionInit.sdp, "KVS\nWebRTC\nSDP\nValue\n");
    rtcSessionDescriptionInit.type = SDP_TYPE_OFFER;
    sessionDescriptionJSONLen = 500;

    EXPECT_EQ(sdp_serializeInit(&rtcSessionDescriptionInit, sessionDescriptionJSON, &sessionDescriptionJSONLen), STATUS_SUCCESS);
    EXPECT_STREQ(sessionDescriptionJSON, "{\"type\": \"offer\", \"sdp\": \"KVS\\r\\nWebRTC\\r\\nSDP\\r\\nValue\\r\\n\"}");
}

TEST_F(PeerConnectionApiTest, suppliedCertificatesVariation)
{
    RtcConfiguration configuration;
    PRtcPeerConnection pRtcPeerConnection;

    MEMSET(&configuration, 0x00, SIZEOF(RtcConfiguration));
    configuration.iceTransportPolicy = ICE_TRANSPORT_POLICY_RELAY;

    // Private key is null but the size is not zero
    configuration.certificates[0].pCertificate = (PBYTE) 1;
    configuration.certificates[0].certificateSize = 0;
    configuration.certificates[0].pPrivateKey = NULL;
    configuration.certificates[0].privateKeySize = 1;
    EXPECT_EQ(STATUS_DTLS_INVALID_CERTIFICATE_BITS, pc_create(&configuration, &pRtcPeerConnection));

    // Private key is null but the size is not zero with specified size for the cert
    configuration.certificates[0].pCertificate = (PBYTE) 1;
    configuration.certificates[0].certificateSize = 100;
    configuration.certificates[0].pPrivateKey = NULL;
    configuration.certificates[0].privateKeySize = 1;
    EXPECT_EQ(STATUS_DTLS_INVALID_CERTIFICATE_BITS, pc_create(&configuration, &pRtcPeerConnection));

    // Bad private key size later in the chain that should be ignored
    configuration.certificates[0].pCertificate = NULL;
    configuration.certificates[0].certificateSize = 0;
    configuration.certificates[0].pPrivateKey = NULL;
    configuration.certificates[0].privateKeySize = 1;
    EXPECT_EQ(STATUS_SUCCESS, pc_create(&configuration, &pRtcPeerConnection));
    EXPECT_EQ(STATUS_SUCCESS, pc_free(&pRtcPeerConnection));

    // Bad private key size later in the chain with cert size not zero that should be ignored
    configuration.certificates[0].pCertificate = NULL;
    configuration.certificates[0].certificateSize = 100;
    configuration.certificates[0].pPrivateKey = NULL;
    configuration.certificates[0].privateKeySize = 1;
    EXPECT_EQ(STATUS_SUCCESS, pc_create(&configuration, &pRtcPeerConnection));
    EXPECT_EQ(STATUS_SUCCESS, pc_free(&pRtcPeerConnection));
}

TEST_F(PeerConnectionApiTest, sdp_deserializeInit)
{
    RtcSessionDescriptionInit rtcSessionDescriptionInit;
    MEMSET(&rtcSessionDescriptionInit, 0x00, SIZEOF(RtcSessionDescriptionInit));

    auto notAnObject = "helloWorld";
    EXPECT_EQ(sdp_deserializeInit((PCHAR) notAnObject, STRLEN(notAnObject), &rtcSessionDescriptionInit),
              STATUS_JSON_API_CALL_INVALID_RETURN);

    auto emptyObject = "{}";
    EXPECT_EQ(sdp_deserializeInit((PCHAR) emptyObject, STRLEN(emptyObject), &rtcSessionDescriptionInit),
              STATUS_JSON_API_CALL_INVALID_RETURN);

    auto noSDPKey = "{type: \"offer\"}";
    EXPECT_EQ(sdp_deserializeInit((PCHAR) noSDPKey, STRLEN(noSDPKey), &rtcSessionDescriptionInit),
              STATUS_SDP_INIT_MISSING_SDP);

    auto noTypeKey = "{\"sdp\": \"KVS\\r\\nWebRTC\\r\\nSDP\\r\\nValue\\r\\n\"}";
    EXPECT_EQ(sdp_deserializeInit((PCHAR) noTypeKey, STRLEN(noTypeKey), &rtcSessionDescriptionInit),
              STATUS_SDP_INIT_MISSING_TYPE);

    auto invalidTypeKey = "{sdp: \"kvsSdp\", type: \"foobar\"}";
    EXPECT_EQ(sdp_deserializeInit((PCHAR) invalidTypeKey, STRLEN(invalidTypeKey), &rtcSessionDescriptionInit),
              STATUS_SDP_INIT_INVALID_TYPE);

    auto keyNoValue = "{1,2,3,4,5}sdp";
    EXPECT_EQ(sdp_deserializeInit((PCHAR) keyNoValue, STRLEN(keyNoValue), &rtcSessionDescriptionInit),
              STATUS_SDP_INIT_MISSING_SDP);

    auto validSessionDescriptionInit = "{sdp: \"KVS\\r\\nWebRTC\\r\\nSDP\\r\\nValue\\r\\n\", type: \"offer\"}";
    EXPECT_EQ(sdp_deserializeInit((PCHAR) validSessionDescriptionInit, STRLEN(validSessionDescriptionInit), &rtcSessionDescriptionInit),
              STATUS_SUCCESS);
    EXPECT_STREQ(rtcSessionDescriptionInit.sdp, "KVS\r\nWebRTC\r\nSDP\r\nValue\r\n");
    EXPECT_EQ(rtcSessionDescriptionInit.type, SDP_TYPE_OFFER);
}

TEST_F(PeerConnectionApiTest, sdp_fmtpForPayloadType)
{
    auto rawSessionDescription = R"(v=0
o=- 686950092 1576880200 IN IP4 0.0.0.0
s=-
t=0 0
m=audio 9 UDP/TLS/RTP/SAVPF 109
a=rtpmap:109 opus/48000/2
a=fmtp:109 minptime=10;useinbandfec=1
m=video 9 UDP/TLS/RTP/SAVPF 97
a=rtpmap:97 H264/90000
a=fmtp:97 profile-level-id=42e01f;level-asymmetry-allowed=1
)";

    SessionDescription sessionDescription;
    MEMSET(&sessionDescription, 0x00, SIZEOF(SessionDescription));
    EXPECT_EQ(deserializeSessionDescription(&sessionDescription, (PCHAR) rawSessionDescription), STATUS_SUCCESS);

    EXPECT_STREQ(sdp_fmtpForPayloadType(97, &sessionDescription), "profile-level-id=42e01f;level-asymmetry-allowed=1");
    EXPECT_STREQ(sdp_fmtpForPayloadType(109, &sessionDescription), "minptime=10;useinbandfec=1");
    EXPECT_STREQ(sdp_fmtpForPayloadType(25, &sessionDescription), NULL);
}

} // namespace webrtcclient
} // namespace video
} // namespace kinesis
} // namespace amazonaws
} // namespace com
