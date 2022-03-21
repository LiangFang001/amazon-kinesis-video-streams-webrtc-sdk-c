#include "WebRTCClientTestFixture.h"

namespace com {
namespace amazonaws {
namespace kinesis {
namespace video {
namespace webrtcclient {

class SignalingApiTest : public WebRtcClientTestBase {
};

TEST_F(SignalingApiTest, signaling_send)
{
    STATUS expectedStatus;
    SignalingMessage signalingMessage;

    initializeSignalingClient();

    signalingMessage.version = SIGNALING_MESSAGE_CURRENT_VERSION;
    signalingMessage.messageType = SIGNALING_MESSAGE_TYPE_OFFER;
    STRCPY(signalingMessage.peerClientId, TEST_SIGNALING_MASTER_CLIENT_ID);
    MEMSET(signalingMessage.payload, 'A', 100);
    signalingMessage.payload[100] = '\0';
    signalingMessage.payloadLen = 0;
    signalingMessage.correlationId[0] = '\0';

    EXPECT_NE(STATUS_SUCCESS, signaling_client_sendMsg(INVALID_SIGNALING_CLIENT_HANDLE_VALUE, &signalingMessage));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_sendMsg(mSignalingClientHandle, NULL));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_sendMsg(INVALID_SIGNALING_CLIENT_HANDLE_VALUE, NULL));

    // Not connected
    expectedStatus = mAccessKeyIdSet ? STATUS_STATE_MACHINE_INVALID_STATE : STATUS_NULL_ARG;
    EXPECT_EQ(expectedStatus, signaling_client_sendMsg(mSignalingClientHandle, &signalingMessage));

    // Connect and retry
    expectedStatus = mAccessKeyIdSet ? STATUS_SUCCESS : STATUS_NULL_ARG;
    EXPECT_EQ(expectedStatus, signaling_client_connect(mSignalingClientHandle));
    EXPECT_EQ(expectedStatus, signaling_client_sendMsg(mSignalingClientHandle, &signalingMessage));

    // Some correlation id
    STRCPY(signalingMessage.correlationId, SIGNAING_TEST_CORRELATION_ID);
    EXPECT_EQ(expectedStatus, signaling_client_sendMsg(mSignalingClientHandle, &signalingMessage));

    // No peer id
    signalingMessage.peerClientId[0] = '\0';
    EXPECT_EQ(expectedStatus, signaling_client_sendMsg(mSignalingClientHandle, &signalingMessage));

    // No peer id no correlation id
    signalingMessage.correlationId[0] = '\0';
    EXPECT_EQ(expectedStatus, signaling_client_sendMsg(mSignalingClientHandle, &signalingMessage));

