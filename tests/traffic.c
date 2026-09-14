/* Loopback-only accounting acceptance: actual bytes, peeks, failures and ENet I/O. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>
#include <pthread.h>
static void* transfer(void* unused) {
    (void)unused;
    int pair[2]; assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == 0);
    char data[1024] = {0};
    for (int i = 0; i < 1000; ++i) {
        assert(send(pair[0], data, sizeof(data), 0) == sizeof(data));
        assert(recv(pair[1], data, sizeof(data), 0) == sizeof(data));
    }
    close(pair[0]); close(pair[1]); return NULL;
}
int main(void) {
    int pair[2]; char buffer[32];
    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == 0);
    assert(send(pair[0], "hello", 5, 0) == 5);
    assert(DpTrafficSent() == 5);
    assert(recv(pair[1], buffer, sizeof(buffer), MSG_PEEK) == 5);
    assert(DpTrafficReceived() == 0);
    assert(recv(pair[1], buffer, sizeof(buffer), 0) == 5);
    assert(DpTrafficReceived() == 5);
    assert(send(-1, "x", 1, 0) == -1 && errno == EBADF);
    assert(recv(-1, buffer, sizeof(buffer), 0) == -1 && errno == EBADF);
    assert(DpTrafficSent() == 5 && DpTrafficReceived() == 5);
    struct iovec vec[2] = {{"abc",3},{"defgh",5}};
    struct msghdr msg = {0}; msg.msg_iov = vec; msg.msg_iovlen = 2;
    assert(sendmsg(pair[0], &msg, 0) == 8);
    struct iovec dest = {buffer,sizeof(buffer)}; msg.msg_iov = &dest; msg.msg_iovlen = 1;
    assert(recvmsg(pair[1], &msg, 0) == 8);
    assert(DpTrafficSent() == 13 && DpTrafficReceived() == 13);
    assert(sendto(pair[0], "udp", 3, 0, NULL, 0) == 3);
    assert(recvfrom(pair[1], buffer, sizeof(buffer), 0, NULL, NULL) == 3);
    assert(DpTrafficSent() == 16 && DpTrafficReceived() == 16);
    close(pair[0]); close(pair[1]);
    pthread_t threads[4];
    for(int i=0;i<4;++i) assert(pthread_create(&threads[i],NULL,transfer,NULL)==0);
    for(int i=0;i<4;++i) pthread_join(threads[i],NULL);
    assert(DpTrafficSent() == 4096016 && DpTrafficReceived() == 4096016);
    puts("PASS: socket byte counts, errors, peeks, scatter/gather and concurrent transfers");
}
