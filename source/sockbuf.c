#include <sockbuf.h>
#include <sockets.h>

void sockbuf_init(SOCKBUF_T *sb, int s) {
    sb->socket = s;
    sb->filled = 0;
}

size_t sockbuf_fill_nonblock(SOCKBUF_T *sb) {
    size_t empty = SOCKBUF_SIZE - sb->filled;
    if(!empty) return 0;
    size_t recvd = socket_recv_nonblock(sb->socket, sb->data + sb->filled, empty);
    if(recvd == -1) {
        return -1;
    }
    sb->filled += recvd;
    return recvd;
}

size_t sockbuf_fill(SOCKBUF_T *sb) {
    if(sb->filled)
        return sockbuf_fill_nonblock(sb);
    
    size_t recvd = socket_recv(sb->socket, sb->data, SOCKBUF_SIZE);
    if(recvd == -1) return -1;
    sb->filled = recvd;
    return recvd;
}

size_t sockbuf_getline_generic(size_t (*fillfunc)(SOCKBUF_T *sb), SOCKBUF_T *sb, char *buf, size_t n) {
    size_t recvd = fillfunc(sb);
    if(recvd == -1 && !sb->filled) return -1;
    n = n < sb->filled ? n : sb->filled;
    if(n == 0) return 0;
    
    size_t ptr = 0;
    while(ptr < n && (sb->data[ptr] != '\n')) {
        buf[ptr] = sb->data[ptr];
        ptr++;
    }
    if(sb->data[ptr] == '\n')
        ptr++;

    size_t left = sb->filled - ptr;
    for(size_t i=0;i<left;i++) {
        sb->data[i] = sb->data[i + ptr];
    }
    sb->filled = left;
    return ptr;
}

size_t sockbuf_getline(SOCKBUF_T *sb, char *buf, size_t n) {
    return sockbuf_getline_generic(&sockbuf_fill, sb, buf, n);
}

size_t sockbuf_getline_nonblock(SOCKBUF_T *sb, char *buf, size_t n) {
    return sockbuf_getline_generic(&sockbuf_fill_nonblock, sb, buf, n);
}