    deinitializeSignalingClient();
}

TEST_F(SignalingApiTest, signalingSendMessageSyncFileCredsProvider)
{
    SignalingMessage signalingMessage;
    PAwsCredentialProvider pAwsCredentialProvider = NULL;
    CHAR fileContent[10000];
    UINT32 length = ARRAY_SIZE(fileContent);

    if (!mAccessKeyIdSet) {
        return;
    }

    // Store the credentials in a file under the current dir
    length = SNPRINTF(fileContent, length, "CREDENTIALS %s %s", mAccessKey, mSecretKey);
    ASSERT_GT(ARRAY_SIZE(fileContent), length);
    ASSERT_EQ(STATUS_SUCCESS, fileio_write(TEST_FILE_CREDENTIALS_FILE_PATH, FALSE, FALSE, (PBYTE) fileContent, length));

    // Create file creds provider from the file
    EXPECT_EQ(STATUS_SUCCESS, createFileCredentialProvider(TEST_FILE_CREDENTIALS_FILE_PATH, &pAwsCredentialProvider));

    initializeSignalingClient(pAwsCredentialProvider);

    signalingMessage.version = SIGNALING_MESSAGE_CURRENT_VERSION;
    signalingMessage.messageType = SIGNALING_MESSAGE_TYPE_OFFER;
    STRCPY(signalingMessage.peerClientId, TEST_SIGNALING_MASTER_CLIENT_ID);
    MEMSET(signalingMessage.payload, 'A', 100);
    signalingMessage.payload[100] = '\0';
    signalingMessage.payloadLen = 0;
    signalingMessage.correlationId[0] = '\0';

    EXPECT_EQ(STATUS_SUCCESS, signaling_client_connect(mSignalingClientHandle));
    EXPECT_EQ(STATUS_SUCCESS, signaling_client_sendMsg(mSignalingClientHandle, &signalingMessage));

    // Some correlation id
    STRCPY(signalingMessage.correlationId, SIGNAING_TEST_CORRELATION_ID);
    EXPECT_EQ(STATUS_SUCCESS, signaling_client_sendMsg(mSignalingClientHandle, &signalingMessage));

    // No peer id
    signalingMessage.peerClientId[0] = '\0';
    EXPECT_EQ(STATUS_SUCCESS, signaling_client_sendMsg(mSignalingClientHandle, &signalingMessage));

    // No peer id no correlation id
    signalingMessage.correlationId[0] = '\0';
    EXPECT_EQ(STATUS_SUCCESS, signaling_client_sendMsg(mSignalingClientHandle, &signalingMessage));

    deinitializeSignalingClient();

    EXPECT_EQ(STATUS_SUCCESS, freeFileCredentialProvider(&pAwsCredentialProvider));
}

TEST_F(SignalingApiTest, signaling_client_connect)
{
    STATUS expectedStatus;

    initializeSignalingClient();
    EXPECT_NE(STATUS_SUCCESS, signaling_client_connect(INVALID_SIGNALING_CLIENT_HANDLE_VALUE));
    expectedStatus = mAccessKeyIdSet ? STATUS_SUCCESS : STATUS_NULL_ARG;
    EXPECT_EQ(expectedStatus, signaling_client_connect(mSignalingClientHandle));

    // Connect again
    EXPECT_EQ(expectedStatus, signaling_client_connect(mSignalingClientHandle));
    EXPECT_EQ(expectedStatus, signaling_client_connect(mSignalingClientHandle));

    deinitializeSignalingClient();
}

TEST_F(SignalingApiTest, signaling_client_delete)
{
    STATUS expectedStatus;

    initializeSignalingClient();
    EXPECT_NE(STATUS_SUCCESS, signaling_client_delete(INVALID_SIGNALING_CLIENT_HANDLE_VALUE));
    expectedStatus = mAccessKeyIdSet ? STATUS_SUCCESS : STATUS_NULL_ARG;
    EXPECT_EQ(expectedStatus, signaling_client_delete(mSignalingClientHandle));

    // Call again - idempotent
    EXPECT_EQ(expectedStatus, signaling_client_delete(mSignalingClientHandle));

    // Attempt to call a connect should fail
    expectedStatus = mAccessKeyIdSet ? STATUS_STATE_MACHINE_INVALID_STATE : STATUS_NULL_ARG;
    EXPECT_EQ(expectedStatus, signaling_client_connect(mSignalingClientHandle));

    // Attempt to send a message should fail
    SignalingMessage signalingMessage;
    signalingMessage.version = SIGNALING_MESSAGE_CURRENT_VERSION;
    signalingMessage.messageType = SIGNALING_MESSAGE_TYPE_OFFER;
    STRCPY(signalingMessage.peerClientId, TEST_SIGNALING_MASTER_CLIENT_ID);
    MEMSET(signalingMessage.payload, 'A', 100);
    signalingMessage.payload[100] = '\0';
    signalingMessage.payloadLen = 0;
    signalingMessage.correlationId[0] = '\0';
    EXPECT_EQ(expectedStatus, signaling_client_sendMsg(mSignalingClientHandle, &signalingMessage));

    deinitializeSignalingClient();
}

TEST_F(SignalingApiTest, signaling_client_getIceConfigInfoCount)
{
    STATUS expectedStatus;
    UINT32 count;

    initializeSignalingClient();
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getIceConfigInfoCount(INVALID_SIGNALING_CLIENT_HANDLE_VALUE, &count));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getIceConfigInfoCount(mSignalingClientHandle, NULL));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getIceConfigInfoCount(INVALID_SIGNALING_CLIENT_HANDLE_VALUE, NULL));

    expectedStatus = mAccessKeyIdSet ? STATUS_SUCCESS : STATUS_NULL_ARG;
    EXPECT_EQ(expectedStatus, signaling_client_getIceConfigInfoCount(mSignalingClientHandle, &count));
    if (mAccessKeyIdSet) {
        EXPECT_NE(0, count);
        EXPECT_GE(MAX_ICE_CONFIG_COUNT, count);
    }

    deinitializeSignalingClient();
}

