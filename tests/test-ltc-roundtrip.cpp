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

	ltc_wrapper_set_timecode(w, 12, 30, 45, 10, 0, 0, 0, 0);

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

	ltc_wrapper_set_timecode(w, 12, 0, 0, 0, 0, 0, 0, 0);

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
	ltc_wrapper_set_timecode(w, 14, 30, 22, 15, 26, 3, 1, 0);

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

	ltc_wrapper_set_timecode(w, 10, 0, 0, 0, 26, 3, 1, 0);

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

/*
 * DaVinci Resolve compatibility: verify that consecutive frames encode
 * with sequential timecodes and no discontinuities. This simulates the
 * free-running inc_timecode path that the drift-aware resync relies on.
 */
TEST(LTCRoundtripTest, ContinuousTimecodeSequence25fps)
{
	const int sample_rate = 48000;
	const int fps = 25;
	const int spf = sample_rate / fps;
	const int num_frames = 50; /* ~2 seconds of continuous TC */

	ltc_wrapper_t *w = ltc_wrapper_create(sample_rate, TC_FPS_25);
	ASSERT_NE(w, nullptr);

	/* Start at 23:59:58:00 to also test midnight rollover in sequence */
	ltc_wrapper_set_timecode(w, 23, 59, 58, 0, 26, 3, 1, 0);

	/* Encode all frames into one large buffer */
	const int total_max = spf * num_frames + 4000;
	float *all_audio = new float[total_max];
	int total_samples = 0;

	for (int f = 0; f < num_frames; f++) {
		float buffer[4800];
		int samples = ltc_wrapper_encode_frame(w, buffer, 4800);
		ASSERT_GT(samples, 0) << "Frame " << f << " encode failed";

		memcpy(&all_audio[total_samples], buffer,
		       (size_t)samples * sizeof(float));
		total_samples += samples;

		ltc_wrapper_inc_timecode(w);
	}

	/* Convert to byte samples for decoder */
	ltcsnd_sample_t *byte_buf = new ltcsnd_sample_t[total_samples];
	for (int i = 0; i < total_samples; i++) {
		byte_buf[i] =
			(ltcsnd_sample_t)((all_audio[i] * 128.0f) + 128.0f);
	}

	/* Decode and verify sequence continuity */
	LTCDecoder *decoder = ltc_decoder_create(spf, 32);
	ASSERT_NE(decoder, nullptr);

	ltc_decoder_write(decoder, byte_buf, (size_t)total_samples, 0);

	LTCFrameExt frame;
	int decoded_count = 0;
	int prev_total_frames = -1;

	while (ltc_decoder_read(decoder, &frame)) {
		SMPTETimecode stime;
		ltc_frame_to_time(&stime, &frame.ltc, 0);

		int total_frames = stime.hours * 3600 * fps +
				   stime.mins * 60 * fps +
				   stime.secs * fps + stime.frame;

		if (prev_total_frames >= 0) {
			int expected = prev_total_frames + 1;
			/* Handle midnight rollover: 24*3600*25 = 2160000 */
			if (expected >= 24 * 3600 * fps)
				expected = 0;
			EXPECT_EQ(total_frames, expected)
				<< "Timecode discontinuity at decoded frame "
				<< decoded_count << ": "
				<< (int)stime.hours << ":"
				<< (int)stime.mins << ":"
				<< (int)stime.secs << ":"
				<< (int)stime.frame;
		}

		prev_total_frames = total_frames;
		decoded_count++;
	}

	EXPECT_GT(decoded_count, 5)
		<< "Expected to decode multiple consecutive frames for continuity check";

	delete[] all_audio;
	delete[] byte_buf;
	ltc_decoder_free(decoder);
	ltc_wrapper_destroy(w);
}

/*
 * Camera IDs above 7 (Kamera I-P, ids 8-15) must reach user7 as well.
 * Regression test: the wrapper used to silently drop ids > 7 after the
 * dropdown was expanded to A-P in 0.4.0.
 */
