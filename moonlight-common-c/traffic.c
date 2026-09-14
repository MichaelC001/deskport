#undef recv
#undef send
#undef recvfrom
#undef sendto
#undef recvmsg
#undef sendmsg
#define DP_TRAFFIC_IMPLEMENTATION
#include "traffic.h"
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
static uint64_t received, sent;
uint64_t DpTrafficReceived(void) { return __atomic_load_n(&received, __ATOMIC_RELAXED); }
uint64_t DpTrafficSent(void) { return __atomic_load_n(&sent, __ATOMIC_RELAXED); }
static ssize_t record(ssize_t count, uint64_t* counter, int flags) {
    const int error = errno;
    if (count > 0 && !(flags & MSG_PEEK)) __atomic_fetch_add(counter, (uint64_t)count, __ATOMIC_RELAXED);
    errno = error;
    return count;
}
ssize_t dp_recv(int fd, void* buf, size_t n, int flags) { return record(recv(fd, buf, n, flags), &received, flags); }
ssize_t dp_send(int fd, const void* buf, size_t n, int flags) { return record(send(fd, buf, n, flags), &sent, 0); }
ssize_t dp_recvfrom(int fd, void* buf, size_t n, int flags, struct sockaddr* addr, socklen_t* len) { return record(recvfrom(fd, buf, n, flags, addr, len), &received, flags); }
ssize_t dp_sendto(int fd, const void* buf, size_t n, int flags, const struct sockaddr* addr, socklen_t len) { return record(sendto(fd, buf, n, flags, addr, len), &sent, 0); }
ssize_t dp_recvmsg(int fd, struct msghdr* msg, int flags) { return record(recvmsg(fd, msg, flags), &received, flags); }
ssize_t dp_sendmsg(int fd, const struct msghdr* msg, int flags) { return record(sendmsg(fd, msg, flags), &sent, 0); }
#else
/* Windows is not a target of this prerelease. Never fabricate measurements. */
uint64_t DpTrafficReceived(void) { return 0; }
uint64_t DpTrafficSent(void) { return 0; }
#endif
