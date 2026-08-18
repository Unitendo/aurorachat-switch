#ifndef SOCKETS_H
#define SOCKETS_H

#include <stddef.h>

extern int socket_create(const char *ip, short port);
extern void socket_destroy(int s);
extern const char *socket_error();
extern size_t socket_recv(int s, void *b, size_t n);
extern size_t socket_recv_nonblock(int s, void *b, size_t n);
extern size_t socket_send(int s, const void *b, size_t n);
extern int socket_wouldveblocked();
extern int socket_stillalive(int s);

#endif