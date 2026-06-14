/*
 * obs-ltc-timecode - NTP-synced LTC timecode audio source for OBS Studio
 * Copyright (C) 2024-2026 Ferdmusic
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Unit tests for mw-recording-helpers (TICKET-037).
 *
 * These exercise the wire-contract helpers without OBS or libcurl in scope:
 *   - mw_build_heartbeat_body: shape + content of the JSON sent to the server
 *   - mw_response_has_resync: substring detection of the director-issued kick
 */

#include <gtest/gtest.h>

extern "C" {
#include "mw-recording-helpers.h"
}

#include <cstring>
#include <string>

/* =================================================================
 * mw_json_escape_string — user strings must never break the JSON body
 * ================================================================= */

TEST(JsonEscapeString, PlainStringPassesThrough)
{
	char buf[64];
	EXPECT_EQ(mw_json_escape_string(buf, sizeof(buf), "Kamera Anna_3"), 13);
	EXPECT_STREQ(buf, "Kamera Anna_3");
}

TEST(JsonEscapeString, QuotesAndBackslashesEscaped)
{
	char buf[64];
	ASSERT_GT(mw_json_escape_string(buf, sizeof(buf), "Max \"Maxi\" M\\W"), 0);
	EXPECT_STREQ(buf, "Max \\\"Maxi\\\" M\\\\W");
}

TEST(JsonEscapeString, ControlCharactersEscaped)
{
	char buf[64];
	ASSERT_GT(mw_json_escape_string(buf, sizeof(buf), "a\nb\tc\x01"), 0);
	EXPECT_STREQ(buf, "a\\nb\\tc\\u0001");
}

TEST(JsonEscapeString, Utf8PassesThroughUnchanged)
{
	char buf[64];
	ASSERT_GT(mw_json_escape_string(buf, sizeof(buf), "K\xc3\xa4the \xe2\x80\x94 B"), 0);
	EXPECT_STREQ(buf, "K\xc3\xa4the \xe2\x80\x94 B");
}

TEST(JsonEscapeString, NullSourceIsEmptyString)
{
	char buf[8];
	EXPECT_EQ(mw_json_escape_string(buf, sizeof(buf), nullptr), 0);
	EXPECT_STREQ(buf, "");
}

TEST(JsonEscapeString, TruncationReturnsMinusOneButTerminates)
{
	char buf[4];
	EXPECT_EQ(mw_json_escape_string(buf, sizeof(buf), "abcdef"), -1);
	EXPECT_EQ(buf[3], '\0'); /* still a valid C string */
	EXPECT_EQ(mw_json_escape_string(nullptr, 0, "x"), -1);
}

TEST(BuildHeartbeatBody, QuoteInNameProducesValidEscapedJson)
{
	/* The field bug this guards: a display name with a quote made the
	 * whole heartbeat invalid JSON — the server 400'd it before its
	 * name sanitization ran, and the camera silently vanished from the
	 * dashboard. */
	char buf[1024];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "Max \"Maxi\" M", true, false, 0, 0, false, 0, -1, nullptr,
					false, 0, 0, 0);
	ASSERT_GT(n, 0);
	std::string s(buf);
	EXPECT_NE(s.find("\"name\":\"Max \\\"Maxi\\\" M\""), std::string::npos);
	/* The unescaped inner quote must not appear anywhere. */
	EXPECT_EQ(s.find("\"Maxi\""), std::string::npos);
}

/* =================================================================
 * mw_build_heartbeat_body — JSON shape with offset present
 * ================================================================= */

