#define LOG_CLASS "SessionDescription"

#include "network.h"
#include "SessionDescription.h"
#include "Rtp.h"

#ifndef JSMN_HEADER
#define JSMN_HEADER
#endif
#include "jsmn.h"

#define VIDEO_SUPPPORT_TYPE(codec) (codec == RTC_CODEC_VP8 || codec == RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE)
#define AUDIO_SUPPORT_TYPE(codec)  (codec == RTC_CODEC_MULAW || codec == RTC_CODEC_ALAW || codec == RTC_CODEC_OPUS)

STATUS sdp_serializeInit(PRtcSessionDescriptionInit pSessionDescriptionInit, PCHAR sessionDescriptionJSON, PUINT32 sessionDescriptionJSONLen)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PCHAR curr, tail, next;
    UINT32 lineLen, inputSize = 0, amountWritten;

    CHK(pSessionDescriptionInit != NULL && sessionDescriptionJSONLen != NULL, STATUS_SDP_NULL_ARG);

    inputSize = *sessionDescriptionJSONLen;
    *sessionDescriptionJSONLen = 0;

    amountWritten =
        SNPRINTF(sessionDescriptionJSON, sessionDescriptionJSON == NULL ? 0 : inputSize - *sessionDescriptionJSONLen,
                 SESSION_DESCRIPTION_INIT_TEMPLATE_HEAD, pSessionDescriptionInit->type == SDP_TYPE_OFFER ? SDP_OFFER_VALUE : SDP_ANSWER_VALUE);
    CHK(sessionDescriptionJSON == NULL || ((inputSize - *sessionDescriptionJSONLen) >= amountWritten), STATUS_BUFFER_TOO_SMALL);
    *sessionDescriptionJSONLen += amountWritten;

    curr = pSessionDescriptionInit->sdp;
    tail = pSessionDescriptionInit->sdp + STRLEN(pSessionDescriptionInit->sdp);

    while ((next = STRNCHR(curr, (UINT32)(tail - curr), '\n')) != NULL) {
        lineLen = (UINT32)(next - curr);

        if (lineLen > 0 && curr[lineLen - 1] == '\r') {
            lineLen--;
        }

        amountWritten =
            SNPRINTF(sessionDescriptionJSON + *sessionDescriptionJSONLen, sessionDescriptionJSON == NULL ? 0 : inputSize - *sessionDescriptionJSONLen,
                     "%*.*s%s", lineLen, lineLen, curr, SESSION_DESCRIPTION_INIT_LINE_ENDING);
        CHK(sessionDescriptionJSON == NULL || ((inputSize - *sessionDescriptionJSONLen) >= amountWritten), STATUS_BUFFER_TOO_SMALL);

        *sessionDescriptionJSONLen += amountWritten;
        curr = next + 1;
    }

    amountWritten = SNPRINTF(sessionDescriptionJSON + *sessionDescriptionJSONLen,
                             sessionDescriptionJSON == NULL ? 0 : inputSize - *sessionDescriptionJSONLen, SESSION_DESCRIPTION_INIT_TEMPLATE_TAIL);
    CHK(sessionDescriptionJSON == NULL || ((inputSize - *sessionDescriptionJSONLen) >= amountWritten), STATUS_BUFFER_TOO_SMALL);
    *sessionDescriptionJSONLen += (amountWritten + 1); // NULL terminator

CleanUp:

    LEAVES();
    return retStatus;
}

/**
 * @brief Parses a JSON string and returns an allocated PSessionDescriptionInit
 *
 * @param[in] PCHAR JSON String of a RtcSessionDescriptionInit
 * @param[in] UINT32 Length of JSON String
 * @param[out] PRtcSessionDescriptionInit RtcSessionDescriptionInit populated from JSON String
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success
 */
STATUS sdp_deserializeInit(PCHAR sessionDescriptionJSON, UINT32 sessionDescriptionJSONLen, PRtcSessionDescriptionInit pSessionDescriptionInit)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    jsmntok_t tokens[MAX_JSON_TOKEN_COUNT];
    jsmn_parser parser;
    INT8 i;
    INT32 j, tokenCount, lineLen;
    PCHAR curr, next, tail;

    CHK(pSessionDescriptionInit != NULL && sessionDescriptionJSON != NULL, STATUS_SDP_NULL_ARG);
    MEMSET(pSessionDescriptionInit, 0x00, SIZEOF(RtcSessionDescriptionInit));

    jsmn_init(&parser);

    tokenCount = jsmn_parse(&parser, sessionDescriptionJSON, sessionDescriptionJSONLen, tokens, ARRAY_SIZE(tokens));
    CHK(tokenCount > 1, STATUS_JSON_API_CALL_INVALID_RETURN);
    CHK(tokens[0].type == JSMN_OBJECT, STATUS_SDP_INIT_NOT_OBJECT);

    for (i = 1; i < tokenCount; i += 2) {
        if (STRNCMP(SDP_TYPE_KEY, sessionDescriptionJSON + tokens[i].start, ARRAY_SIZE(SDP_TYPE_KEY) - 1) == 0) {
            if (STRNCMP(SDP_OFFER_VALUE, sessionDescriptionJSON + tokens[i + 1].start, ARRAY_SIZE(SDP_OFFER_VALUE) - 1) == 0) {
                pSessionDescriptionInit->type = SDP_TYPE_OFFER;
            } else if (STRNCMP(SDP_ANSWER_VALUE, sessionDescriptionJSON + tokens[i + 1].start, ARRAY_SIZE(SDP_ANSWER_VALUE) - 1) == 0) {
                pSessionDescriptionInit->type = SDP_TYPE_ANSWER;
            } else {
                CHK(FALSE, STATUS_SDP_INIT_INVALID_TYPE);
            }
        } else if (STRNCMP(SDP_KEY, sessionDescriptionJSON + tokens[i].start, ARRAY_SIZE(SDP_KEY) - 1) == 0) {
            CHK((tokens[i + 1].end - tokens[i + 1].start) <= MAX_SESSION_DESCRIPTION_INIT_SDP_LEN, STATUS_SDP_INIT_MAX_SDP_LEN_EXCEEDED);
            curr = sessionDescriptionJSON + tokens[i + 1].start;
            tail = sessionDescriptionJSON + tokens[i + 1].end;
            j = 0;

            // Unescape carriage return and line feed characters. The SDP that we receive at this point is in
            // JSON format, meaning that carriage return and line feed characters are escaped. So, to represent
            // these characters, a single escape character is prepended to each of them.
            //
            // When we store the sdp in memory, we want to recover the original format, without the escape characters.
            //
            // For example:
            //     \r becomes '\' and 'r'
            //     \n becomes '\' and 'n'
            while ((next = STRNSTR(curr, SESSION_DESCRIPTION_INIT_LINE_ENDING_WITHOUT_CR, tail - curr)) != NULL) {
                lineLen = (INT32)(next - curr);

                // Check if the SDP format is using \r\n or \n separator.
                // There are escape characters before \n and \r, so we need to move back 1 more character
                if (lineLen > 1 && curr[lineLen - 2] == '\\' && curr[lineLen - 1] == 'r') {
                    lineLen -= 2;
                }

                MEMCPY((pSessionDescriptionInit->sdp) + j, curr, lineLen * SIZEOF(CHAR));
                // Since we're adding 2 characters to the line, \r and \n (SDP record is separated by crlf),
                // we need to add 2 to the serialized line so that the next iteration will not overwrite
                // these 2 characters.
                j += (lineLen + 2);
                pSessionDescriptionInit->sdp[j - 2] = '\r';
                pSessionDescriptionInit->sdp[j - 1] = '\n';

                curr = next + 2;
            }
        }
    }

    CHK(pSessionDescriptionInit->sdp[0] != '\0', STATUS_SDP_INIT_MISSING_SDP);
    CHK(pSessionDescriptionInit->type != 0, STATUS_SDP_INIT_MISSING_TYPE);