TEST_F(SignalingApiTest, signaling_client_getIceConfigInfo)
{
    UINT32 i, j, count;
    PIceConfigInfo pIceConfigInfo;

    initializeSignalingClient();
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getIceConfigInfo(INVALID_SIGNALING_CLIENT_HANDLE_VALUE, 0, &pIceConfigInfo));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getIceConfigInfo(mSignalingClientHandle, 0, NULL));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getIceConfigInfo(INVALID_SIGNALING_CLIENT_HANDLE_VALUE, 0, NULL));

    if (mAccessKeyIdSet) {
        EXPECT_EQ(STATUS_SUCCESS, signaling_client_getIceConfigInfoCount(mSignalingClientHandle, &count));
        EXPECT_NE(0, count);
        EXPECT_GE(MAX_ICE_CONFIG_COUNT, count);

        // Referencing past the max count
        EXPECT_NE(STATUS_SUCCESS, signaling_client_getIceConfigInfo(mSignalingClientHandle, count, &pIceConfigInfo));

        for (i = 0; i < count; i++) {
            EXPECT_EQ(STATUS_SUCCESS, signaling_client_getIceConfigInfo(mSignalingClientHandle, i, &pIceConfigInfo));
            EXPECT_EQ(SIGNALING_ICE_CONFIG_INFO_CURRENT_VERSION, pIceConfigInfo->version);
            EXPECT_NE(0, pIceConfigInfo->uriCount);
            EXPECT_GE(MAX_ICE_CONFIG_URI_COUNT, pIceConfigInfo->uriCount);
            EXPECT_NE('\0', pIceConfigInfo->password[0]);
            EXPECT_NE('\0', pIceConfigInfo->userName[0]);
            EXPECT_NE(0, pIceConfigInfo->ttl);

            for (j = 0; j < pIceConfigInfo->uriCount; j++) {
                EXPECT_NE('\0', pIceConfigInfo->uris[j][0]);
            }
        }
    }

    deinitializeSignalingClient();
}

TEST_F(SignalingApiTest, signaling_client_getCurrentState)
{
    STATUS expectedStatus;
    SIGNALING_CLIENT_STATE state, expectedState;

    initializeSignalingClient();
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getCurrentState(INVALID_SIGNALING_CLIENT_HANDLE_VALUE, &state));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getCurrentState(mSignalingClientHandle, NULL));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getCurrentState(INVALID_SIGNALING_CLIENT_HANDLE_VALUE, NULL));

    expectedStatus = mAccessKeyIdSet ? STATUS_SUCCESS : STATUS_NULL_ARG;
    EXPECT_EQ(expectedStatus, signaling_client_getCurrentState(mSignalingClientHandle, &state));

    expectedState = mAccessKeyIdSet ? SIGNALING_CLIENT_STATE_READY : SIGNALING_CLIENT_STATE_UNKNOWN;
    EXPECT_EQ(expectedState, state);

    deinitializeSignalingClient();
}

TEST_F(SignalingApiTest, signaling_client_getStateString)
{
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getStateString(SIGNALING_CLIENT_STATE_UNKNOWN, NULL));

    for (UINT32 i = 0; i <= (UINT32) SIGNALING_CLIENT_STATE_MAX_VALUE + 1; i++) {
        PCHAR pStateStr;
        EXPECT_EQ(STATUS_SUCCESS, signaling_client_getStateString((SIGNALING_CLIENT_STATE) i, &pStateStr));
        DLOGI("Iterating states \"%s\"", pStateStr);
    }
}

TEST_F(SignalingApiTest, signaling_client_disconnect)
{
    EXPECT_NE(STATUS_SUCCESS, signaling_client_disconnect(INVALID_SIGNALING_CLIENT_HANDLE_VALUE));
}

