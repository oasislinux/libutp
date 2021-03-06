#ifdef WIN32
#define _CRT_SECURE_NO_DEPRECATE
#define WIN32_LEAN_AND_MEAN
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>

#ifdef WIN32
// newer versions of MSVC define these in errno.h
#ifndef ECONNRESET
#define ECONNRESET WSAECONNRESET
#define EMSGSIZE WSAEMSGSIZE
#define ECONNREFUSED WSAECONNREFUSED
#define ECONNRESET WSAECONNRESET
#define ETIMEDOUT WSAETIMEDOUT
#endif
#endif

// platform-specific includes
#ifdef POSIX
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#ifdef __APPLE__
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif

#ifdef FREEBSD
#include <strings.h>            // POSIX requires this split-up between string.h and strings.h
#include <sys/uio.h>            // FreeBSD:  for readv, writev
#include <sys/mount.h>          // FreeBSD:  for statfs
#include <ifaddrs.h>            // FreeBSD:  for ifaddrs, getifaddrs, freeifaddrs
#include <net/if_dl.h>          // FreeBSD:  for sockaddr_dl
#include <sys/sysctl.h>         // FreeBSD:  for system control in util_posix.cpp
#endif

#else  // WIN32

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "win32_inet_ntop.h"

#endif //WIN32

#include "utp.h"
#include "utp_utils.h"

#include "udp.h"

// These are for casting the options for getsockopt
// and setsockopt which if incorrect can cause these
// calls to fail.
#ifdef WIN32
typedef char * SOCKOPTP;
typedef const char * CSOCKOPTP;
#else
typedef void * SOCKOPTP;
typedef const void * CSOCKOPTP;
#endif

FILE* log_file = 0;

int g_send_limit = 50 * 1024 * 1024;
int g_total_sent = 0;

struct socket_state
{
	socket_state(): total_sent(0), state(0), s(0) {}
	int total_sent;
	int state;
	UTPSocket* s;

	bool operator==(socket_state const& rhs) const { return s == rhs.s; }
};

socket_state *g_sockets;
size_t g_sockets_alloc;
size_t g_sockets_count;

static void g_sockets_append(socket_state *s)
{
	if (g_sockets_count >= g_sockets_alloc) {
		g_sockets_alloc = g_sockets_alloc ? g_sockets_alloc * 2 : 16;
		g_sockets = (socket_state *)realloc(g_sockets, g_sockets_alloc * sizeof(g_sockets[0]));
	}
	g_sockets[g_sockets_count++] = *s;
}

void utp_log(char const* fmt, ...)
{
	fprintf(log_file, "[%u] ", UTP_GetMilliseconds());
	va_list vl;
	va_start(vl, fmt);
	vfprintf(log_file, fmt, vl);
	va_end(vl);
	fputs("\n", log_file);
}

UDPSocketManager::UDPSocketManager()
	: _socket(INVALID_SOCKET), pos(0), count(0)
{
}

// Send a message on the actual UDP socket
void UDPSocketManager::Send(const unsigned char *p, size_t len, const struct sockaddr *to, socklen_t tolen)
{
	assert(len <= UTP_GetUDPMTU(to, tolen));

	if (count > 0 ||
		sendto(_socket, (char*)p, len, 0, (struct sockaddr*)to, tolen) < 0) {
		// Buffer a packet.
		if (
#ifndef WIN32
			errno != EPERM && errno != EINVAL && 
#endif
			count < UDP_OUTGOING_SIZE) {
			UdpOutgoing *q = (UdpOutgoing*)malloc(sizeof(UdpOutgoing) - 1 + len);
			memcpy(&q->to, to, tolen);
			q->len = len;
			memcpy(q->mem, p, len);
			buff[pos] = q;
			pos = (pos + 1) & (UDP_OUTGOING_SIZE-1);
			count++;
			printf("buffering packet: %d %s\n", count, strerror(errno));
		} else {
			printf("sendto failed: %s\n", strerror(errno));
		}
	}
}

void UDPSocketManager::Flush()
{
	assert(count >= 0);
	while (count != 0) {
		UdpOutgoing *uo = buff[(pos - count) & (UDP_OUTGOING_SIZE-1)];

		if (sendto(_socket, (char*)uo->mem, uo->len, 0, (struct sockaddr*)&uo->to, sizeof(uo->to)) < 0) {
#ifndef WIN32
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				printf("sendto failed: %s\n", strerror(errno));
				break;
			}
#endif
		}

		free(uo);
		count--;
	}
	assert(count >= 0);
}

SOCKET make_socket(const struct sockaddr *addr, socklen_t addrlen)
{
	SOCKET s = socket(addr->sa_family, SOCK_DGRAM, 0);
	if (s == INVALID_SOCKET) return s;

	if (bind(s, addr, addrlen) < 0) {
		char str[20];
		printf("UDP port bind failed %s: (%d) %s\n",
			   inet_ntop(addr->sa_family, (sockaddr*)addr, str, sizeof(str)), errno, strerror(errno));
		closesocket(s);
		return INVALID_SOCKET;
	}

	// Mark to hold a couple of megabytes
	int size = 2 * 1024 * 1024;

	if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (CSOCKOPTP)&size, sizeof(size)) < 0) {
		printf("UDP setsockopt(SO_RCVBUF, %d) failed: %d %s\n", size, errno, strerror(errno));
	}
	if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (CSOCKOPTP)&size, sizeof(size)) < 0) {
		printf("UDP setsockopt(SO_SNDBUF, %d) failed: %d %s\n", size, errno, strerror(errno));
	}

	// make socket non blocking