CleanUp:

    LEAVES();
    return retStatus;
}
#ifdef ENABLE_STREAMING
/*
 * Populate map with PayloadTypes if we are offering
 */
STATUS sdp_setPayloadTypesForOffer(PHashTable codecTable)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;

    CHK_STATUS(hashTableUpsert(codecTable, RTC_CODEC_MULAW, DEFAULT_PAYLOAD_MULAW));
    CHK_STATUS(hashTableUpsert(codecTable, RTC_CODEC_ALAW, DEFAULT_PAYLOAD_ALAW));
    CHK_STATUS(hashTableUpsert(codecTable, RTC_CODEC_VP8, DEFAULT_PAYLOAD_VP8));
    CHK_STATUS(hashTableUpsert(codecTable, RTC_CODEC_OPUS, DEFAULT_PAYLOAD_OPUS));
    CHK_STATUS(hashTableUpsert(codecTable, RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE, DEFAULT_PAYLOAD_H264));

CleanUp:
    return retStatus;
}

STATUS sdp_setPayloadTypesFromOffer(PHashTable codecTable, PHashTable rtxTable, PSessionDescription pSessionDescription)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSdpMediaDescription pMediaDescription = NULL;
    UINT8 currentMedia, currentAttribute;
    PCHAR attributeValue, end;
    UINT64 parsedPayloadType, rtxPayloadType, hashmapPayloadType;
    BOOL supportCodec;
    UINT32 tokenLen;

    for (currentMedia = 0; currentMedia < pSessionDescription->mediaCount; currentMedia++) {
        pMediaDescription = &(pSessionDescription->mediaDescriptions[currentMedia]);

        attributeValue = pMediaDescription->mediaName;

        do {
            if ((end = STRCHR(attributeValue, ' ')) != NULL) {
                tokenLen = (end - attributeValue);
            } else {
                tokenLen = STRLEN(attributeValue);
            }
            // #TBD, this should be reviewed.
            // update the aduio.
            if (STRNCMP(DEFAULT_PAYLOAD_MULAW_STR, attributeValue, tokenLen) == 0) {
                CHK_STATUS(hashTableUpsert(codecTable, RTC_CODEC_MULAW, DEFAULT_PAYLOAD_MULAW));
            } else if (STRNCMP(DEFAULT_PAYLOAD_ALAW_STR, attributeValue, tokenLen) == 0) {
                CHK_STATUS(hashTableUpsert(codecTable, RTC_CODEC_ALAW, DEFAULT_PAYLOAD_ALAW));
            }

            if (end != NULL) {
                attributeValue = end + 1;
            }
        } while (end != NULL);
        // should split this into different media, no need to process all the media type at the same time.
        for (currentAttribute = 0; currentAttribute < pMediaDescription->mediaAttributesCount; currentAttribute++) {
            attributeValue = pMediaDescription->sdpAttributes[currentAttribute].attributeValue;
            // #video.
            CHK_STATUS(hash_table_contains(codecTable, RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE, &supportCodec));
            if (supportCodec && (end = STRSTR(attributeValue, H264_VALUE)) != NULL) {
                CHK_STATUS(STRTOUI64(attributeValue, end - 1, 10, &parsedPayloadType));
                CHK_STATUS(hashTableUpsert(codecTable, RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE, parsedPayloadType));
            }
            // #audio.
            CHK_STATUS(hash_table_contains(codecTable, RTC_CODEC_OPUS, &supportCodec));
            if (supportCodec && (end = STRSTR(attributeValue, OPUS_VALUE)) != NULL) {
                CHK_STATUS(STRTOUI64(attributeValue, end - 1, 10, &parsedPayloadType));
                CHK_STATUS(hashTableUpsert(codecTable, RTC_CODEC_OPUS, parsedPayloadType));
            }
            // #video.
            CHK_STATUS(hash_table_contains(codecTable, RTC_CODEC_VP8, &supportCodec));
            if (supportCodec && (end = STRSTR(attributeValue, VP8_VALUE)) != NULL) {
                CHK_STATUS(STRTOUI64(attributeValue, end - 1, 10, &parsedPayloadType));
                CHK_STATUS(hashTableUpsert(codecTable, RTC_CODEC_VP8, parsedPayloadType));
            }
            // #audio
            CHK_STATUS(hash_table_contains(codecTable, RTC_CODEC_MULAW, &supportCodec));
            if (supportCodec && (end = STRSTR(attributeValue, MULAW_VALUE)) != NULL) {
                CHK_STATUS(STRTOUI64(attributeValue, end - 1, 10, &parsedPayloadType));
                CHK_STATUS(hashTableUpsert(codecTable, RTC_CODEC_MULAW, parsedPayloadType));
            }
            // #audio
            CHK_STATUS(hash_table_contains(codecTable, RTC_CODEC_ALAW, &supportCodec));
            if (supportCodec && (end = STRSTR(attributeValue, ALAW_VALUE)) != NULL) {
                CHK_STATUS(STRTOUI64(attributeValue, end - 1, 10, &parsedPayloadType));
                CHK_STATUS(hashTableUpsert(codecTable, RTC_CODEC_ALAW, parsedPayloadType));
            }
            // #rtx
            if ((end = STRSTR(attributeValue, RTX_VALUE)) != NULL) {
                CHK_STATUS(STRTOUI64(attributeValue, end - 1, 10, &rtxPayloadType));
                attributeValue = pMediaDescription->sdpAttributes[++currentAttribute].attributeValue;
                if ((end = STRSTR(attributeValue, RTX_CODEC_VALUE)) != NULL) {
                    CHK_STATUS(STRTOUI64(end + STRLEN(RTX_CODEC_VALUE), NULL, 10, &parsedPayloadType));
                    CHK_STATUS(
                        hash_table_contains(codecTable, RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE, &supportCodec));
                    if (supportCodec) {
                        CHK_STATUS(hash_table_get(codecTable, RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE,
                                                  &hashmapPayloadType));
                        if (parsedPayloadType == hashmapPayloadType) {
                            CHK_STATUS(hashTableUpsert(rtxTable, RTC_RTX_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE,
                                                       rtxPayloadType));
                        }
                    }

                    CHK_STATUS(hash_table_contains(codecTable, RTC_CODEC_VP8, &supportCodec));
                    if (supportCodec) {
                        CHK_STATUS(hash_table_get(codecTable, RTC_CODEC_VP8, &hashmapPayloadType));
                        if (parsedPayloadType == hashmapPayloadType) {
                            CHK_STATUS(hashTableUpsert(rtxTable, RTC_RTX_CODEC_VP8, rtxPayloadType));
                        }
                    }
                }
            }
        }
    }

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS sdp_setTransceiverPayloadTypes(PHashTable codecTable, PHashTable rtxTable, PDoubleList pTransceivers)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PDoubleListNode pCurNode = NULL;
    PKvsRtpTransceiver pKvsRtpTransceiver;
    UINT64 data;

    // Loop over Transceivers and set the payloadType (which what we got from the other side)
    // If a codec we want to send wasn't supported by the other return an error
    CHK_STATUS(double_list_getHeadNode(pTransceivers, &pCurNode));
    while (pCurNode != NULL) {
        CHK_STATUS(double_list_getNodeData(pCurNode, &data));
        pCurNode = pCurNode->pNext;
        pKvsRtpTransceiver = (PKvsRtpTransceiver) data;

        if (pKvsRtpTransceiver != NULL &&
            (pKvsRtpTransceiver->transceiver.direction == RTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV ||
             pKvsRtpTransceiver->transceiver.direction == RTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY)) {
            CHK_STATUS(hash_table_get(codecTable, pKvsRtpTransceiver->sender.track.codec, &data));
            pKvsRtpTransceiver->sender.payloadType = (UINT8) data;
            pKvsRtpTransceiver->sender.rtxPayloadType = (UINT8) data;

            // NACKs may have distinct PayloadTypes, look in the rtxTable and check. Otherwise NACKs will just be re-sending the same seqnum
            if (hash_table_get(rtxTable, pKvsRtpTransceiver->sender.track.codec, &data) == STATUS_SUCCESS) {
                pKvsRtpTransceiver->sender.rtxPayloadType = (UINT8) data;
            }
        }

        CHK_STATUS(rtp_rolling_buffer_create(DEFAULT_ROLLING_BUFFER_DURATION_IN_SECONDS * HIGHEST_EXPECTED_BIT_RATE / 8 / DEFAULT_MTU_SIZE,
                                             &pKvsRtpTransceiver->sender.packetBuffer));
        CHK_STATUS(retransmitter_create(DEFAULT_SEQ_NUM_BUFFER_SIZE, DEFAULT_VALID_INDEX_BUFFER_SIZE, &pKvsRtpTransceiver->sender.retransmitter));
    }