TEST_F(SignalingApiTest, signaling_client_getMetrics)
{
    SignalingClientMetrics metrics;
    SignalingMessage signalingMessage;
    metrics.version = SIGNALING_CLIENT_METRICS_CURRENT_VERSION;

    // Invalid input
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getMetrics(INVALID_SIGNALING_CLIENT_HANDLE_VALUE, &metrics));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getMetrics(INVALID_SIGNALING_CLIENT_HANDLE_VALUE, NULL));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getMetrics(mSignalingClientHandle, NULL));

    if (!mAccessKeyIdSet) {
        return;
    }

    initializeSignalingClient();
    // Valid call
    EXPECT_EQ(STATUS_SUCCESS, signaling_client_getMetrics(mSignalingClientHandle, &metrics));

    EXPECT_EQ(0, metrics.signalingClientStats.numberOfReconnects);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfMessagesSent);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfMessagesReceived);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfErrors);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfRuntimeErrors);
    EXPECT_EQ(1, metrics.signalingClientStats.iceRefreshCount);
    EXPECT_NE(0, metrics.signalingClientStats.signalingClientUptime);
    EXPECT_EQ(0, metrics.signalingClientStats.connectionDuration);
    EXPECT_NE(0, metrics.signalingClientStats.cpApiCallLatency);
    EXPECT_NE(0, metrics.signalingClientStats.dpApiCallLatency);

    // Connect and get metrics
    EXPECT_EQ(STATUS_SUCCESS, signaling_client_connect(mSignalingClientHandle));

    // Await for a little to ensure we get some metrics for the connection duration
    THREAD_SLEEP(200 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND);

    EXPECT_EQ(STATUS_SUCCESS, signaling_client_getMetrics(mSignalingClientHandle, &metrics));
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfReconnects);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfMessagesSent);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfMessagesReceived);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfErrors);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfRuntimeErrors);
    EXPECT_EQ(1, metrics.signalingClientStats.iceRefreshCount);
    EXPECT_NE(0, metrics.signalingClientStats.signalingClientUptime);
    EXPECT_NE(0, metrics.signalingClientStats.connectionDuration);
    EXPECT_NE(0, metrics.signalingClientStats.cpApiCallLatency);
    EXPECT_NE(0, metrics.signalingClientStats.dpApiCallLatency);

    // Send a message and get metrics
    signalingMessage.version = SIGNALING_MESSAGE_CURRENT_VERSION;
    signalingMessage.messageType = SIGNALING_MESSAGE_TYPE_OFFER;
    STRCPY(signalingMessage.peerClientId, TEST_SIGNALING_MASTER_CLIENT_ID);
    MEMSET(signalingMessage.payload, 'A', 100);
    signalingMessage.payload[100] = '\0';
    signalingMessage.payloadLen = 0;
    signalingMessage.correlationId[0] = '\0';

    EXPECT_EQ(STATUS_SUCCESS, signaling_client_sendMsg(mSignalingClientHandle, &signalingMessage));
    EXPECT_EQ(STATUS_SUCCESS, signaling_client_getMetrics(mSignalingClientHandle, &metrics));
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfReconnects);
    EXPECT_EQ(1, metrics.signalingClientStats.numberOfMessagesSent);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfMessagesReceived);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfErrors);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfRuntimeErrors);
    EXPECT_EQ(1, metrics.signalingClientStats.iceRefreshCount);
    EXPECT_NE(0, metrics.signalingClientStats.signalingClientUptime);
    EXPECT_NE(0, metrics.signalingClientStats.connectionDuration);
    EXPECT_NE(0, metrics.signalingClientStats.cpApiCallLatency);
    EXPECT_NE(0, metrics.signalingClientStats.dpApiCallLatency);

    // Make a couple of bad API invocations
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getIceConfigInfoCount(mSignalingClientHandle, NULL));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getIceConfigInfo(mSignalingClientHandle, 0, NULL));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getCurrentState(mSignalingClientHandle, NULL));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_getMetrics(mSignalingClientHandle, NULL));
    EXPECT_NE(STATUS_SUCCESS, signaling_client_sendMsg(mSignalingClientHandle, NULL));

    EXPECT_EQ(STATUS_SUCCESS, signaling_client_getMetrics(mSignalingClientHandle, &metrics));
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfReconnects);
    EXPECT_EQ(1, metrics.signalingClientStats.numberOfMessagesSent);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfMessagesReceived);
    EXPECT_EQ(5, metrics.signalingClientStats.numberOfErrors);
    EXPECT_EQ(0, metrics.signalingClientStats.numberOfRuntimeErrors);
    EXPECT_EQ(1, metrics.signalingClientStats.iceRefreshCount);
    EXPECT_NE(0, metrics.signalingClientStats.signalingClientUptime);
    EXPECT_NE(0, metrics.signalingClientStats.connectionDuration);
    EXPECT_NE(0, metrics.signalingClientStats.cpApiCallLatency);
    EXPECT_NE(0, metrics.signalingClientStats.dpApiCallLatency);

    deinitializeSignalingClient();
}

} // namespace webrtcclient
} // namespace video
} // namespace kinesis
} // namespace amazonaws
} // namespace com