TEST(BuildHeartbeatBody, IncludesAllFieldsWhenHaveOffsetTrue)
{
	char buf[512];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "Alice",
					/* recording_active */ true,
					/* have_offset */ true,
					/* offset_ms */ 42,
					/* sync_method */ 1,
					/* synced */ true,
					/* raw_offset_ms */ 42,
					/* offset_age_sec */ 5,
					/* plugin_version */ nullptr,
					/* sync_lost_in_session */ false, 0, 0, 0);
	ASSERT_GT(n, 0);
	std::string s(buf);

	EXPECT_NE(s.find("\"name\":\"Alice\""), std::string::npos);
	EXPECT_NE(s.find("\"recording_active\":true"), std::string::npos);
	EXPECT_NE(s.find("\"offset_ms\":42"), std::string::npos);
	EXPECT_NE(s.find("\"sync_method\":1"), std::string::npos);
	EXPECT_NE(s.find("\"synced\":true"), std::string::npos);
	EXPECT_NE(s.find("\"raw_offset_ms\":42"), std::string::npos);
	EXPECT_NE(s.find("\"offset_age_sec\":5"), std::string::npos);
}

TEST(BuildHeartbeatBody, DiagnosticFieldsPresentWithOffset)
{
	/* TICKET-075: rtt_ms / applied_delta_ms / initial_skew_ms ride in
	 * the have_offset block so the developer can remote-diagnose line
	 * quality, live TC error and boot-time clock skew per camera. */
	char buf[1024];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "Alice", true, true, 42, 1, true, 42, 5, nullptr, false,
					/* rtt_ms */ 18,
					/* applied_delta_ms */ -3,
					/* initial_skew_ms */ 50000);
	ASSERT_GT(n, 0);
	std::string s(buf);
	EXPECT_NE(s.find("\"rtt_ms\":18"), std::string::npos);
	EXPECT_NE(s.find("\"applied_delta_ms\":-3"), std::string::npos);
	EXPECT_NE(s.find("\"initial_skew_ms\":50000"), std::string::npos);
}

TEST(BuildHeartbeatBody, DiagnosticFieldsOmittedWithoutOffset)
{
	char buf[1024];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "Bob", false, false, 0, 0, false, 0, -1, nullptr, false, 18,
					-3, 50000);
	ASSERT_GT(n, 0);
	std::string s(buf);
	EXPECT_EQ(s.find("rtt_ms"), std::string::npos);
	EXPECT_EQ(s.find("applied_delta_ms"), std::string::npos);
	EXPECT_EQ(s.find("initial_skew_ms"), std::string::npos);
}

TEST(BuildHeartbeatBody, OmitsOffsetFieldsWhenHaveOffsetFalse)
{
	char buf[512];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "Bob",
					/* recording_active */ false,
					/* have_offset */ false,
					/* offset_ms */ 999, /* must be ignored */
					/* sync_method */ 3, /* must be ignored */
					/* synced */ true,   /* must be ignored */
					/* raw_offset_ms */ 999,
					/* offset_age_sec */ 99,
					/* plugin_version */ nullptr,
					/* sync_lost_in_session */ false, 0, 0, 0);
	ASSERT_GT(n, 0);
	std::string s(buf);

	EXPECT_NE(s.find("\"name\":\"Bob\""), std::string::npos);
	EXPECT_NE(s.find("\"recording_active\":false"), std::string::npos);
	EXPECT_EQ(s.find("offset_ms"), std::string::npos);
	EXPECT_EQ(s.find("sync_method"), std::string::npos);
	EXPECT_EQ(s.find("synced"), std::string::npos);
	EXPECT_EQ(s.find("raw_offset_ms"), std::string::npos);
	EXPECT_EQ(s.find("offset_age_sec"), std::string::npos);
	/* Must still be a valid JSON object */
	EXPECT_EQ(s.front(), '{');
	EXPECT_EQ(s.back(), '}');
}

TEST(BuildHeartbeatBody, RecordingActiveFormattedAsJsonBool)
{
	char buf[256];

	mw_build_heartbeat_body(buf, sizeof(buf), "X", true, false, 0, 0, false, 0, 0, nullptr, false, 0, 0, 0);
	EXPECT_NE(std::string(buf).find("\"recording_active\":true"), std::string::npos);
	EXPECT_EQ(std::string(buf).find("\"recording_active\":1"), std::string::npos); /* not numeric */

	mw_build_heartbeat_body(buf, sizeof(buf), "X", false, false, 0, 0, false, 0, 0, nullptr, false, 0, 0, 0);
	EXPECT_NE(std::string(buf).find("\"recording_active\":false"), std::string::npos);
}