CleanUp:

    LEAVES();
    return retStatus;
}
#endif

PCHAR sdp_fmtpForPayloadType(UINT64 payloadType, PSessionDescription pSessionDescription)
{
    UINT32 currentMedia, currentAttribute;
    PSdpMediaDescription pMediaDescription = NULL;
    CHAR payloadStr[MAX_SDP_ATTRIBUTE_VALUE_LENGTH];

    MEMSET(payloadStr, 0x00, MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    SNPRINTF(payloadStr, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%" PRId64, payloadType);

    for (currentMedia = 0; currentMedia < pSessionDescription->mediaCount; currentMedia++) {
        pMediaDescription = &(pSessionDescription->mediaDescriptions[currentMedia]);
        for (currentAttribute = 0; currentAttribute < pMediaDescription->mediaAttributesCount; currentAttribute++) {
            if (STRCMP(pMediaDescription->sdpAttributes[currentAttribute].attributeName, "fmtp") == 0 &&
                STRNCMP(pMediaDescription->sdpAttributes[currentAttribute].attributeValue, payloadStr, STRLEN(payloadStr)) == 0) {
                return pMediaDescription->sdpAttributes[currentAttribute].attributeValue + STRLEN(payloadStr) + 1;
            }
        }
    }

    return NULL;
}

// Populate a single media section from a PKvsRtpTransceiver
STATUS sdp_populateSingleMediaSection(PKvsPeerConnection pKvsPeerConnection, PKvsRtpTransceiver pKvsRtpTransceiver,
                                      PSdpMediaDescription pSdpMediaDescription, PSessionDescription pRemoteSessionDescription,
                                      PCHAR pCertificateFingerprint, UINT32 mediaSectionId, PCHAR pDtlsRole)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT64 payloadType, rtxPayloadType;
    BOOL containRtx = FALSE;
    BOOL directionFound = FALSE;
    UINT32 i, remoteAttributeCount, attributeCount = 0;
    PRtcMediaStreamTrack pRtcMediaStreamTrack = &(pKvsRtpTransceiver->sender.track);
    PSdpMediaDescription pSdpMediaDescriptionRemote;
    PCHAR currentFmtp = NULL;

    CHK_STATUS(hash_table_get(pKvsPeerConnection->pCodecTable, pRtcMediaStreamTrack->codec, &payloadType));
    // get the payload type of audio or video.
    currentFmtp = sdp_fmtpForPayloadType(payloadType, &(pKvsPeerConnection->remoteSessionDescription));
    // video
    if (pRtcMediaStreamTrack->codec == RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE ||
        pRtcMediaStreamTrack->codec == RTC_CODEC_VP8) {
        // get the payload type from rtx table.
        if (pRtcMediaStreamTrack->codec == RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE) {
            retStatus = hash_table_get(pKvsPeerConnection->pRtxTable, RTC_RTX_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE,
                                       &rtxPayloadType);
        } else {
            retStatus = hash_table_get(pKvsPeerConnection->pRtxTable, RTC_RTX_CODEC_VP8, &rtxPayloadType);
        }
        CHK(retStatus == STATUS_SUCCESS || retStatus == STATUS_HASH_KEY_NOT_PRESENT, retStatus);
        containRtx = (retStatus == STATUS_SUCCESS);
        retStatus = STATUS_SUCCESS;
        if (containRtx) {
            SNPRINTF(pSdpMediaDescription->mediaName, MAX_SDP_MEDIA_NAME_LENGTH, "video 9 UDP/TLS/RTP/SAVPF %" PRId64 " %" PRId64, payloadType,
                     rtxPayloadType);
        } else {
            SNPRINTF(pSdpMediaDescription->mediaName, MAX_SDP_MEDIA_NAME_LENGTH, "video 9 UDP/TLS/RTP/SAVPF %" PRId64, payloadType);
        }
        // audio
    } else if (pRtcMediaStreamTrack->codec == RTC_CODEC_OPUS || pRtcMediaStreamTrack->codec == RTC_CODEC_MULAW ||
               pRtcMediaStreamTrack->codec == RTC_CODEC_ALAW) {
        SNPRINTF(pSdpMediaDescription->mediaName, MAX_SDP_MEDIA_NAME_LENGTH, "audio 9 UDP/TLS/RTP/SAVPF %" PRId64, payloadType);
    }
    // get the information of ice candidates.
    CHK_STATUS(ice_agent_populateSdpMediaDescriptionCandidates(pKvsPeerConnection->pIceAgent, pSdpMediaDescription, MAX_SDP_ATTRIBUTE_VALUE_LENGTH,
                                                               &attributeCount));
    // rtx
    if (containRtx) {
        STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "msid", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%s %sRTX",
                 pRtcMediaStreamTrack->streamId, pRtcMediaStreamTrack->trackId);
        attributeCount++;

        STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ssrc-group", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "FID %u %u",
                 pKvsRtpTransceiver->sender.ssrc, pKvsRtpTransceiver->sender.rtxSsrc);
        attributeCount++;
        // no rtx.
    } else {
        STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "msid", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%s %s",
                 pRtcMediaStreamTrack->streamId, pRtcMediaStreamTrack->trackId);
        attributeCount++;
    }

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ssrc", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%u cname:%s",
             pKvsRtpTransceiver->sender.ssrc, pKvsPeerConnection->localCNAME);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ssrc", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%u msid:%s %s",
             pKvsRtpTransceiver->sender.ssrc, pRtcMediaStreamTrack->streamId, pRtcMediaStreamTrack->trackId);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ssrc", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%u mslabel:%s",
             pKvsRtpTransceiver->sender.ssrc, pRtcMediaStreamTrack->streamId);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ssrc", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%u label:%s",
             pKvsRtpTransceiver->sender.ssrc, pRtcMediaStreamTrack->trackId);
    attributeCount++;

    if (containRtx) {
        STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ssrc", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%u cname:%s",
                 pKvsRtpTransceiver->sender.rtxSsrc, pKvsPeerConnection->localCNAME);
        attributeCount++;

        STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ssrc", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%u msid:%s %sRTX",
                 pKvsRtpTransceiver->sender.rtxSsrc, pRtcMediaStreamTrack->streamId, pRtcMediaStreamTrack->trackId);
        attributeCount++;

        STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ssrc", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%u mslabel:%sRTX",
                 pKvsRtpTransceiver->sender.rtxSsrc, pRtcMediaStreamTrack->streamId);
        attributeCount++;

        STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ssrc", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%u label:%sRTX",
                 pKvsRtpTransceiver->sender.rtxSsrc, pRtcMediaStreamTrack->trackId);
        attributeCount++;
    }

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "rtcp", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, "9 IN IP4 0.0.0.0", MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ice-ufrag", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, pKvsPeerConnection->localIceUfrag, MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ice-pwd", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, pKvsPeerConnection->localIcePwd, MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ice-options", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, "trickle", MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "fingerprint", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, "sha-256 ", MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue + 8, pCertificateFingerprint, MAX_SDP_ATTRIBUTE_VALUE_LENGTH - 8);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "setup", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, pDtlsRole, MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "mid", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%d", mediaSectionId);
    attributeCount++;
    // setup the direction of offer.
    if (pKvsPeerConnection->isOffer) {
        switch (pKvsRtpTransceiver->transceiver.direction) {
            case RTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV:
                STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "sendrecv", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
                break;
            case RTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY:
                STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "sendonly", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
                break;
            case RTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY:
                STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "recvonly", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
                break;
            default:
                // https://www.w3.org/TR/webrtc/#dom-rtcrtptransceiverdirection
                DLOGW("Incorrect/no transceiver direction set...this attribute will be set to inactive");
                STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "inactive", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        }
        // setup the direction of answer.
    } else {
        pSdpMediaDescriptionRemote = &pRemoteSessionDescription->mediaDescriptions[mediaSectionId];
        remoteAttributeCount = pSdpMediaDescriptionRemote->mediaAttributesCount;

        for (i = 0; i < remoteAttributeCount && directionFound == FALSE; i++) {
            if (STRCMP(pSdpMediaDescriptionRemote->sdpAttributes[i].attributeName, "sendrecv") == 0) {
                STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "sendrecv", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
                directionFound = TRUE;
            } else if (STRCMP(pSdpMediaDescriptionRemote->sdpAttributes[i].attributeName, "recvonly") == 0) {
                STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "sendonly", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
                directionFound = TRUE;
            } else if (STRCMP(pSdpMediaDescriptionRemote->sdpAttributes[i].attributeName, "sendonly") == 0) {
                STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "recvonly", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
                directionFound = TRUE;
            }
        }
    }

    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "rtcp-mux", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "rtcp-rsize", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    attributeCount++;

    if (pRtcMediaStreamTrack->codec == RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE) {
        if (pKvsPeerConnection->isOffer) {
            currentFmtp = DEFAULT_H264_FMTP;
        }
        STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "rtpmap", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%" PRId64 " H264/90000",
                 payloadType);
        attributeCount++;

        if (currentFmtp != NULL) {
            STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "fmtp", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
            SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%" PRId64 " %s",
                     payloadType, currentFmtp);
            attributeCount++;
        }

        if (containRtx) {
            STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "rtpmap", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
            SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%" PRId64 " " RTX_VALUE,
                     rtxPayloadType);
            attributeCount++;

            STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "fmtp", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
            SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH,
                     "%" PRId64 " apt=%" PRId64 "", rtxPayloadType, payloadType);
            attributeCount++;
        }
    } else if (pRtcMediaStreamTrack->codec == RTC_CODEC_OPUS) {
        if (pKvsPeerConnection->isOffer) {
            currentFmtp = DEFAULT_OPUS_FMTP;
        }
        STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "rtpmap", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%" PRId64 " opus/48000/2",
                 payloadType);
        attributeCount++;

        if (currentFmtp != NULL) {
            STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "fmtp", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
            SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%" PRId64 " %s",
                     payloadType, currentFmtp);
            attributeCount++;
        }
    } else if (pRtcMediaStreamTrack->codec == RTC_CODEC_VP8) {
        STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "rtpmap", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%" PRId64 " " VP8_VALUE,
                 payloadType);
        attributeCount++;

        if (containRtx) {
            CHK_STATUS(hash_table_get(pKvsPeerConnection->pRtxTable, RTC_RTX_CODEC_VP8, &rtxPayloadType));
            STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "rtpmap", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
            SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%" PRId64 " " RTX_VALUE,
                     rtxPayloadType);
            attributeCount++;

            STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "fmtp", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
            SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH,
                     "%" PRId64 " apt=%" PRId64 "", rtxPayloadType, payloadType);
            attributeCount++;
        }
    } else if (pRtcMediaStreamTrack->codec == RTC_CODEC_MULAW) {
        STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "rtpmap", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%" PRId64 " " MULAW_VALUE,
                 payloadType);
        attributeCount++;
    } else if (pRtcMediaStreamTrack->codec == RTC_CODEC_ALAW) {
        STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "rtpmap", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
        SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%" PRId64 " " ALAW_VALUE,
                 payloadType);
        attributeCount++;
    }

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "rtcp-fb", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%" PRId64 " nack", payloadType);
    attributeCount++;

    pSdpMediaDescription->mediaAttributesCount = attributeCount;

