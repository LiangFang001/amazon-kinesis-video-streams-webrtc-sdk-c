#include "WebRTCClientTestFixture.h"

namespace com {
namespace amazonaws {
namespace kinesis {
namespace video {
namespace webrtcclient {

class DataChannelApiTest : public WebRtcClientTestBase {
};

TEST_F(DataChannelApiTest, createDataChannel_Disconnected)
{
    RtcConfiguration configuration;
    PRtcPeerConnection pPeerConnection = nullptr;
    PRtcDataChannel pDataChannel = nullptr;

    MEMSET(&configuration, 0x00, SIZEOF(RtcConfiguration));

    EXPECT_EQ(pc_create(&configuration, &pPeerConnection), STATUS_SUCCESS);

    // Create two DataChannels
    EXPECT_EQ(data_channel_create(pPeerConnection, (PCHAR) "DataChannel 1", nullptr, &pDataChannel), STATUS_SUCCESS);
    EXPECT_EQ(data_channel_create(pPeerConnection, (PCHAR) "DataChannel 2", nullptr, &pDataChannel), STATUS_SUCCESS);

    // Don't allow NULL
    EXPECT_EQ(data_channel_create(nullptr, (PCHAR) "DataChannel 2", nullptr, &pDataChannel), STATUS_NULL_ARG);
    EXPECT_EQ(data_channel_create(pPeerConnection, nullptr, nullptr, &pDataChannel), STATUS_NULL_ARG);
    EXPECT_EQ(data_channel_create(pPeerConnection, (PCHAR) "DataChannel 2", nullptr, nullptr), STATUS_NULL_ARG);

    pc_close(pPeerConnection);
    pc_free(&pPeerConnection);
}

} // namespace webrtcclient
} // namespace video
} // namespace kinesis
} // namespace amazonaws
} // namespace com