#ifdef _WIN32
	u_long b = 1;
	ioctlsocket(s, FIONBIO, &b);
#else
	int flags = fcntl(s, F_GETFL, 0);
	fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif

	return s;
}

void UDPSocketManager::set_socket(SOCKET s)
{
	if (_socket != INVALID_SOCKET) closesocket(_socket);
	assert(s != INVALID_SOCKET);
	_socket = s;
}

void send_to(void *userdata, const unsigned char *p, size_t len, const struct sockaddr *to, socklen_t tolen)
{
	((UDPSocketManager*)userdata)->Send(p, len, to, tolen);
}

void got_incoming_connection(void *userdata, UTPSocket* s)
{
	printf("incoming socket %p\n", s);
	socket_state sock;
	sock.s = s;
	g_sockets_append(&sock);
	// An application would call UTP_SetCallbacks here, but we don't want to do anything
	// with this socket except throw away bytes received.
	//UTP_SetCallbacks(s, &utp_callbacks, &g_sockets[g_sockets_count-1]);
	// perhaps they would set some sock opts here too
	//UTP_SetSockopt(s, SO_SNDBUF, 100*300);
}

void UDPSocketManager::select(int microsec)
{
	struct timeval tv = {microsec / 1000000, microsec % 1000000};
	fd_set r, e;
	FD_ZERO(&r);
	FD_ZERO(&e);
	FD_SET(_socket, &r);
	FD_SET(_socket, &e);
	int ret = ::select(_socket + 1, &r, 0, &e, &tv);

	if (ret == 0) return;

	if (ret < 0) {
		printf("select() failed: %s\n", strerror(errno));
		return;
	}

	Flush();

	if (FD_ISSET(_socket, &r)) {
		unsigned char buffer[8192];
		struct sockaddr_storage sa;
		socklen_t salen = sizeof(sa);

		for (;;) {
			int len = recvfrom(_socket, (char*)buffer, sizeof(buffer), 0, (struct sockaddr*)&sa, &salen);
			if (len < 0) {
				int err = WSAGetLastError();
				// ECONNRESET - On a UDP-datagram socket
				// this error indicates a previous send operation
				// resulted in an ICMP Port Unreachable message.
				if (err == ECONNRESET) continue;
				// EMSGSIZE - The message was too large to fit into
				// the buffer pointed to by the buf parameter and was
				// truncated.
				if (err == EMSGSIZE) continue;
				// any other error (such as EWOULDBLOCK) results in breaking the loop
				break;
			}

			// Lookup the right UTP socket that can handle this message
			if (UTP_IsIncomingUTP(&got_incoming_connection, &send_to, this,
								  buffer, (size_t)len, (const struct sockaddr*)&sa, salen))
				continue;
		}

		if (FD_ISSET(_socket, &e)) {
			// error!
			printf("socket error!\n");
		}
	}

}

void utp_read(void* socket, const unsigned char *bytes, size_t count)
{
}

void utp_write(void* socket, unsigned char *bytes, size_t count)
{
	for (size_t i = 0; i < count; ++i) {
		*bytes = rand();
		++bytes;
	}
	socket_state* s = (socket_state*)socket;
	s->total_sent += count;
	g_total_sent += count;
}

size_t utp_get_rb_size(void* socket)
{
	return 0;
}

void utp_state(void* socket, int state)
{
	utp_log("[%p] state: %d", socket, state);

	socket_state* s = (socket_state*)socket;
	s->state = state;
	if (state == UTP_STATE_WRITABLE || state == UTP_STATE_CONNECT) {
		if (UTP_Write(s->s, g_send_limit - s->total_sent)) {
			UTP_Close(s->s);
			s->s = NULL;
		}
	} else if (state == UTP_STATE_DESTROYING) {
		size_t i;
		for (i = 0; i != g_sockets_count; i++) {
			if (g_sockets[i] == *s) {
				size_t c = --g_sockets_count;
				if (i != c)
					g_sockets[i] = g_sockets[c];
				break;
			}
		}
	}
}

void utp_error(void* socket, int errcode)
{
	socket_state* s = (socket_state*)socket;
	printf("socket error: (%d) %s\n", errcode, strerror(errcode));
	if (s->s) {
		UTP_Close(s->s);
		s->s = NULL;
	}
}

void utp_overhead(void *socket, bool send, size_t count, int type)
{
}

extern int utp_max_cwnd_increase_bytes_per_rtt;
extern int utp_target_delay;
extern int utp_max_window_decay;
extern int utp_reorder_buffer_size;
extern int utp_packet_size;
extern int utp_serializations_per_target;
extern int utp_min_window_size;
extern int utp_use_packet_pacing;
extern int utp_duplicate_acks_before_resend;
extern int utp_delayed_ack_byte_threshold;
extern int utp_delayed_ack_time_threshold;
extern int utp_ratecheck_interval;