CleanUp:

    LEAVES();
    return retStatus;
}

STATUS sdp_populateSessionDescriptionDataChannel(PKvsPeerConnection pKvsPeerConnection, PSdpMediaDescription pSdpMediaDescription,
                                                 PCHAR pCertificateFingerprint, UINT32 mediaSectionId, PCHAR pDtlsRole)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 attributeCount = 0;

    SNPRINTF(pSdpMediaDescription->mediaName, MAX_SDP_MEDIA_NAME_LENGTH, "application 9 UDP/DTLS/SCTP webrtc-datachannel");

    CHK_STATUS(ice_agent_populateSdpMediaDescriptionCandidates(pKvsPeerConnection->pIceAgent, pSdpMediaDescription, MAX_SDP_ATTRIBUTE_VALUE_LENGTH,
                                                               &attributeCount));

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "rtcp", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, "9 IN IP4 0.0.0.0", MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ice-ufrag", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, pKvsPeerConnection->localIceUfrag, MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "ice-pwd", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, pKvsPeerConnection->localIcePwd, MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "fingerprint", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, "sha-256 ", 8);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue + 8, pCertificateFingerprint, MAX_SDP_ATTRIBUTE_VALUE_LENGTH - 8);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "setup", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, pDtlsRole, MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "mid", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "%d", mediaSectionId);
    attributeCount++;

    STRNCPY(pSdpMediaDescription->sdpAttributes[attributeCount].attributeName, "sctp-port", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    SNPRINTF(pSdpMediaDescription->sdpAttributes[attributeCount].attributeValue, MAX_SDP_ATTRIBUTE_VALUE_LENGTH, "5000");
    attributeCount++;

    pSdpMediaDescription->mediaAttributesCount = attributeCount;

CleanUp:

    LEAVES();
    return retStatus;
}