TEST(BuildHeartbeatBody, NegativeOffsetSerializedCorrectly)
{
	char buf[256];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "Y", true, true, -1234, 2, true, -1234, 0, nullptr, false, 0,
					0, 0);
	ASSERT_GT(n, 0);
	EXPECT_NE(std::string(buf).find("\"offset_ms\":-1234"), std::string::npos);
	EXPECT_NE(std::string(buf).find("\"raw_offset_ms\":-1234"), std::string::npos);
}

TEST(BuildHeartbeatBody, LargeOffsetSerializedCorrectly)
{
	char buf[256];
	/* 5 minutes drift in ms — close to the worst case seen in the field */
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "Z", true, true, 300000, 1, true, 300000, 12, nullptr, false,
					0, 0, 0);
	ASSERT_GT(n, 0);
	EXPECT_NE(std::string(buf).find("\"offset_ms\":300000"), std::string::npos);
}

TEST(BuildHeartbeatBody, AllSyncMethodsRoundtripAsIntegers)
{
	char buf[256];
	for (int m = 0; m <= 3; m++) {
		mw_build_heartbeat_body(buf, sizeof(buf), "n", true, true, 0, m, false, 0, 0, nullptr, false, 0, 0, 0);
		std::string expect = "\"sync_method\":" + std::to_string(m);
		EXPECT_NE(std::string(buf).find(expect), std::string::npos)
			<< "sync_method " << m << " missing in: " << buf;
	}
}

TEST(BuildHeartbeatBody, ReturnsMinusOneOnTruncation)
{
	char tiny[8]; /* nowhere near enough */
	int n = mw_build_heartbeat_body(tiny, sizeof(tiny), "VeryLongUserName", true, true, 999999, 1, true, 999999, 0,
					nullptr, false, 0, 0, 0);
	EXPECT_EQ(n, -1);
}

TEST(BuildHeartbeatBody, NullBufRejected)
{
	int n = mw_build_heartbeat_body(nullptr, 100, "x", true, false, 0, 0, false, 0, 0, nullptr, false, 0, 0, 0);
	EXPECT_EQ(n, -1);
}

TEST(BuildHeartbeatBody, ZeroBufSizeRejected)
{
	char buf[16];
	int n = mw_build_heartbeat_body(buf, 0, "x", true, false, 0, 0, false, 0, 0, nullptr, false, 0, 0, 0);
	EXPECT_EQ(n, -1);
}

TEST(BuildHeartbeatBody, NullNameBecomesEmptyString)
{
	char buf[256];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), nullptr, true, false, 0, 0, false, 0, 0, nullptr, false, 0, 0,
					0);
	ASSERT_GT(n, 0);
	EXPECT_NE(std::string(buf).find("\"name\":\"\""), std::string::npos);
}

TEST(BuildHeartbeatBody, RawOffsetCanDifferFromSlewedOffset)
{
	/* The wire-contract allows the slewed (applied) and raw values to
	 * differ — useful when the dashboard wants to render both. Today the
	 * plugin reports raw in both fields, but the helper must not enforce
	 * equality. */
	char buf[512];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "n", true, true,
					/* offset_ms (applied) */ 250,
					/* sync_method */ 1,
					/* synced */ true,
					/* raw_offset_ms */ 5000,
					/* offset_age_sec */ 7,
					/* plugin_version */ nullptr,
					/* sync_lost_in_session */ false, 0, 0, 0);
	ASSERT_GT(n, 0);
	std::string s(buf);
	EXPECT_NE(s.find("\"offset_ms\":250"), std::string::npos);
	EXPECT_NE(s.find("\"raw_offset_ms\":5000"), std::string::npos);
	EXPECT_NE(s.find("\"offset_age_sec\":7"), std::string::npos);
}

