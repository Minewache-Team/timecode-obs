/*
 * obs-ltc-timecode - NTP-synced LTC timecode audio source for OBS Studio
 * Copyright (C) 2024-2026 Ferdmusic
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>
 *
 * ntp-client.c - Minimal SNTP client implementation
 *
 * Supports Windows (Winsock2) and Linux (POSIX sockets).
 */

#include "ntp-client.h"

#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#define CLOSE_SOCKET closesocket
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
typedef int socket_t;
#define INVALID_SOCK (-1)
#define CLOSE_SOCKET close
#endif

/* NTP epoch: 1900-01-01, Unix epoch: 1970-01-01 */
#define NTP_UNIX_DELTA 2208988800ULL

/* Garbage-server floor: a server whose transmit time is before 2020 is
 * broken or malicious — accepting it would yank the timecode by years.
 * (Unix seconds for 2020-01-01T00:00:00Z.) */
#define NTP_SANITY_FLOOR_UNIX 1577836800LL

/* Convert a raw 32-bit NTP timestamp (seconds field) to Unix seconds with
 * era handling: era 0 runs 1900–2036 (MSB is set for everything after
 * 1968), era 1 starts 2036-02-07 with the MSB clear. Without this, every
 * client breaks at the 2036 rollover — and field hardware tends to
 * outlive its planned lifetime. Valid until ~2104. */
static int64_t ntp_ts_to_unix_sec(uint32_t raw_sec)
{
	if (raw_sec & 0x80000000U)
		return (int64_t)raw_sec - (int64_t)NTP_UNIX_DELTA;
	return (int64_t)raw_sec + 4294967296LL - (int64_t)NTP_UNIX_DELTA;
}

/* NTP packet structure (48 bytes) */
typedef struct {
	uint8_t li_vn_mode;
	uint8_t stratum;
	uint8_t poll;
	int8_t precision;
	uint32_t root_delay;
	uint32_t root_dispersion;
	uint32_t ref_id;
	uint32_t ref_ts_sec;
	uint32_t ref_ts_frac;
	uint32_t orig_ts_sec;
	uint32_t orig_ts_frac;
	uint32_t rx_ts_sec;
	uint32_t rx_ts_frac;
	uint32_t tx_ts_sec;
	uint32_t tx_ts_frac;
} ntp_packet_t;

static void get_system_time(int64_t *sec, int64_t *usec)
{
#ifdef _WIN32
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	/* Convert from 100-ns intervals since 1601 to Unix epoch */
	t -= 116444736000000000ULL;
	*sec = (int64_t)(t / 10000000ULL);
	*usec = (int64_t)((t % 10000000ULL) / 10);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	*sec = tv.tv_sec;
	*usec = tv.tv_usec;
#endif
}

bool ntp_platform_init(void)
{
#ifdef _WIN32
	WSADATA wsa;
	return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
	return true;
#endif
}

void ntp_platform_cleanup(void)
{
#ifdef _WIN32
	WSACleanup();
#endif
}

bool ntp_query(const char *server, int timeout_ms, ntp_result_t *result)
{
	if (!server || !result)
		return false;

	memset(result, 0, sizeof(*result));

	/* Resolve hostname. AF_UNSPEC on purpose: a large share of German
	 * residential connections are DS-Lite (cable ISPs) — IPv4 there is
	 * tunneled through the provider's CGNAT (AFTR), adding latency and
	 * jitter, while native IPv6 goes direct. The OS orders the results
	 * (RFC 6724, IPv6 preferred when routable); we walk them until one
	 * connects. Forcing AF_INET picked the WORSE path on those lines. */
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo(server, "123", &hints, &res) != 0)
		return false;

	socket_t sock = INVALID_SOCK;
	for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
		sock = socket(ai->ai_family, SOCK_DGRAM, IPPROTO_UDP);
		if (sock == INVALID_SOCK)
			continue;

		/* Set receive timeout */
#ifdef _WIN32
		/* Windows: SO_RCVTIMEO expects a DWORD (milliseconds) */
		DWORD rcv_timeout = (DWORD)timeout_ms;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
			   (const char *)&rcv_timeout, sizeof(rcv_timeout));
#else
		struct timeval tv;
		tv.tv_sec = timeout_ms / 1000;
		tv.tv_usec = (timeout_ms % 1000) * 1000;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv,
			   sizeof(tv));