BOOL sdp_isPresentInRemote(PKvsRtpTransceiver pKvsRtpTransceiver, PSessionDescription pRemoteSessionDescription)
{
    PCHAR remoteAttributeValue, end;
    UINT32 remoteTokenLen, i;
    PSdpMediaDescription pRemoteMediaDescription;
    MEDIA_STREAM_TRACK_KIND localTrackKind = pKvsRtpTransceiver->sender.track.kind;
    BOOL wasFound = FALSE;

    for (i = 0; i < pRemoteSessionDescription->mediaCount && wasFound == FALSE; i++) {
        pRemoteMediaDescription = &pRemoteSessionDescription->mediaDescriptions[i];
        remoteAttributeValue = pRemoteMediaDescription->mediaName;

        if ((end = STRCHR(remoteAttributeValue, ' ')) != NULL) {
            remoteTokenLen = (end - remoteAttributeValue);
        } else {
            remoteTokenLen = STRLEN(remoteAttributeValue);
        }

        switch (localTrackKind) {
            case MEDIA_STREAM_TRACK_KIND_AUDIO:
                if (remoteTokenLen == (ARRAY_SIZE(MEDIA_SECTION_AUDIO_VALUE) - 1) &&
                    STRNCMP(MEDIA_SECTION_AUDIO_VALUE, remoteAttributeValue, remoteTokenLen) == 0) {
                    wasFound = TRUE;
                }
                break;
            case MEDIA_STREAM_TRACK_KIND_VIDEO:
                if (remoteTokenLen == (ARRAY_SIZE(MEDIA_SECTION_VIDEO_VALUE) - 1) &&
                    STRNCMP(MEDIA_SECTION_VIDEO_VALUE, remoteAttributeValue, remoteTokenLen) == 0) {
                    wasFound = TRUE;
                }
                break;
            default:
                DLOGW("Unknown track kind:  %d", localTrackKind);
        }
    }

    return wasFound;
}