TEST(BuildHeartbeatBody, OffsetAgeSecMinusOneIsValid)
{
	/* age = -1 means no sync has succeeded yet — must serialise as -1
	 * (NOT as a missing key — the dashboard expects the field present). */
	char buf[256];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "n", false, true, 0, 0, false, 0, -1, nullptr, false, 0, 0,
					0);
	ASSERT_GT(n, 0);
	EXPECT_NE(std::string(buf).find("\"offset_age_sec\":-1"), std::string::npos);
}

TEST(BuildHeartbeatBody, OffsetAgeSecZeroBoundary)
{
	/* age = 0 (sync less than 1 s ago) must serialise as 0, not omit. */
	char buf[256];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "n", true, true, 42, 1, true, 42, 0, nullptr, false, 0, 0, 0);
	ASSERT_GT(n, 0);
	EXPECT_NE(std::string(buf).find("\"offset_age_sec\":0"), std::string::npos);
}

/* =================================================================
 * TICKET-047: plugin_version field
 * ================================================================= */

TEST(BuildHeartbeatBody, IncludesPluginVersionWhenProvided)
{
	char buf[512];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "Alice", true, true, 42, 1, true, 42, 5, "0.6.0", false, 0, 0,
					0);
	ASSERT_GT(n, 0);
	EXPECT_NE(std::string(buf).find("\"plugin_version\":\"0.6.0\""), std::string::npos);
}

TEST(BuildHeartbeatBody, OmitsPluginVersionWhenNullOrEmpty)
{
	char buf[512];
	mw_build_heartbeat_body(buf, sizeof(buf), "Bob", true, true, 0, 0, false, 0, 0, nullptr, false, 0, 0, 0);
	EXPECT_EQ(std::string(buf).find("plugin_version"), std::string::npos);

	mw_build_heartbeat_body(buf, sizeof(buf), "Bob", true, true, 0, 0, false, 0, 0, "", false, 0, 0, 0);
	EXPECT_EQ(std::string(buf).find("plugin_version"), std::string::npos);
}

TEST(BuildHeartbeatBody, PluginVersionWorksWithoutOffsetFields)
{
	/* recording_active without offset must still carry plugin_version
	 * (e.g. very first heartbeat before NTP succeeded). */
	char buf[512];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "Carol", true, false, 0, 0, false, 0, 0, "0.6.0", false, 0, 0,
					0);
	ASSERT_GT(n, 0);
	std::string s(buf);
	EXPECT_NE(s.find("\"name\":\"Carol\""), std::string::npos);
	EXPECT_NE(s.find("\"plugin_version\":\"0.6.0\""), std::string::npos);
	EXPECT_EQ(s.find("offset_ms"), std::string::npos);
}

TEST(BuildHeartbeatBody, PluginVersionTruncationReturnsMinusOne)
{
	/* If the version string pushes the JSON past the buffer, snprintf
	 * truncation must be reported. */
	char tiny[64];
	int n = mw_build_heartbeat_body(tiny, sizeof(tiny), "VeryLongUserName", true, true, 999999, 1, true, 999999, 0,
					"0.999.999-prerelease+long", false, 0, 0, 0);
	EXPECT_EQ(n, -1);
}

/* =================================================================
 * TICKET-043: sync_lost_in_session field
 * ================================================================= */

TEST(BuildHeartbeatBody, SyncLostFieldAlwaysPresentInOffsetForm)
{
	/* Even when false, the field must be in the JSON so the dashboard can
	 * clear a previously shown red marker. */
	char buf[512];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "Dora", true, true, 42, 1, true, 42, 5, nullptr,
					/* sync_lost */ false, 0, 0, 0);
	ASSERT_GT(n, 0);
	EXPECT_NE(std::string(buf).find("\"sync_lost_in_session\":false"), std::string::npos);
}

