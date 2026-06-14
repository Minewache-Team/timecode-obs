/*
 * test-http-time.cpp - Unit tests for HTTP Date header time parsing
 *
 * Tests the date parser independently (no network needed).
 */

#include <gtest/gtest.h>

extern "C" {
#include "http-time-client.h"
}

TEST(HTTPTimeTest, ParseValidDate)
{
	int64_t sec;
	/* 2026-03-01 12:34:56 UTC */
	ASSERT_TRUE(http_time_parse_date("Sun, 01 Mar 2026 12:34:56 GMT", &sec));
	/* Verify: 2026-03-01 = day 20513 from epoch
	 * 20513 * 86400 = 1772323200 + 12*3600 + 34*60 + 56 = 1772368496 */
	EXPECT_EQ(sec, 1772323200LL + 12 * 3600 + 34 * 60 + 56);
}

TEST(HTTPTimeTest, ParseDateDifferentDay)
{
	int64_t sec;
	/* 2024-02-29 00:00:00 UTC (leap year) */
	ASSERT_TRUE(http_time_parse_date("Thu, 29 Feb 2024 00:00:00 GMT", &sec));
	EXPECT_EQ(sec, 1709164800LL);
}

TEST(HTTPTimeTest, ParseDateMidnight)
{
	int64_t sec;
	ASSERT_TRUE(http_time_parse_date("Wed, 01 Jan 2025 00:00:00 GMT", &sec));
	/* 2025-01-01 00:00:00 UTC */
	EXPECT_EQ(sec, 1735689600LL);
}

TEST(HTTPTimeTest, ParseDateEndOfDay)
{
	int64_t sec;
	ASSERT_TRUE(http_time_parse_date("Wed, 01 Jan 2025 23:59:59 GMT", &sec));
	EXPECT_EQ(sec, 1735689600LL + 23 * 3600 + 59 * 60 + 59);
}

TEST(HTTPTimeTest, ParseInvalidEmpty)
{
	int64_t sec;
	EXPECT_FALSE(http_time_parse_date("", &sec));
}

TEST(HTTPTimeTest, ParseInvalidGarbage)
{
	int64_t sec;
	EXPECT_FALSE(http_time_parse_date("not a date at all", &sec));
}

TEST(HTTPTimeTest, ParseNullInputs)
{
	int64_t sec;
	EXPECT_FALSE(http_time_parse_date(NULL, &sec));
	EXPECT_FALSE(http_time_parse_date("Sun, 01 Mar 2026 12:34:56 GMT", NULL));
}

TEST(HTTPTimeTest, ParseInvalidMonth)
{
	int64_t sec;
	EXPECT_FALSE(http_time_parse_date("Sun, 01 Xyz 2026 12:34:56 GMT", &sec));
}

TEST(HTTPTimeTest, ParseInvalidDay)
{
	int64_t sec;
	EXPECT_FALSE(http_time_parse_date("Sun, 00 Mar 2026 12:34:56 GMT", &sec));
	EXPECT_FALSE(http_time_parse_date("Sun, 32 Mar 2026 12:34:56 GMT", &sec));
}

TEST(HTTPTimeTest, ParseInvalidYear)
{
	int64_t sec;
	EXPECT_FALSE(http_time_parse_date("Sun, 01 Mar 1969 12:34:56 GMT", &sec));
}

TEST(HTTPTimeTest, ParseAllMonths)
{
	int64_t sec;
	const char *dates[] = {
		"Wed, 15 Jan 2025 12:00:00 GMT", "Sat, 15 Feb 2025 12:00:00 GMT", "Sat, 15 Mar 2025 12:00:00 GMT",
		"Tue, 15 Apr 2025 12:00:00 GMT", "Thu, 15 May 2025 12:00:00 GMT", "Sun, 15 Jun 2025 12:00:00 GMT",
		"Tue, 15 Jul 2025 12:00:00 GMT", "Fri, 15 Aug 2025 12:00:00 GMT", "Mon, 15 Sep 2025 12:00:00 GMT",
		"Wed, 15 Oct 2025 12:00:00 GMT", "Sat, 15 Nov 2025 12:00:00 GMT", "Mon, 15 Dec 2025 12:00:00 GMT",
	};
	for (int i = 0; i < 12; i++) {
		EXPECT_TRUE(http_time_parse_date(dates[i], &sec)) << "Failed for month " << (i + 1);
	}
}