// Populate the media sections of a SessionDescription with the current state of the KvsPeerConnection
STATUS sdp_populateSessionDescriptionMedia(PKvsPeerConnection pKvsPeerConnection, PSessionDescription pRemoteSessionDescription,
                                           PSessionDescription pLocalSessionDescription)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PDoubleListNode pCurNode = NULL;
    // #stack 160bytes.
    CHAR certificateFingerprint[CERTIFICATE_FINGERPRINT_LENGTH];
    UINT64 data;
    PKvsRtpTransceiver pKvsRtpTransceiver;
    PCHAR pDtlsRole = NULL;

    // get the certificate.
    CHK_STATUS(dtls_session_getLocalCertificateFingerprint(pKvsPeerConnection->pDtlsSession, certificateFingerprint, CERTIFICATE_FINGERPRINT_LENGTH));

    if (pKvsPeerConnection->isOffer) {
        pDtlsRole = DTLS_ROLE_ACTPASS;
    } else {
        pDtlsRole = DTLS_ROLE_ACTIVE;
        CHK_STATUS(sdp_reorderTransceiverByRemoteDescription(pKvsPeerConnection, pRemoteSessionDescription));
    }
    // setup the streaming section.
    CHK_STATUS(double_list_getHeadNode(pKvsPeerConnection->pTransceivers, &pCurNode));
    while (pCurNode != NULL) {
        CHK_STATUS(double_list_getNodeData(pCurNode, &data));
        pCurNode = pCurNode->pNext;
        pKvsRtpTransceiver = (PKvsRtpTransceiver) data;
        if (pKvsRtpTransceiver != NULL) {
            CHK(pLocalSessionDescription->mediaCount < MAX_SDP_SESSION_MEDIA_COUNT, STATUS_SDP_MAX_MEDIA_COUNT);

            // If generating answer, need to check if Local Description is present in remote -- if not, we don't need to create a local description
            // for it or else our Answer will have an extra m-line, for offer the local is the offer itself, don't care about remote
            if (pKvsPeerConnection->isOffer || sdp_isPresentInRemote(pKvsRtpTransceiver, pRemoteSessionDescription)) {
                CHK_STATUS(sdp_populateSingleMediaSection(
                    pKvsPeerConnection, pKvsRtpTransceiver, &(pLocalSessionDescription->mediaDescriptions[pLocalSessionDescription->mediaCount]),
                    pRemoteSessionDescription, certificateFingerprint, pLocalSessionDescription->mediaCount, pDtlsRole));
                pLocalSessionDescription->mediaCount++;
            }
        }
    }
    // setup the data channel section.
    if (pKvsPeerConnection->sctpIsEnabled) {
        CHK(pLocalSessionDescription->mediaCount < MAX_SDP_SESSION_MEDIA_COUNT, STATUS_SDP_MAX_MEDIA_COUNT);
        CHK_STATUS(sdp_populateSessionDescriptionDataChannel(pKvsPeerConnection,
                                                             &(pLocalSessionDescription->mediaDescriptions[pLocalSessionDescription->mediaCount]),
                                                             certificateFingerprint, pLocalSessionDescription->mediaCount, pDtlsRole));
        pLocalSessionDescription->mediaCount++;
    }

CleanUp:

    LEAVES();
    return retStatus;
}

