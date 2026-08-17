#include <sockets.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <switch.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int socket_create(const char *ip, short port) {
    socketInitializeDefault();
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if(s == -1) return -1;

    struct sockaddr_in addr;
    if(inet_pton(AF_INET, ip, &addr.sin_addr) != 1) return -1;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if(connect(s, (const struct sockaddr *) &addr, sizeof(addr))) return -1;
    
    return s;
}

void socket_destroy(int s) {
    close(s);
}

const char *socket_error() {
    return strerror(errno);
}

size_t socket_recv(int s, void *b, size_t n) {
    return recv(s, b, n, 0);
}

size_t socket_recv_nonblock(int s, void *b, size_t n) {
    return recv(s, b, n, MSG_DONTWAIT);
}

size_t socket_send(int s, const void *b, size_t n) {
    return send(s, b, n, 0);
}

int socket_wouldveblocked() {
    if(errno == EAGAIN) return 1;
    if(errno == EWOULDBLOCK) return 1;
    return 0;
}

int socket_stillalive(int s) {
    int err = 0;
    socklen_t err_size = sizeof(err);
    getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &err_size);
    return !err;
}