struct setting_t
{
	char const* name;
	int* val;
};

#define SETTING(x) {#x, &x},

setting_t settings[] =
{
	// these are broken because I broke them. they could probably be fixed, though.
	/*
	SETTING(utp_max_cwnd_increase_bytes_per_rtt)
	SETTING(utp_target_delay)
	SETTING(utp_max_window_decay)
	SETTING(utp_reorder_buffer_size)
	SETTING(utp_packet_size)
	SETTING(utp_serializations_per_target)
	SETTING(utp_min_window_size)
	SETTING(utp_use_packet_pacing)
	SETTING(utp_duplicate_acks_before_resend)
	SETTING(utp_delayed_ack_byte_threshold)
	SETTING(utp_delayed_ack_time_threshold)
	SETTING(utp_ratecheck_interval)
	*/
	SETTING(g_send_limit)
};

const int num_settings = sizeof(settings) / sizeof(setting_t);

int main(int argc, char* argv[])
{
	char const* log_file_name = "utp.log";
	int port = 8000;

	if (argc < 4) {
		printf("usage: %s listen-port log-file destination [config-file]\n\n"
			"   listen-port: the port for this node to listen on\n"
			"   log-file: name and path of the file to log uTP logs to\n"
			"   destination: destination node to connect to, in the form <host>:<port>\n"
			"   config-file: name and path of configuration file\n\n"
			, argv[0]);
		return 1;
	}

	port = atoi(argv[1]);
	log_file_name = argv[2];
	char *dest = argv[3];
	char const* disabled = "disabled";
	char const* settings_file = disabled;
	if (argc > 4) settings_file = argv[4];

	printf("listening on port %d\n", port);
	printf("logging to '%s'\n", log_file_name);
	printf("connecting to %s\n", dest);
	printf("using settings file %s\n", settings_file);

	if (settings_file != disabled) {
		FILE* config = fopen(settings_file, "r");
		if (config == 0) {
			fprintf(stderr, "failed to open config file \"%s\": %s\n", settings_file, strerror(errno));
			return 1;
		}
		char name[100];
		int value;
		while (fscanf(config, "%s%d\n", name, &value) == 2) {
			bool found_setting = false;
			for (int i = 0; i < num_settings; ++i) {
				if (strcmp(settings[i].name, name) != 0) continue;
				found_setting = true;
				*settings[i].val = value;
				break;
			}
			if (!found_setting) fprintf(stderr, "ignored unrecognized setting: \"%s\"\n", name);
		}
		fclose(config);
	}

	log_file = fopen(log_file_name, "w+");

	puts("using configuration:\n");
	for (int i = 0; i < num_settings; ++i) {
		printf("   %s: %d\n", settings[i].name, *settings[i].val);
	}
	puts("\n");

#ifdef WIN32
	// ow
	WSADATA wsa;
	BYTE byMajorVersion = 2, byMinorVersion = 2;
	int result = WSAStartup(MAKEWORD(byMajorVersion, byMinorVersion), &wsa);
	if (result != 0 || LOBYTE(wsa.wVersion) != byMajorVersion ||
		HIBYTE(wsa.wVersion) != byMinorVersion ) {
		if (result == 0)
			WSACleanup();
		return -1;
	}
#endif

	UDPSocketManager sm;

	sockaddr_in sin;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port);
	SOCKET sock = make_socket((const struct sockaddr*)&sin, sizeof(sin));

	sm.set_socket(sock);

	char *portchr = strchr(dest, ':');
	*portchr = 0;
	portchr++;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inet_addr(dest);
	sin.sin_port = htons(atoi(portchr));

	socket_state s;
	s.s = UTP_Create(&send_to, &sm, (const struct sockaddr*)&sin, sizeof(sin));
	UTP_SetSockopt(s.s, SO_SNDBUF, 100*300);
	s.state = 0;
	printf("creating socket %p\n", s.s);

	UTPFunctionTable utp_callbacks = {
		&utp_read,
		&utp_write,
		&utp_get_rb_size,
		&utp_state,
		&utp_error,
		&utp_overhead
	};
	g_sockets_append(&s);
	UTP_SetCallbacks(s.s, &utp_callbacks, &g_sockets[g_sockets_count-1]);

	printf("connecting socket %p\n", s.s);
	UTP_Connect(s.s);

	int last_sent = 0;
	unsigned int last_time = UTP_GetMilliseconds();

	while (g_sockets_count > 0) {
		sm.select(50000);
		UTP_CheckTimeouts();
		unsigned int cur_time = UTP_GetMilliseconds();
		if (cur_time >= last_time + 1000) {
			float rate = (g_total_sent - last_sent) * 1000.f / (cur_time - last_time);
			last_sent = g_total_sent;
			last_time = cur_time;
			printf("\r[%u] sent: %d/%d  %.1f bytes/s  ", cur_time, g_total_sent, g_send_limit, rate);
			fflush(stdout);
		}
	}

	fclose(log_file);
}
