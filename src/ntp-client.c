/*
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

	/* Resolve hostname */
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	if (getaddrinfo(server, "123", &hints, &res) != 0)
		return false;

	/* Create UDP socket */
	socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCK) {
		freeaddrinfo(res);
		return false;
	}

	/* Set timeout */
	struct timeval tv;
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

	/* Build NTP request: version 4, mode 3 (client) */
	ntp_packet_t packet;
	memset(&packet, 0, sizeof(packet));
	packet.li_vn_mode = 0x23; /* LI=0, VN=4, Mode=3 */

	/* Record T1 (client send time) */
	int64_t t1_sec, t1_usec;
	get_system_time(&t1_sec, &t1_usec);

	/* Send request */
	if (sendto(sock, (const char *)&packet, sizeof(packet), 0, res->ai_addr, (int)res->ai_addrlen) < 0) {
		CLOSE_SOCKET(sock);
		freeaddrinfo(res);
		return false;
	}

	freeaddrinfo(res);

	/* Receive response */
	int n = recv(sock, (char *)&packet, sizeof(packet), 0);
	CLOSE_SOCKET(sock);

	if (n < (int)sizeof(packet))
		return false;

	/* Record T4 (client receive time) */
	int64_t t4_sec, t4_usec;
	get_system_time(&t4_sec, &t4_usec);

	/* Extract T2 (server receive) and T3 (server transmit) */
	int64_t t2_sec = (int64_t)ntohl(packet.rx_ts_sec) - NTP_UNIX_DELTA;
	int64_t t2_frac = (int64_t)ntohl(packet.rx_ts_frac);

	int64_t t3_sec = (int64_t)ntohl(packet.tx_ts_sec) - NTP_UNIX_DELTA;
	int64_t t3_frac = (int64_t)ntohl(packet.tx_ts_frac);

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
