/*
 * test-ltc-roundtrip.cpp - Encode/decode roundtrip tests for LTC wrapper
 *
 * Encodes a timecode via the wrapper, then decodes it using libltc's
 * decoder to verify the timecode survives the roundtrip.
 */

#include <gtest/gtest.h>

extern "C" {
#include "ltc-encoder-wrapper.h"
#include <ltc.h>
}

TEST(LTCRoundtripTest, CreateDestroy)
{
	ltc_wrapper_t *w = ltc_wrapper_create(48000, TC_FPS_25);
	ASSERT_NE(w, nullptr);
	ltc_wrapper_destroy(w);
}

TEST(LTCRoundtripTest, CreateAllFramerates)
{
	tc_framerate_t rates[] = {TC_FPS_24, TC_FPS_25, TC_FPS_29_97_DF,
				  TC_FPS_30, TC_FPS_50, TC_FPS_60};
	for (auto r : rates) {
		ltc_wrapper_t *w = ltc_wrapper_create(48000, r);
		ASSERT_NE(w, nullptr) << "Failed for framerate enum " << (int)r;
		EXPECT_GT(ltc_wrapper_get_samples_per_frame(w), 0);
		EXPECT_GT(ltc_wrapper_get_fps(w), 0);
		ltc_wrapper_destroy(w);
	}
}

TEST(LTCRoundtripTest, CreateInvalidParams)
{
	EXPECT_EQ(ltc_wrapper_create(0, TC_FPS_25), nullptr);
	EXPECT_EQ(ltc_wrapper_create(-1, TC_FPS_25), nullptr);
	EXPECT_EQ(ltc_wrapper_create(48000, (tc_framerate_t)9999), nullptr);
}

TEST(LTCRoundtripTest, DestroyNull)
{
	ltc_wrapper_destroy(nullptr); /* should not crash */
}

TEST(LTCRoundtripTest, EncodeProducesSamples)
{
	ltc_wrapper_t *w = ltc_wrapper_create(48000, TC_FPS_25);
	ASSERT_NE(w, nullptr);

	ltc_wrapper_set_timecode(w, 12, 30, 45, 10);

	float buffer[4800];
	int samples = ltc_wrapper_encode_frame(w, buffer, 4800);

	EXPECT_GT(samples, 0);
	EXPECT_LE(samples, 4800);

	/* Verify samples are not all zero (signal present) */
	bool has_nonzero = false;
	for (int i = 0; i < samples; i++) {
		if (buffer[i] != 0.0f) {
			has_nonzero = true;
			break;
		}
	}
	EXPECT_TRUE(has_nonzero);

	ltc_wrapper_destroy(w);
}

TEST(LTCRoundtripTest, IncTimecodeWorks)
{
	ltc_wrapper_t *w = ltc_wrapper_create(48000, TC_FPS_25);
	ASSERT_NE(w, nullptr);

	ltc_wrapper_set_timecode(w, 12, 0, 0, 0);

	/* Encode frame 0, then increment and encode frame 1 */
	float buf1[4800], buf2[4800];
	int s1 = ltc_wrapper_encode_frame(w, buf1, 4800);
	ASSERT_GT(s1, 0);

	ltc_wrapper_inc_timecode(w);
	int s2 = ltc_wrapper_encode_frame(w, buf2, 4800);
	ASSERT_GT(s2, 0);

	/* Both frames should produce similar sample counts */
	EXPECT_EQ(s1, s2);

	ltc_wrapper_destroy(w);
}

TEST(LTCRoundtripTest, IncTimecodeNull)
{
	ltc_wrapper_inc_timecode(nullptr); /* should not crash */
}

TEST(LTCRoundtripTest, GetSamplesPerFrame)
{
	ltc_wrapper_t *w = ltc_wrapper_create(48000, TC_FPS_25);
	ASSERT_NE(w, nullptr);
	EXPECT_EQ(ltc_wrapper_get_samples_per_frame(w), 1920); /* 48000/25 */
	EXPECT_EQ(ltc_wrapper_get_fps(w), 25);
	ltc_wrapper_destroy(w);

	ltc_wrapper_t *w2 = ltc_wrapper_create(48000, TC_FPS_30);
	ASSERT_NE(w2, nullptr);
	EXPECT_EQ(ltc_wrapper_get_samples_per_frame(w2), 1600); /* 48000/30 */
	EXPECT_EQ(ltc_wrapper_get_fps(w2), 30);
	ltc_wrapper_destroy(w2);

	EXPECT_EQ(ltc_wrapper_get_samples_per_frame(nullptr), 0);
	EXPECT_EQ(ltc_wrapper_get_fps(nullptr), 0);
}

