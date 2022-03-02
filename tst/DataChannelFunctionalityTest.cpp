#include "WebRTCClientTestFixture.h"

namespace com {
namespace amazonaws {
namespace kinesis {
namespace video {
namespace webrtcclient {

class DataChannelFunctionalityTest : public WebRtcClientTestBase {
};

// Macro so we don't have to deal with scope capture
#define TEST_DATA_CHANNEL_MESSAGE "This is my test message"

struct RemoteOpen {
    std::mutex lock{};
    std::map<std::string, uint64_t> channels{};
};

// Create two PeerConnections and ensure DataChannels that were declared
// before signaling go to connected
TEST_F(DataChannelFunctionalityTest, createDataChannel_Disconnected)
{
    RtcConfiguration configuration;
    PRtcPeerConnection offerPc = NULL, answerPc = NULL;
    PRtcDataChannel pOfferDataChannel = nullptr, pAnswerDataChannel = nullptr;
    SIZE_T datachannelLocalOpenCount = 0, msgCount = 0;
    RemoteOpen remoteOpen{};

    MEMSET(&configuration, 0x00, SIZEOF(RtcConfiguration));

    EXPECT_EQ(peer_connection_create(&configuration, &offerPc), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_create(&configuration, &answerPc), STATUS_SUCCESS);

    auto onDataChannel = [](UINT64 customData, PRtcDataChannel pRtcDataChannel) {
        auto remoteOpen = reinterpret_cast<RemoteOpen*>(customData);
        DLOGD("onDataChannel '%s'", pRtcDataChannel->name);
        std::string name(pRtcDataChannel->name);
        {
            std::lock_guard<std::mutex> lock(remoteOpen->lock);
            if (remoteOpen->channels.count(name) == 0) {
                remoteOpen->channels.emplace(name, 1u);
            } else {
                auto count = remoteOpen->channels.at(name);
                remoteOpen->channels.erase(name);
                remoteOpen->channels.emplace(name, count + 1);
            }
        }
        data_channel_send(pRtcDataChannel, FALSE, (PBYTE) TEST_DATA_CHANNEL_MESSAGE, STRLEN(TEST_DATA_CHANNEL_MESSAGE));
    };

    auto dataChannelOnOpenCallback = [](UINT64 customData, PRtcDataChannel pDataChannel) {
        UNUSED_PARAM(pDataChannel);
        ATOMIC_INCREMENT((PSIZE_T) customData);
    };

    auto dataChannelOnMessageCallback = [](UINT64 customData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMsg, UINT32 pMsgLen) {
        UNUSED_PARAM(pDataChannel);
        UNUSED_PARAM(isBinary);
        if (STRNCMP((PCHAR) pMsg, TEST_DATA_CHANNEL_MESSAGE, pMsgLen) == 0) {
            ATOMIC_INCREMENT((PSIZE_T) customData);
        }
    };

    EXPECT_EQ(peer_connection_onDataChannel(offerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_onDataChannel(answerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);

    // Create two DataChannels
    EXPECT_EQ(data_channel_create(offerPc, (PCHAR) "Offer PeerConnection", nullptr, &pOfferDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_create(answerPc, (PCHAR) "Answer PeerConnection", nullptr, &pAnswerDataChannel), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onOpen(pOfferDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onOpen(pAnswerDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onMessage(pOfferDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onMessage(pAnswerDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);

    EXPECT_EQ(connectTwoPeers(offerPc, answerPc), TRUE);

    // Busy wait until DataChannels connect and send a message
    for (auto i = 0; i <= 100 && (ATOMIC_LOAD(&datachannelLocalOpenCount) + ATOMIC_LOAD(&msgCount)) != 4; i++) {
        THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_SECOND);
    }

    peer_connection_close(offerPc);
    peer_connection_close(answerPc);
    peer_connection_free(&offerPc);
    peer_connection_free(&answerPc);

    ASSERT_EQ(ATOMIC_LOAD(&datachannelLocalOpenCount), 2);
    ASSERT_EQ(ATOMIC_LOAD(&msgCount), 2);
    ASSERT_EQ(2, remoteOpen.channels.size());
    ASSERT_EQ(1, remoteOpen.channels.count("Offer PeerConnection"));
    ASSERT_EQ(1, remoteOpen.channels.count("Answer PeerConnection"));
    ASSERT_EQ(1u, remoteOpen.channels.at("Offer PeerConnection"));
    ASSERT_EQ(1u, remoteOpen.channels.at("Answer PeerConnection"));
}

TEST_F(DataChannelFunctionalityTest, dataChannelSendRecvMessageAfterDtlsCompleted)
{
    RtcConfiguration configuration;
    PRtcPeerConnection offerPc = NULL, answerPc = NULL;
    PRtcDataChannel pOfferDataChannel = nullptr, pAnswerDataChannel = nullptr;
    SIZE_T pOfferRemoteDataChannel = 0, pAnswerRemoteDataChannel = 0;
    SIZE_T datachannelLocalOpenCount = 0, msgCount = 0;
    BOOL dtlsCompleted = FALSE;

    MEMSET(&configuration, 0x00, SIZEOF(RtcConfiguration));

    auto onDataChannel = [](UINT64 customData, PRtcDataChannel pRtcDataChannel) { ATOMIC_STORE((PSIZE_T) customData, (SIZE_T) pRtcDataChannel); };

    auto dataChannelOnOpenCallback = [](UINT64 customData, PRtcDataChannel pDataChannel) {
        UNUSED_PARAM(pDataChannel);
        ATOMIC_INCREMENT((PSIZE_T) customData);
    };

    auto dataChannelOnMessageCallback = [](UINT64 customData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMsg, UINT32 pMsgLen) {
        UNUSED_PARAM(pDataChannel);
        UNUSED_PARAM(isBinary);
        if (STRNCMP((PCHAR) pMsg, TEST_DATA_CHANNEL_MESSAGE, pMsgLen) == 0) {
            ATOMIC_INCREMENT((PSIZE_T) customData);
        }
    };

    EXPECT_EQ(peer_connection_create(&configuration, &offerPc), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_create(&configuration, &answerPc), STATUS_SUCCESS);

    EXPECT_EQ(peer_connection_onDataChannel(offerPc, (UINT64) &pOfferRemoteDataChannel, onDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_onDataChannel(answerPc, (UINT64) &pAnswerRemoteDataChannel, onDataChannel), STATUS_SUCCESS);

    // Create two DataChannels
    EXPECT_EQ(data_channel_create(offerPc, (PCHAR) "Offer PeerConnection", nullptr, &pOfferDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_create(answerPc, (PCHAR) "Answer PeerConnection", nullptr, &pAnswerDataChannel), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onOpen(pOfferDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onOpen(pAnswerDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onMessage(pOfferDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onMessage(pAnswerDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);

    EXPECT_EQ(connectTwoPeers(offerPc, answerPc), TRUE);

    // Busy wait until remote channel open and dtls completed
    for (auto i = 0; i <= 100 &&
         (dtls_session_isInitFinished(((PKvsPeerConnection) offerPc)->pDtlsSession, &dtlsCompleted) || ATOMIC_LOAD(&pOfferRemoteDataChannel) == 0);
         i++) {
        THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_SECOND);
    }

    EXPECT_EQ(dtlsCompleted, TRUE);
    EXPECT_TRUE(ATOMIC_LOAD(&pOfferRemoteDataChannel) != 0);

    EXPECT_EQ(data_channel_send((PRtcDataChannel) ATOMIC_LOAD(&pOfferRemoteDataChannel), FALSE, (PBYTE) TEST_DATA_CHANNEL_MESSAGE,
                              STRLEN(TEST_DATA_CHANNEL_MESSAGE)),
              STATUS_SUCCESS);

    /* wait until the channel message is received */
    for (auto i = 0; i <= 5 && ATOMIC_LOAD(&msgCount) == 0; i++) {
        THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_SECOND);
    }
    EXPECT_EQ(ATOMIC_LOAD(&msgCount), 1);

    peer_connection_close(offerPc);
    peer_connection_close(answerPc);
    peer_connection_free(&offerPc);
    peer_connection_free(&answerPc);
}

TEST_F(DataChannelFunctionalityTest, createDataChannel_PartialReliabilityUnorderedMaxPacketLifeTimeParameterSet)
{
    RtcConfiguration configuration;
    PRtcPeerConnection offerPc = NULL, answerPc = NULL;
    PRtcDataChannel pOfferDataChannel = nullptr, pAnswerDataChannel = nullptr;
    SIZE_T datachannelLocalOpenCount = 0, msgCount = 0;
    RtcDataChannelInit rtcDataChannelInit;
    PSctpSession pSctpSession = NULL;
    PKvsDataChannel pKvsDataChannel = NULL;
    RemoteOpen remoteOpen{};

    MEMSET(&configuration, 0x00, SIZEOF(RtcConfiguration));

    EXPECT_EQ(peer_connection_create(&configuration, &offerPc), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_create(&configuration, &answerPc), STATUS_SUCCESS);

    // Set partial reliability parameters
    NULLABLE_SET_VALUE(rtcDataChannelInit.maxPacketLifeTime, 1234);
    NULLABLE_SET_EMPTY(rtcDataChannelInit.maxRetransmits);
    rtcDataChannelInit.ordered = FALSE;

    auto onDataChannel = [](UINT64 customData, PRtcDataChannel pRtcDataChannel) {
      auto remoteOpen = reinterpret_cast<RemoteOpen*>(customData);
      DLOGD("onDataChannel '%s'", pRtcDataChannel->name);
      std::string name(pRtcDataChannel->name);
      {
          std::lock_guard<std::mutex> lock(remoteOpen->lock);
          if (remoteOpen->channels.count(name) == 0) {
              remoteOpen->channels.emplace(name, 1u);
          } else {
              auto count = remoteOpen->channels.at(name);
              remoteOpen->channels.erase(name);
              remoteOpen->channels.emplace(name, count + 1);
          }
      }
      data_channel_send(pRtcDataChannel, FALSE, (PBYTE) TEST_DATA_CHANNEL_MESSAGE, STRLEN(TEST_DATA_CHANNEL_MESSAGE));
    };

    auto dataChannelOnOpenCallback = [](UINT64 customData, PRtcDataChannel pDataChannel) {
        UNUSED_PARAM(pDataChannel);
        ATOMIC_INCREMENT((PSIZE_T) customData);
    };

    auto dataChannelOnMessageCallback = [](UINT64 customData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMsg, UINT32 pMsgLen) {
        UNUSED_PARAM(pDataChannel);
        UNUSED_PARAM(isBinary);
        if (STRNCMP((PCHAR) pMsg, TEST_DATA_CHANNEL_MESSAGE, pMsgLen) == 0) {
          ATOMIC_INCREMENT((PSIZE_T) customData);
        }
    };

    EXPECT_EQ(peer_connection_onDataChannel(offerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_onDataChannel(answerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);

    // Create two DataChannels
    EXPECT_EQ(data_channel_create(offerPc, (PCHAR) "Offer PeerConnection", &rtcDataChannelInit, &pOfferDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_create(answerPc, (PCHAR) "Answer PeerConnection", NULL, &pAnswerDataChannel), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onOpen(pOfferDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onOpen(pAnswerDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onMessage(pOfferDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onMessage(pAnswerDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);

    EXPECT_EQ(connectTwoPeers(offerPc, answerPc), TRUE);

    // Busy wait until DataChannels connect and send a message
    for (auto i = 0; i <= 100 && (ATOMIC_LOAD(&datachannelLocalOpenCount) + ATOMIC_LOAD(&msgCount)) != 4 ; i++) {
        THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_SECOND);
    }
    // Close the connection to avoid data race while accessing SctpSession
    peer_connection_close(offerPc);
    peer_connection_close(answerPc);

    pKvsDataChannel = (PKvsDataChannel) pOfferDataChannel;
    pSctpSession = ((PKvsPeerConnection) pKvsDataChannel->pRtcPeerConnection)->pSctpSession;

    ASSERT_EQ(pSctpSession->spa.sendv_sndinfo.snd_flags, SCTP_UNORDERED);
    ASSERT_EQ(pSctpSession->spa.sendv_prinfo.pr_policy, SCTP_PR_SCTP_TTL);
    ASSERT_EQ(pSctpSession->spa.sendv_prinfo.pr_value, rtcDataChannelInit.maxPacketLifeTime.value);

    peer_connection_free(&offerPc);
    peer_connection_free(&answerPc);
}

TEST_F(DataChannelFunctionalityTest, createDataChannel_PartialReliabilityUnOrderedMaxRetransmitsParameterSet)
{
    RtcConfiguration configuration;
    PRtcPeerConnection offerPc = NULL, answerPc = NULL;
    PRtcDataChannel pOfferDataChannel = nullptr, pAnswerDataChannel = nullptr;
    SIZE_T datachannelLocalOpenCount = 0, msgCount = 0;
    RtcDataChannelInit rtcDataChannelInit;
    PSctpSession pSctpSession = NULL;
    PKvsDataChannel pKvsDataChannel = NULL;
    RemoteOpen remoteOpen{};

    MEMSET(&configuration, 0x00, SIZEOF(RtcConfiguration));

    EXPECT_EQ(peer_connection_create(&configuration, &offerPc), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_create(&configuration, &answerPc), STATUS_SUCCESS);

    // Set partial reliability parameters
    NULLABLE_SET_VALUE(rtcDataChannelInit.maxRetransmits, 5);
    NULLABLE_SET_EMPTY(rtcDataChannelInit.maxPacketLifeTime);
    rtcDataChannelInit.ordered = FALSE;

    auto onDataChannel = [](UINT64 customData, PRtcDataChannel pRtcDataChannel) {
      auto remoteOpen = reinterpret_cast<RemoteOpen*>(customData);
      DLOGD("onDataChannel '%s'", pRtcDataChannel->name);
      std::string name(pRtcDataChannel->name);
      {
          std::lock_guard<std::mutex> lock(remoteOpen->lock);
          if (remoteOpen->channels.count(name) == 0) {
              remoteOpen->channels.emplace(name, 1u);
          } else {
              auto count = remoteOpen->channels.at(name);
              remoteOpen->channels.erase(name);
              remoteOpen->channels.emplace(name, count + 1);
          }
      }
      data_channel_send(pRtcDataChannel, FALSE, (PBYTE) TEST_DATA_CHANNEL_MESSAGE, STRLEN(TEST_DATA_CHANNEL_MESSAGE));
    };

    auto dataChannelOnOpenCallback = [](UINT64 customData, PRtcDataChannel pDataChannel) {
        UNUSED_PARAM(pDataChannel);
        ATOMIC_INCREMENT((PSIZE_T) customData);
    };

    auto dataChannelOnMessageCallback = [](UINT64 customData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMsg, UINT32 pMsgLen) {
        UNUSED_PARAM(pDataChannel);
        UNUSED_PARAM(isBinary);
        if (STRNCMP((PCHAR) pMsg, TEST_DATA_CHANNEL_MESSAGE, pMsgLen) == 0) {
          ATOMIC_INCREMENT((PSIZE_T) customData);
        }
    };

    EXPECT_EQ(peer_connection_onDataChannel(offerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_onDataChannel(answerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);

    // Create two DataChannels
    EXPECT_EQ(data_channel_create(offerPc, (PCHAR) "Offer PeerConnection", &rtcDataChannelInit, &pOfferDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_create(answerPc, (PCHAR) "Answer PeerConnection", NULL, &pAnswerDataChannel), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onOpen(pOfferDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onOpen(pAnswerDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onMessage(pOfferDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onMessage(pAnswerDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);

    EXPECT_EQ(connectTwoPeers(offerPc, answerPc), TRUE);

    // Busy wait until DataChannels connect and send a message
    for (auto i = 0; i <= 100 && (ATOMIC_LOAD(&datachannelLocalOpenCount) + ATOMIC_LOAD(&msgCount)) != 4 ; i++) {
        THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_SECOND);
    }

    // Close the connection to avoid data race while accessing SctpSession
    peer_connection_close(offerPc);
    peer_connection_close(answerPc);

    pKvsDataChannel = (PKvsDataChannel) pOfferDataChannel;
    pSctpSession = ((PKvsPeerConnection) pKvsDataChannel->pRtcPeerConnection)->pSctpSession;

    ASSERT_EQ(pSctpSession->spa.sendv_sndinfo.snd_flags, SCTP_UNORDERED);
    ASSERT_EQ(pSctpSession->spa.sendv_prinfo.pr_policy, SCTP_PR_SCTP_RTX);
    ASSERT_EQ(pSctpSession->spa.sendv_prinfo.pr_value, rtcDataChannelInit.maxRetransmits.value);

    peer_connection_free(&offerPc);
    peer_connection_free(&answerPc);
}

TEST_F(DataChannelFunctionalityTest, createDataChannel_PartialReliabilityOrderedMaxPacketLifeTimeParameterSet)
{
    RtcConfiguration configuration;
    PRtcPeerConnection offerPc = NULL, answerPc = NULL;
    PRtcDataChannel pOfferDataChannel = nullptr, pAnswerDataChannel = nullptr;
    SIZE_T datachannelLocalOpenCount = 0, msgCount = 0;
    RtcDataChannelInit rtcDataChannelInit;
    PSctpSession pSctpSession = NULL;
    PKvsDataChannel pKvsDataChannel = NULL;
    RemoteOpen remoteOpen{};

    MEMSET(&configuration, 0x00, SIZEOF(RtcConfiguration));

    EXPECT_EQ(peer_connection_create(&configuration, &offerPc), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_create(&configuration, &answerPc), STATUS_SUCCESS);

    // Set partial reliability parameters
    NULLABLE_SET_VALUE(rtcDataChannelInit.maxPacketLifeTime, 1234);
    NULLABLE_SET_EMPTY(rtcDataChannelInit.maxRetransmits);
    rtcDataChannelInit.ordered = TRUE;

    auto onDataChannel = [](UINT64 customData, PRtcDataChannel pRtcDataChannel) {
      auto remoteOpen = reinterpret_cast<RemoteOpen*>(customData);
      DLOGD("onDataChannel '%s'", pRtcDataChannel->name);
      std::string name(pRtcDataChannel->name);
      {
          std::lock_guard<std::mutex> lock(remoteOpen->lock);
          if (remoteOpen->channels.count(name) == 0) {
              remoteOpen->channels.emplace(name, 1u);
          } else {
              auto count = remoteOpen->channels.at(name);
              remoteOpen->channels.erase(name);
              remoteOpen->channels.emplace(name, count + 1);
          }
      }
      data_channel_send(pRtcDataChannel, FALSE, (PBYTE) TEST_DATA_CHANNEL_MESSAGE, STRLEN(TEST_DATA_CHANNEL_MESSAGE));
    };

    auto dataChannelOnOpenCallback = [](UINT64 customData, PRtcDataChannel pDataChannel) {
        UNUSED_PARAM(pDataChannel);
        ATOMIC_INCREMENT((PSIZE_T) customData);
    };

    auto dataChannelOnMessageCallback = [](UINT64 customData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMsg, UINT32 pMsgLen) {
        UNUSED_PARAM(pDataChannel);
        UNUSED_PARAM(isBinary);
        if (STRNCMP((PCHAR) pMsg, TEST_DATA_CHANNEL_MESSAGE, pMsgLen) == 0) {
          ATOMIC_INCREMENT((PSIZE_T) customData);
        }
    };

    EXPECT_EQ(peer_connection_onDataChannel(offerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_onDataChannel(answerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);

    // Create two DataChannels
    EXPECT_EQ(data_channel_create(offerPc, (PCHAR) "Offer PeerConnection", &rtcDataChannelInit, &pOfferDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_create(answerPc, (PCHAR) "Answer PeerConnection", NULL, &pAnswerDataChannel), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onOpen(pOfferDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onOpen(pAnswerDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onMessage(pOfferDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onMessage(pAnswerDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);

    EXPECT_EQ(connectTwoPeers(offerPc, answerPc), TRUE);

    // Busy wait until DataChannels connect and send a message
    for (auto i = 0; i <= 100 && (ATOMIC_LOAD(&datachannelLocalOpenCount) + ATOMIC_LOAD(&msgCount)) != 4 ; i++) {
        THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_SECOND);
    }

    // Close the connection to avoid data race while accessing SctpSession
    peer_connection_close(offerPc);
    peer_connection_close(answerPc);

    pKvsDataChannel = (PKvsDataChannel) pOfferDataChannel;
    pSctpSession = ((PKvsPeerConnection) pKvsDataChannel->pRtcPeerConnection)->pSctpSession;

    ASSERT_NE(pSctpSession->spa.sendv_sndinfo.snd_flags, SCTP_UNORDERED);
    ASSERT_EQ(pSctpSession->spa.sendv_prinfo.pr_policy, SCTP_PR_SCTP_TTL);
    ASSERT_EQ(pSctpSession->spa.sendv_prinfo.pr_value, rtcDataChannelInit.maxPacketLifeTime.value);

    peer_connection_free(&offerPc);
    peer_connection_free(&answerPc);
}

TEST_F(DataChannelFunctionalityTest, createDataChannel_PartialReliabilityOrderedMaxRetransmitsParameterSet)
{
    RtcConfiguration configuration;
    PRtcPeerConnection offerPc = NULL, answerPc = NULL;
    PRtcDataChannel pOfferDataChannel = nullptr, pAnswerDataChannel = nullptr;
    SIZE_T datachannelLocalOpenCount = 0, msgCount = 0;
    RtcDataChannelInit rtcDataChannelInit;
    PSctpSession pSctpSession = NULL;
    PKvsDataChannel pKvsDataChannel = NULL;
    RemoteOpen remoteOpen{};

    MEMSET(&configuration, 0x00, SIZEOF(RtcConfiguration));

    EXPECT_EQ(peer_connection_create(&configuration, &offerPc), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_create(&configuration, &answerPc), STATUS_SUCCESS);

    // Set partial reliability parameters
    NULLABLE_SET_VALUE(rtcDataChannelInit.maxRetransmits, 5);
    NULLABLE_SET_EMPTY(rtcDataChannelInit.maxPacketLifeTime);
    rtcDataChannelInit.ordered = TRUE;

    auto onDataChannel = [](UINT64 customData, PRtcDataChannel pRtcDataChannel) {
      auto remoteOpen = reinterpret_cast<RemoteOpen*>(customData);
      DLOGD("onDataChannel '%s'", pRtcDataChannel->name);
      std::string name(pRtcDataChannel->name);
      {
          std::lock_guard<std::mutex> lock(remoteOpen->lock);
          if (remoteOpen->channels.count(name) == 0) {
              remoteOpen->channels.emplace(name, 1u);
          } else {
              auto count = remoteOpen->channels.at(name);
              remoteOpen->channels.erase(name);
              remoteOpen->channels.emplace(name, count + 1);
          }
      }
      data_channel_send(pRtcDataChannel, FALSE, (PBYTE) TEST_DATA_CHANNEL_MESSAGE, STRLEN(TEST_DATA_CHANNEL_MESSAGE));
    };

    auto dataChannelOnOpenCallback = [](UINT64 customData, PRtcDataChannel pDataChannel) {
        UNUSED_PARAM(pDataChannel);
        ATOMIC_INCREMENT((PSIZE_T) customData);
    };

    auto dataChannelOnMessageCallback = [](UINT64 customData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMsg, UINT32 pMsgLen) {
        UNUSED_PARAM(pDataChannel);
        UNUSED_PARAM(isBinary);
        if (STRNCMP((PCHAR) pMsg, TEST_DATA_CHANNEL_MESSAGE, pMsgLen) == 0) {
          ATOMIC_INCREMENT((PSIZE_T) customData);
        }
    };

    EXPECT_EQ(peer_connection_onDataChannel(offerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_onDataChannel(answerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);

    // Create two DataChannels
    EXPECT_EQ(data_channel_create(offerPc, (PCHAR) "Offer PeerConnection", &rtcDataChannelInit, &pOfferDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_create(answerPc, (PCHAR) "Answer PeerConnection", NULL, &pAnswerDataChannel), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onOpen(pOfferDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onOpen(pAnswerDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onMessage(pOfferDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onMessage(pAnswerDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);

    EXPECT_EQ(connectTwoPeers(offerPc, answerPc), TRUE);

    // Busy wait until DataChannels connect and send a message
    for (auto i = 0; i <= 100 && (ATOMIC_LOAD(&datachannelLocalOpenCount) + ATOMIC_LOAD(&msgCount)) != 4 ; i++) {
        THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_SECOND);
    }

    // Close the connection to avoid data race while accessing SctpSession
    peer_connection_close(offerPc);
    peer_connection_close(answerPc);
    pKvsDataChannel = (PKvsDataChannel) pOfferDataChannel;
    pSctpSession = ((PKvsPeerConnection) pKvsDataChannel->pRtcPeerConnection)->pSctpSession;
    
    ASSERT_NE(pSctpSession->spa.sendv_sndinfo.snd_flags, SCTP_UNORDERED);
    ASSERT_EQ(pSctpSession->spa.sendv_prinfo.pr_policy, SCTP_PR_SCTP_RTX);
    ASSERT_EQ(pSctpSession->spa.sendv_prinfo.pr_value, rtcDataChannelInit.maxRetransmits.value);

    peer_connection_free(&offerPc);
    peer_connection_free(&answerPc);
}

TEST_F(DataChannelFunctionalityTest, createDataChannel_DataChannelMetricsTest)
{
    RtcConfiguration configuration;
    PRtcPeerConnection offerPc = NULL, answerPc = NULL;
    PRtcDataChannel pOfferDataChannel = nullptr, pAnswerDataChannel = nullptr;
    volatile SIZE_T datachannelLocalOpenCount = 0, msgCount = 0;
    RemoteOpen remoteOpen{};
    RtcStats rtcMetrics;
    rtcMetrics.requestedTypeOfStats = RTC_STATS_TYPE_DATA_CHANNEL;

    EXPECT_EQ(rtcPeerConnectionGetMetrics(NULL, NULL, NULL), STATUS_NULL_ARG);

    MEMSET(&configuration, 0x00, SIZEOF(RtcConfiguration));

    EXPECT_EQ(peer_connection_create(&configuration, &offerPc), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_create(&configuration, &answerPc), STATUS_SUCCESS);

    auto onDataChannel = [](UINT64 customData, PRtcDataChannel pRtcDataChannel) {
        auto remoteOpen = reinterpret_cast<RemoteOpen*>(customData);
        DLOGD("onDataChannel '%s'", pRtcDataChannel->name);
        std::string name(pRtcDataChannel->name);
        {
            std::lock_guard<std::mutex> lock(remoteOpen->lock);
            if (remoteOpen->channels.count(name) == 0) {
                remoteOpen->channels.emplace(name, 1u);
            } else {
                auto count = remoteOpen->channels.at(name);
                remoteOpen->channels.erase(name);
                remoteOpen->channels.emplace(name, count + 1);
            }
        }
        data_channel_send(pRtcDataChannel, FALSE, (PBYTE) TEST_DATA_CHANNEL_MESSAGE, STRLEN(TEST_DATA_CHANNEL_MESSAGE));
    };

    auto dataChannelOnOpenCallback = [](UINT64 customData, PRtcDataChannel pDataChannel) {
        UNUSED_PARAM(pDataChannel);
        ATOMIC_INCREMENT((PSIZE_T) customData);
    };

    auto dataChannelOnMessageCallback = [](UINT64 customData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMsg, UINT32 pMsgLen) {
        UNUSED_PARAM(pDataChannel);
        UNUSED_PARAM(isBinary);
        if (STRNCMP((PCHAR) pMsg, TEST_DATA_CHANNEL_MESSAGE, pMsgLen) == 0) {
            ATOMIC_INCREMENT((PSIZE_T) customData);
        }
    };

    EXPECT_EQ(peer_connection_onDataChannel(offerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(peer_connection_onDataChannel(answerPc, (UINT64) &remoteOpen, onDataChannel), STATUS_SUCCESS);

    // Create two DataChannels
    EXPECT_EQ(data_channel_create(offerPc, (PCHAR) "Offer PeerConnection", nullptr, &pOfferDataChannel), STATUS_SUCCESS);
    rtcMetrics.rtcStatsObject.rtcDataChannelStats.dataChannelIdentifier = pOfferDataChannel->id;
    EXPECT_EQ(rtcPeerConnectionGetMetrics(offerPc, NULL, &rtcMetrics), STATUS_SUCCESS);
    EXPECT_EQ(rtcMetrics.rtcStatsObject.rtcDataChannelStats.state, RTC_DATA_CHANNEL_STATE_CONNECTING);
    EXPECT_EQ(data_channel_create(answerPc, (PCHAR) "Answer PeerConnection", nullptr, &pAnswerDataChannel), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onOpen(pOfferDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onOpen(pAnswerDataChannel, (UINT64) &datachannelLocalOpenCount, dataChannelOnOpenCallback), STATUS_SUCCESS);

    EXPECT_EQ(data_channel_onMessage(pOfferDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_onMessage(pAnswerDataChannel, (UINT64) &msgCount, dataChannelOnMessageCallback), STATUS_SUCCESS);

    EXPECT_EQ(connectTwoPeers(offerPc, answerPc), TRUE);

    // Busy wait until DataChannels connect and send a message
    for (auto i = 0; i <= 100 && (ATOMIC_LOAD(&datachannelLocalOpenCount) + ATOMIC_LOAD(&msgCount)) != 4; i++) {
        THREAD_SLEEP(HUNDREDS_OF_NANOS_IN_A_SECOND);
    }
    EXPECT_EQ(rtcPeerConnectionGetMetrics(offerPc, NULL, &rtcMetrics), STATUS_SUCCESS);
    EXPECT_EQ(rtcMetrics.rtcStatsObject.rtcDataChannelStats.bytesReceived, 0);
    EXPECT_EQ(rtcMetrics.rtcStatsObject.rtcDataChannelStats.messagesReceived, 0);
    EXPECT_EQ(rtcMetrics.rtcStatsObject.rtcDataChannelStats.bytesSent, STRLEN(TEST_DATA_CHANNEL_MESSAGE));
    EXPECT_EQ(rtcMetrics.rtcStatsObject.rtcDataChannelStats.messagesSent, 1);
    EXPECT_EQ(rtcMetrics.rtcStatsObject.rtcDataChannelStats.state, RTC_DATA_CHANNEL_STATE_OPEN);

    peer_connection_close(offerPc);
    peer_connection_close(answerPc);
    peer_connection_free(&offerPc);
    peer_connection_free(&answerPc);
}

} // namespace webrtcclient
} // namespace video
} // namespace kinesis
} // namespace amazonaws
} // namespace com
