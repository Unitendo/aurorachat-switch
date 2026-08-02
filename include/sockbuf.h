#ifndef SOCKBUF_H
#define SOCKBUF_H

#include <stddef.h>

#define SOCKBUF_SIZE 65536

typedef struct SockBuf {
    int socket;
    char data[SOCKBUF_SIZE];
    size_t filled;
} SOCKBUF_T;

extern void sockbuf_init(SOCKBUF_T *sb, int s);
extern size_t sockbuf_fill_nonblock(SOCKBUF_T *sb);
extern size_t sockbuf_fill(SOCKBUF_T *sb);
extern size_t sockbuf_getline(SOCKBUF_T *sb, char *buf, size_t n);
extern size_t sockbuf_getline_nonblock(SOCKBUF_T *sb, char *buf, size_t n);

#endif