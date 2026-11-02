/*
 * obs-ltc-timecode - NTP-synced LTC timecode audio source for OBS Studio
 * Copyright (C) 2024-2026 Ferdmusic
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Unit tests for audio-monitor-core (TICKET-081).
 */

#include <gtest/gtest.h>

extern "C" {
#include "audio-monitor-core.h"
}

/* =================================================================
 * am_block_has_signal
 * ================================================================= */

TEST(BlockHasSignal, DigitalSilenceHasNone)
{
	const float block[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	EXPECT_FALSE(am_block_has_signal(block, 4, AM_SIGNAL_THRESHOLD));
}

TEST(BlockHasSignal, BelowThresholdHasNone)
{
	const float block[2] = {0.00005f, -0.00005f};
	EXPECT_FALSE(am_block_has_signal(block, 2, AM_SIGNAL_THRESHOLD));
}

TEST(BlockHasSignal, PositiveAndNegativePeaksCount)
{
	const float positive[2] = {0.0f, 0.5f};
	const float negative[2] = {0.0f, -0.5f};
	EXPECT_TRUE(am_block_has_signal(positive, 2, AM_SIGNAL_THRESHOLD));
	EXPECT_TRUE(am_block_has_signal(negative, 2, AM_SIGNAL_THRESHOLD));
}

TEST(BlockHasSignal, NullPlaneHasNone)
{
	EXPECT_FALSE(am_block_has_signal(nullptr, 480, AM_SIGNAL_THRESHOLD));
}

/* =================================================================
 * am_state_update
 * ================================================================= */

static am_source_state fresh_state()
{
	am_source_state state;
	am_state_init(&state, 0);
	return state;
}

TEST(StateUpdate, NoWarningBeforeLimit)
{
	am_source_state state = fresh_state();
	EXPECT_EQ(am_state_update(&state, AM_LIMIT_SOURCE_MS - 1, 0, false, false, AM_LIMIT_SOURCE_MS), AM_EVENT_NONE);
}

TEST(StateUpdate, SourceWithoutSoundWarnsAtLimit)
{
	am_source_state state = fresh_state();
	EXPECT_EQ(am_state_update(&state, AM_LIMIT_SOURCE_MS, 0, false, false, AM_LIMIT_SOURCE_MS), AM_EVENT_WARN);
}

TEST(StateUpdate, WarnsOnlyOncePerOutage)
{
	am_source_state state = fresh_state();
	am_state_update(&state, AM_LIMIT_SOURCE_MS, 0, false, false, AM_LIMIT_SOURCE_MS);
	EXPECT_EQ(am_state_update(&state, AM_LIMIT_SOURCE_MS * 5, 0, false, false, AM_LIMIT_SOURCE_MS), AM_EVENT_NONE);
}

TEST(StateUpdate, SoundKeepsClockAtZero)
{
	am_source_state state = fresh_state();
	EXPECT_EQ(am_state_update(&state, 59000, 59000, false, false, AM_LIMIT_SOURCE_MS), AM_EVENT_NONE);
	EXPECT_EQ(am_state_update(&state, 118000, 59000, false, false, AM_LIMIT_SOURCE_MS), AM_EVENT_NONE);
	EXPECT_EQ(am_state_update(&state, 119000, 59000, false, false, AM_LIMIT_SOURCE_MS), AM_EVENT_WARN);
}

TEST(StateUpdate, RecoversAndWarnsAgainOnNextOutage)
{
	am_source_state state = fresh_state();
	am_state_update(&state, 60000, 0, false, false, AM_LIMIT_SOURCE_MS);
	EXPECT_EQ(am_state_update(&state, 70000, 70000, false, false, AM_LIMIT_SOURCE_MS), AM_EVENT_RECOVERED);
	EXPECT_EQ(am_state_update(&state, 130000, 70000, false, false, AM_LIMIT_SOURCE_MS), AM_EVENT_WARN);
}

TEST(StateUpdate, MutedSourceNeverWarns)
{
	am_source_state state = fresh_state();
	EXPECT_EQ(am_state_update(&state, 600000, 0, true, false, AM_LIMIT_SOURCE_MS), AM_EVENT_NONE);
}

TEST(StateUpdate, UnmuteRestartsTheClock)
{
	am_source_state state = fresh_state();
	am_state_update(&state, 100000, 0, true, false, AM_LIMIT_SOURCE_MS);
	EXPECT_EQ(am_state_update(&state, 159000, 0, false, false, AM_LIMIT_SOURCE_MS), AM_EVENT_NONE);
	EXPECT_EQ(am_state_update(&state, 160000, 0, false, false, AM_LIMIT_SOURCE_MS), AM_EVENT_WARN);
}

TEST(StateUpdate, MutingAfterWarningRearms)
{
	am_source_state state = fresh_state();
	am_state_update(&state, 60000, 0, false, false, AM_LIMIT_SOURCE_MS);
	am_state_update(&state, 61000, 0, true, false, AM_LIMIT_SOURCE_MS);
	EXPECT_EQ(am_state_update(&state, 121000, 0, false, false, AM_LIMIT_SOURCE_MS), AM_EVENT_WARN);
}

TEST(StateUpdate, LtcForcedFaultWarnsDespiteSamples)
{
	am_source_state state = fresh_state();
	EXPECT_EQ(am_state_update(&state, 2000, 2000, false, true, AM_LIMIT_LTC_MS), AM_EVENT_NONE);
	EXPECT_EQ(am_state_update(&state, 3000, 3000, false, true, AM_LIMIT_LTC_MS), AM_EVENT_WARN);
}

TEST(StateUpdate, LtcRecoversWhenFaultEnds)
{
	am_source_state state = fresh_state();
	am_state_update(&state, 3000, 3000, false, true, AM_LIMIT_LTC_MS);
	EXPECT_EQ(am_state_update(&state, 4000, 4000, false, false, AM_LIMIT_LTC_MS), AM_EVENT_RECOVERED);
}

TEST(StateUpdate, SignalNewerThanNowDoesNotUnderflow)
{
	am_source_state state = fresh_state();
	am_state_update(&state, 5000, 5100, false, false, AM_LIMIT_LTC_MS);
	EXPECT_EQ(am_state_update(&state, 5050, 5100, false, false, AM_LIMIT_LTC_MS), AM_EVENT_NONE);
}