#endif

		/* Connect the UDP socket: the kernel then discards datagrams
		 * from any other source address/port, so an unrelated host
		 * can't answer in the server's place. Also turns ICMP
		 * port-unreachable into a fast recv error instead of a full
		 * timeout — and fails immediately for an unroutable family
		 * (IPv6 address without IPv6 uplink), falling through to the
		 * next resolved address. */
		if (connect(sock, ai->ai_addr, (int)ai->ai_addrlen) == 0)
			break;

		CLOSE_SOCKET(sock);
		sock = INVALID_SOCK;
	}
	freeaddrinfo(res);

	if (sock == INVALID_SOCK)
		return false;

	/* Build NTP request: version 4, mode 3 (client) */
	ntp_packet_t packet;
	memset(&packet, 0, sizeof(packet));
	packet.li_vn_mode = 0x23; /* LI=0, VN=4, Mode=3 */

	/* Record T1 (client send time) */
	int64_t t1_sec, t1_usec;
	get_system_time(&t1_sec, &t1_usec);

	/* Put T1 into the request's transmit timestamp. The server echoes it
	 * back in originate_ts; a response that doesn't echo it is stale, a
	 * duplicate, or spoofed — and gets dropped. Standard SNTP nonce
	 * (RFC 4330 §5); the fractional part carries sub-µs entropy. */
	uint32_t t1_ntp_sec = (uint32_t)((uint64_t)t1_sec + NTP_UNIX_DELTA);
	uint32_t t1_ntp_frac = (uint32_t)((t1_usec << 32) / 1000000);
	packet.tx_ts_sec = htonl(t1_ntp_sec);
	packet.tx_ts_frac = htonl(t1_ntp_frac);

	/* Send request */
	if (send(sock, (const char *)&packet, sizeof(packet), 0) < 0) {
		CLOSE_SOCKET(sock);
		return false;
	}

	/* Receive response */
	int n = recv(sock, (char *)&packet, sizeof(packet), 0);
	CLOSE_SOCKET(sock);

	if (n < (int)sizeof(packet))
		return false;

	/* Validate response: reject KoD packets and unsynchronized servers */
	uint8_t mode = packet.li_vn_mode & 0x07;
	uint8_t stratum = packet.stratum;
	if (mode != 4 && mode != 5) /* expect server or broadcast mode */
		return false;
	if (stratum == 0 || stratum > 15) /* stratum 0 = KoD, >15 = invalid */
		return false;
	if (packet.tx_ts_sec == 0) /* server never set transmit time */
		return false;

	/* Originate timestamp must echo our transmit timestamp (nonce). */
	if (packet.orig_ts_sec != htonl(t1_ntp_sec) ||
	    packet.orig_ts_frac != htonl(t1_ntp_frac))
		return false;

	/* Record T4 (client receive time) */
	int64_t t4_sec, t4_usec;
	get_system_time(&t4_sec, &t4_usec);

	/* Extract T2 (server receive) and T3 (server transmit) */
	int64_t t2_sec = ntp_ts_to_unix_sec(ntohl(packet.rx_ts_sec));
	int64_t t2_frac = (int64_t)ntohl(packet.rx_ts_frac);

	int64_t t3_sec = ntp_ts_to_unix_sec(ntohl(packet.tx_ts_sec));
	int64_t t3_frac = (int64_t)ntohl(packet.tx_ts_frac);

	/* Garbage-server guard: transmit time before 2020 means the server's
	 * own clock is nonsense — never let it become the sync target. */
	if (t3_sec < NTP_SANITY_FLOOR_UNIX)
		return false;

	/* Convert fractional parts to microseconds */
	int64_t t2_usec = (t2_frac * 1000000LL) >> 32;
	int64_t t3_usec = (t3_frac * 1000000LL) >> 32;

	/* Calculate offset: ((T2 - T1) + (T3 - T4)) / 2 */
	int64_t d1_ms = (t2_sec - t1_sec) * 1000 + (t2_usec - t1_usec) / 1000;
	int64_t d2_ms = (t3_sec - t4_sec) * 1000 + (t3_usec - t4_usec) / 1000;

	result->offset_ms = (d1_ms + d2_ms) / 2;
	result->roundtrip_ms = (d1_ms - d2_ms);
	if (result->roundtrip_ms < 0)
		result->roundtrip_ms = -result->roundtrip_ms;
	result->success = true;

	return true;
}

void ntp_corrected_time(int64_t offset_ms, int64_t *out_sec, int64_t *out_usec)
{
	int64_t sec, usec;
	get_system_time(&sec, &usec);

	/* Apply NTP offset */
	int64_t total_usec = usec + (offset_ms % 1000) * 1000;
	sec += offset_ms / 1000;

	if (total_usec >= 1000000) {
		sec += 1;
		total_usec -= 1000000;
	} else if (total_usec < 0) {
		sec -= 1;
		total_usec += 1000000;
	}

	if (out_sec)
		*out_sec = sec;
	if (out_usec)
		*out_usec = total_usec;
}

int ntp_select_best_sample(const ntp_result_t *samples, int count,
			   int64_t max_rtt_ms)
{
	int best = -1;

	if (!samples)
		return -1;

	for (int i = 0; i < count; i++) {
		if (!samples[i].success)
			continue;
		if (max_rtt_ms > 0 && samples[i].roundtrip_ms > max_rtt_ms)
			continue;
		if (best < 0 ||
		    samples[i].roundtrip_ms < samples[best].roundtrip_ms)
			best = i;
	}

	return best;
}

int64_t ntp_slew_step(int64_t applied_ms, int64_t target_ms, int64_t max_step_ms)
{
	if (max_step_ms <= 0)
		return applied_ms;
	int64_t diff = target_ms - applied_ms;
	if (diff > max_step_ms)
		return applied_ms + max_step_ms;
	if (diff < -max_step_ms)
		return applied_ms - max_step_ms;
	return target_ms;
}
