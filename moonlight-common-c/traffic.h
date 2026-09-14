#pragma once
/* DeskPort-owned socket accounting. Keep the pinned upstream sources intact. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint64_t DpTrafficReceived(void);
uint64_t DpTrafficSent(void);
#ifdef __cplusplus
}
#endif
#if !defined(_WIN32) && !defined(DP_TRAFFIC_IMPLEMENTATION)
#include <sys/types.h>
#include <sys/socket.h>
ssize_t dp_recv(int, void*, size_t, int);
ssize_t dp_send(int, const void*, size_t, int);
ssize_t dp_recvfrom(int, void*, size_t, int, struct sockaddr*, socklen_t*);
ssize_t dp_sendto(int, const void*, size_t, int, const struct sockaddr*, socklen_t);
ssize_t dp_recvmsg(int, struct msghdr*, int);
ssize_t dp_sendmsg(int, const struct msghdr*, int);
#define recv dp_recv
#define send dp_send
#define recvfrom dp_recvfrom
#define sendto dp_sendto
#define recvmsg dp_recvmsg
#define sendmsg dp_sendmsg
#endif