TEST(BuildHeartbeatBody, SyncLostFieldAlwaysPresentInMinimalForm)
{
	/* No offset, no version, still must carry sync_lost. */
	char buf[256];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "Eve", true, false, 0, 0, false, 0, 0, nullptr,
					/* sync_lost */ false, 0, 0, 0);
	ASSERT_GT(n, 0);
	EXPECT_NE(std::string(buf).find("\"sync_lost_in_session\":false"), std::string::npos);
}

TEST(BuildHeartbeatBody, SyncLostTrueSerializesAsTrue)
{
	char buf[512];
	int n = mw_build_heartbeat_body(buf, sizeof(buf), "Fred", true, true, 0, 0, false, 0, 0, "0.6.0",
					/* sync_lost */ true, 0, 0, 0);
	ASSERT_GT(n, 0);
	std::string s(buf);
	EXPECT_NE(s.find("\"sync_lost_in_session\":true"), std::string::npos);
	EXPECT_NE(s.find("\"plugin_version\":\"0.6.0\""), std::string::npos);
}

/* =================================================================
 * mw_response_has_resync — substring detection
 * ================================================================= */

TEST(ResponseHasResync, DetectsCompactTrueForm)
{
	EXPECT_TRUE(mw_response_has_resync("{\"ok\":true,\"resync\":true}"));
}

TEST(ResponseHasResync, NotDetectedWhenAbsent)
{
	EXPECT_FALSE(mw_response_has_resync("{\"ok\":true,\"updated\":1}"));
}

TEST(ResponseHasResync, NotDetectedWhenFalse)
{
	EXPECT_FALSE(mw_response_has_resync("{\"ok\":true,\"resync\":false}"));
}

TEST(ResponseHasResync, ToleratesWhitespaceAfterColon)
{
	EXPECT_TRUE(mw_response_has_resync("{\"ok\":true,\"resync\": true}"));
	EXPECT_TRUE(mw_response_has_resync("{\"ok\":true,\"resync\":  true}"));
	EXPECT_TRUE(mw_response_has_resync("{\"ok\":true,\"resync\":\ttrue}"));
}

TEST(ResponseHasResync, ToleratesNewlinesAndCarriageReturns)
{
	EXPECT_TRUE(mw_response_has_resync("{\n  \"ok\": true,\n  \"resync\": true\n}"));
	EXPECT_TRUE(mw_response_has_resync("{\"resync\":\r\ntrue}"));
}

TEST(ResponseHasResync, NullInputReturnsFalse)
{
	EXPECT_FALSE(mw_response_has_resync(nullptr));
}

TEST(ResponseHasResync, EmptyInputReturnsFalse)
{
	EXPECT_FALSE(mw_response_has_resync(""));
}

TEST(ResponseHasResync, NotConfusedByTrueishSuffix)
{
	/* "resync":truely is NOT a match — defensive against weird future
	 * JSON variants */
	EXPECT_FALSE(mw_response_has_resync("{\"resync\":truely}"));
	EXPECT_FALSE(mw_response_has_resync("{\"resync\":true_value}"));
}

TEST(ResponseHasResync, NotConfusedByOtherKeysContainingResync)
{
	/* A key called "no_resync_needed" must not trigger */
	EXPECT_FALSE(mw_response_has_resync("{\"no_resync_needed\":true,\"ok\":true}"));
}

TEST(ResponseHasResync, DetectsAtEndOfBufferWithoutBrace)
{
	/* If response was truncated by the capture buffer, the closing brace
	 * might be missing. Still need to detect the resync flag. */
	EXPECT_TRUE(mw_response_has_resync("{\"ok\":true,\"resync\":true"));
}

TEST(ResponseHasResync, DetectsWhenResyncIsFirstKey)
{
	EXPECT_TRUE(mw_response_has_resync("{\"resync\":true,\"ok\":true}"));
}