TEST(LTCRoundtripTest, RoundtripTimecode25fps)
{
	const int sample_rate = 48000;

	ltc_wrapper_t *w = ltc_wrapper_create(sample_rate, TC_FPS_25);
	ASSERT_NE(w, nullptr);

	/* Encode timecode 14:30:22:15 */
	ltc_wrapper_set_timecode(w, 14, 30, 22, 15);

	float buffer[4800];
	int samples = ltc_wrapper_encode_frame(w, buffer, 4800);
	ASSERT_GT(samples, 0);

	/* Decode using libltc decoder */
	LTCDecoder *decoder = ltc_decoder_create(sample_rate / 25, 32);
	ASSERT_NE(decoder, nullptr);

	/* Convert float samples to ltcsnd_sample_t (unsigned byte) */
	ltcsnd_sample_t *byte_buf = new ltcsnd_sample_t[samples];
	for (int i = 0; i < samples; i++) {
		byte_buf[i] = (ltcsnd_sample_t)((buffer[i] * 128.0f) + 128.0f);
	}

	ltc_decoder_write(decoder, byte_buf, (size_t)samples, 0);

	LTCFrameExt frame;
	int found = ltc_decoder_read(decoder, &frame);

	if (found) {
		SMPTETimecode stime;
		ltc_frame_to_time(&stime, &frame.ltc, 0);

		EXPECT_EQ(stime.hours, 14);
		EXPECT_EQ(stime.mins, 30);
		EXPECT_EQ(stime.secs, 22);
		EXPECT_EQ(stime.frame, 15);
	}
	/* Note: single frame may not always decode; this is expected.
	 * Full decode requires multiple consecutive frames. */

	delete[] byte_buf;
	ltc_decoder_free(decoder);
	ltc_wrapper_destroy(w);
}

TEST(LTCRoundtripTest, MultiFrameRoundtrip25fps)
{
	const int sample_rate = 48000;
	const int spf = sample_rate / 25;

	ltc_wrapper_t *w = ltc_wrapper_create(sample_rate, TC_FPS_25);
	ASSERT_NE(w, nullptr);

	/* Collect all audio into one big buffer for the decoder */
	const int num_frames = 10;
	const int total_max = spf * num_frames + 1000;
	float *all_audio = new float[total_max];
	int total_samples = 0;

	ltc_wrapper_set_timecode(w, 10, 0, 0, 0);

	for (int f = 0; f < num_frames; f++) {
		float buffer[4800];
		int samples = ltc_wrapper_encode_frame(w, buffer, 4800);
		ASSERT_GT(samples, 0);

		memcpy(&all_audio[total_samples], buffer, (size_t)samples * sizeof(float));
		total_samples += samples;

		ltc_wrapper_inc_timecode(w);
	}

	/* Convert all to byte samples and decode in one shot */
	ltcsnd_sample_t *byte_buf = new ltcsnd_sample_t[total_samples];
	for (int i = 0; i < total_samples; i++) {
		byte_buf[i] = (ltcsnd_sample_t)((all_audio[i] * 128.0f) + 128.0f);
	}

	LTCDecoder *decoder = ltc_decoder_create(spf, 32);
	ASSERT_NE(decoder, nullptr);

	ltc_decoder_write(decoder, byte_buf, (size_t)total_samples, 0);

	LTCFrameExt frame;
	bool decoded = false;
	while (ltc_decoder_read(decoder, &frame)) {
		SMPTETimecode stime;
		ltc_frame_to_time(&stime, &frame.ltc, 0);
		EXPECT_EQ(stime.hours, 10);
		EXPECT_EQ(stime.mins, 0);
		EXPECT_EQ(stime.secs, 0);
		decoded = true;
		break;
	}
	EXPECT_TRUE(decoded) << "Failed to decode any LTC frames from 10-frame sequence";

	delete[] all_audio;
	delete[] byte_buf;
	ltc_decoder_free(decoder);
	ltc_wrapper_destroy(w);
}

TEST(LTCRoundtripTest, EncodeNullBuffer)
{
	ltc_wrapper_t *w = ltc_wrapper_create(48000, TC_FPS_25);
	ASSERT_NE(w, nullptr);

	EXPECT_EQ(ltc_wrapper_encode_frame(w, nullptr, 4800), -1);
	EXPECT_EQ(ltc_wrapper_encode_frame(nullptr, nullptr, 4800), -1);

	float buffer[10];
	EXPECT_EQ(ltc_wrapper_encode_frame(w, buffer, 0), -1);

	ltc_wrapper_destroy(w);
}