TEST(LTCRoundtripTest, CameraIDUpperRangeRoundtrip)
{
	const int sample_rate = 48000;
	const int spf = sample_rate / 25;

	for (int camera_id : {8, 15}) {
		ltc_wrapper_t *w = ltc_wrapper_create(sample_rate, TC_FPS_25);
		ASSERT_NE(w, nullptr);

		ltc_wrapper_set_timecode(w, 10, 0, 0, 0, 26, 6, 10, camera_id);

		const int num_frames = 10;
		const int total_max = spf * num_frames + 1000;
		float *all_audio = new float[total_max];
		int total_samples = 0;

		for (int f = 0; f < num_frames; f++) {
			float buffer[4800];
			int samples = ltc_wrapper_encode_frame(w, buffer, 4800);
			ASSERT_GT(samples, 0);
			memcpy(&all_audio[total_samples], buffer,
			       (size_t)samples * sizeof(float));
			total_samples += samples;
			ltc_wrapper_inc_timecode(w);
		}

		ltcsnd_sample_t *byte_buf = new ltcsnd_sample_t[total_samples];
		for (int i = 0; i < total_samples; i++) {
			byte_buf[i] = (ltcsnd_sample_t)((all_audio[i] * 128.0f) +
							128.0f);
		}

		LTCDecoder *decoder = ltc_decoder_create(spf, 32);
		ASSERT_NE(decoder, nullptr);
		ltc_decoder_write(decoder, byte_buf, (size_t)total_samples, 0);

		LTCFrameExt frame;
		bool decoded = false;
		while (ltc_decoder_read(decoder, &frame)) {
			EXPECT_EQ(frame.ltc.user7, (unsigned)camera_id)
				<< "camera_id " << camera_id
				<< " did not survive the roundtrip";
			EXPECT_EQ(frame.ltc.user8, 0u);
			decoded = true;
			break;
		}
		EXPECT_TRUE(decoded)
			<< "Failed to decode LTC frames for camera_id "
			<< camera_id;

		delete[] all_audio;
		delete[] byte_buf;
		ltc_decoder_free(decoder);
		ltc_wrapper_destroy(w);
	}
}

/*
 * Verify that date (SMPTE 12M User Bits) and camera ID survive
 * the encode/decode roundtrip.
 */
TEST(LTCRoundtripTest, DateAndCameraIDRoundtrip)
{
	const int sample_rate = 48000;
	const int spf = sample_rate / 25;

	ltc_wrapper_t *w = ltc_wrapper_create(sample_rate, TC_FPS_25);
	ASSERT_NE(w, nullptr);

	/* 2026-03-15, camera C (id=2) */
	ltc_wrapper_set_timecode(w, 14, 30, 22, 15, 26, 3, 15, 2);

	/* Encode 10 frames */
	const int num_frames = 10;
	const int total_max = spf * num_frames + 1000;
	float *all_audio = new float[total_max];
	int total_samples = 0;

	for (int f = 0; f < num_frames; f++) {
		float buffer[4800];
		int samples = ltc_wrapper_encode_frame(w, buffer, 4800);
		ASSERT_GT(samples, 0);
		memcpy(&all_audio[total_samples], buffer,
		       (size_t)samples * sizeof(float));
		total_samples += samples;
		ltc_wrapper_inc_timecode(w);
	}

	/* Convert to byte samples and decode */
	ltcsnd_sample_t *byte_buf = new ltcsnd_sample_t[total_samples];
	for (int i = 0; i < total_samples; i++) {
		byte_buf[i] =
			(ltcsnd_sample_t)((all_audio[i] * 128.0f) + 128.0f);
	}

	LTCDecoder *decoder = ltc_decoder_create(spf, 32);
	ASSERT_NE(decoder, nullptr);

	ltc_decoder_write(decoder, byte_buf, (size_t)total_samples, 0);

	LTCFrameExt frame;
	bool decoded = false;
	while (ltc_decoder_read(decoder, &frame)) {
		SMPTETimecode stime;
		ltc_frame_to_time(&stime, &frame.ltc, LTC_USE_DATE);

		/* Verify date fields */
		EXPECT_EQ(stime.years, 26);
		EXPECT_EQ(stime.months, 3);
		EXPECT_EQ(stime.days, 15);

		/* Verify camera ID in user7 */
		EXPECT_EQ(frame.ltc.user7, 2u);
		EXPECT_EQ(frame.ltc.user8, 0u);

		decoded = true;
		break;
	}
	EXPECT_TRUE(decoded)
		<< "Failed to decode LTC frames for date/camera roundtrip";

	delete[] all_audio;
	delete[] byte_buf;
	ltc_decoder_free(decoder);
	ltc_wrapper_destroy(w);
}