// Populate a SessionDescription with the current state of the KvsPeerConnection
STATUS sdp_populateSessionDescription(PKvsPeerConnection pKvsPeerConnection, PSessionDescription pRemoteSessionDescription,
                                      PSessionDescription pLocalSessionDescription)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    CHAR bundleValue[MAX_SDP_ATTRIBUTE_VALUE_LENGTH], wmsValue[MAX_SDP_ATTRIBUTE_VALUE_LENGTH];
    PCHAR curr = NULL;
    UINT32 i, sizeRemaining;

    CHK(pKvsPeerConnection != NULL && pLocalSessionDescription != NULL && pRemoteSessionDescription != NULL, STATUS_NULL_ARG);

    CHK_STATUS(sdp_populateSessionDescriptionMedia(pKvsPeerConnection, pRemoteSessionDescription, pLocalSessionDescription));

    MEMSET(bundleValue, 0, MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    MEMSET(wmsValue, 0, MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    /**
     * https://tools.ietf.org/html/rfc4566#section-5.2
     * Origin ==> o=<username> <sess-id> <sess-version> <nettype> <addrtype> <unicast-address>
     */
    STRNCPY(pLocalSessionDescription->sdpOrigin.userName, "-", MAX_SDP_SESSION_USERNAME_LENGTH);
    pLocalSessionDescription->sdpOrigin.sessionId = RAND();
    pLocalSessionDescription->sdpOrigin.sessionVersion = 2;
    STRNCPY(pLocalSessionDescription->sdpOrigin.sdpConnectionInformation.networkType, "IN", MAX_SDP_NETWORK_TYPE_LENGTH);
    STRNCPY(pLocalSessionDescription->sdpOrigin.sdpConnectionInformation.addressType, "IP4", MAX_SDP_ADDRESS_TYPE_LENGTH);
    STRNCPY(pLocalSessionDescription->sdpOrigin.sdpConnectionInformation.connectionAddress, "127.0.0.1", MAX_SDP_CONNECTION_ADDRESS_LENGTH);
    /**
     * https://tools.ietf.org/html/rfc4566#section-5.3
     * Session Name ==> s=<session name>
     */
    STRNCPY(pLocalSessionDescription->sessionName, "-", MAX_SDP_SESSION_NAME_LENGTH);

    pLocalSessionDescription->timeDescriptionCount = 1;
    pLocalSessionDescription->sdpTimeDescription[0].startTime = 0;
    pLocalSessionDescription->sdpTimeDescription[0].stopTime = 0;
    // a=group:BUNDLE mid0 mid1
    // setup the information of bundle media.
    STRNCPY(pLocalSessionDescription->sdpAttributes[0].attributeName, "group", MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pLocalSessionDescription->sdpAttributes[0].attributeValue, BUNDLE_KEY, MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    for (curr = (pLocalSessionDescription->sdpAttributes[0].attributeValue + ARRAY_SIZE(BUNDLE_KEY) - 1), i = 0;
         i < pLocalSessionDescription->mediaCount; i++) {
        STRNCPY(pLocalSessionDescription->mediaDescriptions[i].sdpConnectionInformation.networkType, "IN", MAX_SDP_NETWORK_TYPE_LENGTH);
        STRNCPY(pLocalSessionDescription->mediaDescriptions[i].sdpConnectionInformation.addressType, "IP4", MAX_SDP_ADDRESS_TYPE_LENGTH);
        STRNCPY(pLocalSessionDescription->mediaDescriptions[i].sdpConnectionInformation.connectionAddress, "127.0.0.1",
                MAX_SDP_CONNECTION_ADDRESS_LENGTH);

        sizeRemaining = MAX_SDP_ATTRIBUTE_VALUE_LENGTH - (curr - pLocalSessionDescription->sdpAttributes[0].attributeValue);
        curr += SNPRINTF(curr, sizeRemaining, " %d", i);
    }
    pLocalSessionDescription->sessionAttributesCount++;
    // a=msid-semantic
    // This attribute MUST be present if "a=msid" is used.
    STRNCPY(pLocalSessionDescription->sdpAttributes[pLocalSessionDescription->sessionAttributesCount].attributeName, "msid-semantic",
            MAX_SDP_ATTRIBUTE_NAME_LENGTH);
    STRNCPY(pLocalSessionDescription->sdpAttributes[pLocalSessionDescription->sessionAttributesCount].attributeValue, " WMS myKvsVideoStream",
            MAX_SDP_ATTRIBUTE_VALUE_LENGTH);
    pLocalSessionDescription->sessionAttributesCount++;

CleanUp:

    LEAVES();
    return retStatus;
}

// primarily meant to be used by sdp_reorderTransceiverByRemoteDescription
// Find a Transceiver with n codec, and then copy it to the end of the transceivers
// this allows us to re-order by the order the remote dictates
STATUS sdp_copyTransceiverWithCodec(PKvsPeerConnection pKvsPeerConnection, RTC_CODEC rtcCodec, PBOOL pDidFindCodec)
{
    STATUS retStatus = STATUS_SUCCESS;
    PDoubleListNode pCurNode = NULL;
    PKvsRtpTransceiver pTargetKvsRtpTransceiver = NULL, pKvsRtpTransceiver;
    UINT64 data;

    CHK(pKvsPeerConnection != NULL && pDidFindCodec != NULL, STATUS_SDP_NULL_ARG);

    *pDidFindCodec = FALSE;

    CHK_STATUS(double_list_getHeadNode(pKvsPeerConnection->pTransceivers, &pCurNode));
    while (pCurNode != NULL) {
        CHK_STATUS(double_list_getNodeData(pCurNode, &data));
        pKvsRtpTransceiver = (PKvsRtpTransceiver) data;
        if (pKvsRtpTransceiver != NULL && pKvsRtpTransceiver->sender.track.codec == rtcCodec) {
            pTargetKvsRtpTransceiver = pKvsRtpTransceiver;
            doubleListDeleteNode(pKvsPeerConnection->pTransceivers, pCurNode);
            break;
        }
        pCurNode = pCurNode->pNext;
    }
    if (pTargetKvsRtpTransceiver != NULL) {
        CHK_STATUS(double_list_insertItemTail(pKvsPeerConnection->pTransceivers, (UINT64) pTargetKvsRtpTransceiver));
        *pDidFindCodec = TRUE;
    }

CleanUp:

    return retStatus;
}

STATUS sdp_reorderTransceiverByRemoteDescription(PKvsPeerConnection pKvsPeerConnection, PSessionDescription pRemoteSessionDescription)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 currentMedia, currentAttribute, transceiverCount = 0, tokenLen;
    PSdpMediaDescription pMediaDescription = NULL;
    PCHAR attributeValue, end;
    BOOL supportCodec, foundMediaSectionWithCodec;
    RTC_CODEC rtcCodec;

    // change the order of pKvsPeerConnection->pTransceivers to have the same codec order in pRemoteSessionDescription
    CHK_STATUS(doubleListGetNodeCount(pKvsPeerConnection->pTransceivers, &transceiverCount));

    for (currentMedia = 0; currentMedia < pRemoteSessionDescription->mediaCount; currentMedia++) {
        pMediaDescription = &(pRemoteSessionDescription->mediaDescriptions[currentMedia]);
        foundMediaSectionWithCodec = FALSE;

        // Scan the media section name for any codecs we support
        attributeValue = pMediaDescription->mediaName;

        do {
            if ((end = STRCHR(attributeValue, ' ')) != NULL) {
                tokenLen = (end - attributeValue);
            } else {
                tokenLen = STRLEN(attributeValue);
            }

            if (STRNCMP(DEFAULT_PAYLOAD_MULAW_STR, attributeValue, tokenLen) == 0) {
                supportCodec = TRUE;
                rtcCodec = RTC_CODEC_MULAW;
            } else if (STRNCMP(DEFAULT_PAYLOAD_ALAW_STR, attributeValue, tokenLen) == 0) {
                supportCodec = TRUE;
                rtcCodec = RTC_CODEC_ALAW;
            } else {
                supportCodec = FALSE;
            }

            // find transceiver with rtcCodec and duplicate it at tail
            if (supportCodec) {
                CHK_STATUS(sdp_copyTransceiverWithCodec(pKvsPeerConnection, rtcCodec, &foundMediaSectionWithCodec));
            }
            if (end != NULL) {
                attributeValue = end + 1;
            }
        } while (end != NULL && !foundMediaSectionWithCodec);

        // Scan the media section attributes for codecs we support
        for (currentAttribute = 0; currentAttribute < pMediaDescription->mediaAttributesCount && !foundMediaSectionWithCodec; currentAttribute++) {
            attributeValue = pMediaDescription->sdpAttributes[currentAttribute].attributeValue;

            if (STRSTR(attributeValue, H264_VALUE) != NULL) {
                supportCodec = TRUE;
                rtcCodec = RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE;
            } else if (STRSTR(attributeValue, OPUS_VALUE) != NULL) {
                supportCodec = TRUE;
                rtcCodec = RTC_CODEC_OPUS;
            } else if (STRSTR(attributeValue, MULAW_VALUE) != NULL) {
                supportCodec = TRUE;
                rtcCodec = RTC_CODEC_MULAW;
            } else if (STRSTR(attributeValue, ALAW_VALUE) != NULL) {
                supportCodec = TRUE;
                rtcCodec = RTC_CODEC_ALAW;
            } else if (STRSTR(attributeValue, VP8_VALUE) != NULL) {
                supportCodec = TRUE;
                rtcCodec = RTC_CODEC_VP8;
            } else {
                supportCodec = FALSE;
            }

            // find transceiver with rtcCodec and duplicate it at tail
            if (supportCodec) {
                CHK_STATUS(sdp_copyTransceiverWithCodec(pKvsPeerConnection, rtcCodec, &foundMediaSectionWithCodec));
            }
        }
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS sdp_deserializeRtcIceCandidateInit(PCHAR pJson, UINT32 jsonLen, PRtcIceCandidateInit pRtcIceCandidateInit)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    jsmntok_t* pTokens = NULL;
    jsmn_parser parser;
    INT8 i;
    INT32 tokenCount;

    CHK(pRtcIceCandidateInit != NULL && pJson != NULL, STATUS_SDP_ICE_CANDIDATE_NULL_ARG);
    MEMSET(pRtcIceCandidateInit->candidate, 0x00, MAX_ICE_CANDIDATE_INIT_CANDIDATE_LEN + 1);

    CHK(NULL != (pTokens = (jsmntok_t*) MEMALLOC(MAX_JSON_TOKEN_COUNT * SIZEOF(jsmntok_t))), STATUS_NOT_ENOUGH_MEMORY);

    jsmn_init(&parser);

    tokenCount = jsmn_parse(&parser, pJson, jsonLen, pTokens, MAX_JSON_TOKEN_COUNT);
    CHK(tokenCount > 1, STATUS_JSON_API_CALL_INVALID_RETURN);
    CHK(pTokens[0].type == JSMN_OBJECT, STATUS_SDP_ICE_CANDIDATE_INIT_MALFORMED);

    for (i = 1; i < (tokenCount - 1); i += 2) {
        if (STRNCMP(CANDIDATE_KEY, pJson + pTokens[i].start, ARRAY_SIZE(CANDIDATE_KEY) - 1) == 0) {
            STRNCPY(pRtcIceCandidateInit->candidate, pJson + pTokens[i + 1].start, (pTokens[i + 1].end - pTokens[i + 1].start));
        }
    }

    CHK(pRtcIceCandidateInit->candidate[0] != '\0', STATUS_SDP_ICE_CANDIDATE_MISSING_CANDIDATE);

CleanUp:

    SAFE_MEMFREE(pTokens);
    LEAVES();
    return retStatus;
}

STATUS sdp_setReceiversSsrc(PSessionDescription pRemoteSessionDescription, PDoubleList pTransceivers)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSdpMediaDescription pMediaDescription = NULL;
    BOOL foundSsrc, isVideoMediaSection, isAudioMediaSection, isAudioCodec, isVideoCodec;
    UINT32 currentAttribute, currentMedia, ssrc;
    UINT64 data;
    PDoubleListNode pCurNode = NULL;
    PKvsRtpTransceiver pKvsRtpTransceiver;
    RTC_CODEC codec;
    PCHAR end = NULL;

    for (currentMedia = 0; currentMedia < pRemoteSessionDescription->mediaCount; currentMedia++) {
        pMediaDescription = &(pRemoteSessionDescription->mediaDescriptions[currentMedia]);
        // check this medianame is video or audio.
        isVideoMediaSection = (STRNCMP(pMediaDescription->mediaName, MEDIA_SECTION_VIDEO_VALUE, ARRAY_SIZE(MEDIA_SECTION_VIDEO_VALUE) - 1) == 0);
        isAudioMediaSection = (STRNCMP(pMediaDescription->mediaName, MEDIA_SECTION_AUDIO_VALUE, ARRAY_SIZE(MEDIA_SECTION_AUDIO_VALUE) - 1) == 0);
        foundSsrc = FALSE;
        ssrc = 0;

        if (isVideoMediaSection || isAudioMediaSection) {
            for (currentAttribute = 0; currentAttribute < pMediaDescription->mediaAttributesCount && !foundSsrc; currentAttribute++) {
                if (STRNCMP(pMediaDescription->sdpAttributes[currentAttribute].attributeName, SSRC_KEY,
                            STRLEN(pMediaDescription->sdpAttributes[currentAttribute].attributeName)) == 0) {
                    if ((end = STRCHR(pMediaDescription->sdpAttributes[currentAttribute].attributeValue, ' ')) != NULL) {
                        CHK_STATUS(STRTOUI32(pMediaDescription->sdpAttributes[currentAttribute].attributeValue, end, 10, &ssrc));
                        foundSsrc = TRUE;
                    }
                }
            }

            if (foundSsrc) {
                CHK_STATUS(double_list_getHeadNode(pTransceivers, &pCurNode));
                while (pCurNode != NULL) {
                    CHK_STATUS(double_list_getNodeData(pCurNode, &data));
                    pKvsRtpTransceiver = (PKvsRtpTransceiver) data;
                    codec = pKvsRtpTransceiver->sender.track.codec;
                    isVideoCodec = VIDEO_SUPPPORT_TYPE(codec);
                    isAudioCodec = AUDIO_SUPPORT_TYPE(codec);

                    if (pKvsRtpTransceiver->jitterBufferSsrc == 0 &&
                        ((isVideoCodec && isVideoMediaSection) || (isAudioCodec && isAudioMediaSection))) {
                        // Finish iteration, we assigned the ssrc move on to next media section
                        pKvsRtpTransceiver->jitterBufferSsrc = ssrc;
                        pKvsRtpTransceiver->inboundStats.received.rtpStream.ssrc = ssrc;
                        STRNCPY(pKvsRtpTransceiver->inboundStats.received.rtpStream.kind,
                                pKvsRtpTransceiver->transceiver.receiver.track.kind == MEDIA_STREAM_TRACK_KIND_VIDEO ? "video" : "audio",
                                ARRAY_SIZE(pKvsRtpTransceiver->inboundStats.received.rtpStream.kind));

                        pCurNode = NULL;
                    } else {
                        pCurNode = pCurNode->pNext;
                    }
                }
            }
        }
    }

CleanUp:

    return retStatus;
}
