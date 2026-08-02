// thank you jakub for this humble code
#include <v7.h>
#include <sockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int v7_waitforhello(SOCKBUF_T *sb, char *servername, size_t sname_n) {
    char buffer[1024] = {0};
    
    if(sockbuf_getline(sb, buffer, sizeof(buffer) - 1) == -1)
        return 1;

    char *token = strtok(buffer, "|");
    if(!token) return 2;
    if(strncmp(token, "hello", sizeof(buffer)))
        return 2;

    token = strtok(NULL, "|");
    if(!token) return 2;
    if(strncmp(token, "v7", sizeof(buffer)))
        return 2;

    servername[0] = 0;
    token = strtok(NULL, "|");
    if(!token) return 0;
    strncpy(servername, token, sname_n);
    servername[sname_n - 1] = 0;
    
    return 0;
}

void v7_encode(char *dst, const char *src, size_t n) {
    int i = 0, j = 0;
    char c;
    while( (c = src[i]) ) { 
        if(j >= n - 1) break;
        switch(c) {
            case '%':
                j += 3;
                if(j >= n - 1) break;
                dst[j - 3] = '%';
                dst[j - 2] = '2';
                dst[j - 1] = '5';
            break;

            case '|':
                j += 3;
                if(j >= n - 1) break;
                dst[j - 3] = '%';
                dst[j - 2] = '7';
                dst[j - 1] = 'C';
            break;

            case '\n':
                j += 3;
                if(j >= n - 1) break;
                dst[j - 3] = '%';
                dst[j - 2] = '0';
                dst[j - 1] = 'A';
            break;

            default:
                dst[j] = c;
                j++;
        }
        i++;
    }
    dst[j] = 0;
    dst[n - 1] = 0;
}

void v7_decode(char *dst, const char *src, size_t n) {
    int i = 0, j = 0;
    char c;
    char b[3] = {0};
    while( (c = src[i]) ) { 
        if(j >= n - 1) break;
        switch(c) {
            case '%':
                i++;
                b[0] = src[i];
                i++;
                b[1] = src[i];
                dst[j] = strtol(b, NULL, 16);
                j++;
            break;

            default:
                dst[j] = c;
                j++;
        }
        i++;
    }
    dst[j] = 0;
    dst[n - 1] = 0;
}

void v7_sendRulesRequest(int s) {
    const char *buffer = "rules|\n";
    socket_send(s, buffer, strlen(buffer));
}

int v7_getRulesResponse(SOCKBUF_T *sb, char *rules, size_t rules_n) {
    char buffer[16384] = {0};

    if(sockbuf_getline(sb, buffer, sizeof(buffer) - 1) == -1)
        return 1;

    char *token = strtok(buffer, "|");
    if(!token) return 2;
    if(strncmp(token, "rules", sizeof(buffer)))
        return 2;

    rules[0] = 0;
    token = strtok(NULL, "|");
    if(!token) return 2;
    v7_decode(rules, token, rules_n);
    rules[rules_n - 1] = 0;
    return 0;
}

void v7_loginOrRegister(int s, const char *cmd, const char *login, const char *passwd) {
    char buffer[1024] = {0};

    char login_encoded[64] = {0};
    char passwd_encoded[64] = {0};

    v7_encode(login_encoded, login, sizeof(login_encoded));
    v7_encode(passwd_encoded, passwd, sizeof(passwd_encoded));

    snprintf(buffer, sizeof(buffer) - 1, "%s|%s|%s|\n", cmd, login_encoded, passwd_encoded);
    socket_send(s, buffer, strlen(buffer));
}

int v7_loginOKCheck(SOCKBUF_T *sb, char *err, char *banmsg, size_t err_n, size_t ban_n) {
    char buffer[1024] = {0};

    if(sockbuf_getline(sb, buffer, sizeof(buffer) - 1) == -1)
        return 1;

    char *token = strtok(buffer, "|");
    if(!token) return 2;
    if(!strncmp(token, "ok", sizeof(buffer)))
        return 0;

    if(strncmp(token, "err", sizeof(buffer)))
        return 2;

    err[0] = 0;
    token = strtok(NULL, "|");
    if(!token) return 3;
    v7_decode(err, token, err_n);
    err[err_n - 1] = 0;
    if(!strncmp(token, "banned", sizeof(buffer))) {
        token = strtok(NULL, "|");
        if(!token) return 3;
        v7_decode(banmsg, token, ban_n);
        return 4;
    }
    return 3;
}

void v7_joinRoom(int s, const char *room) {
    char buffer[1024] = {0};

    char room_encoded[64] = {0};
    v7_encode(room_encoded, room, sizeof(room_encoded));

    snprintf(buffer, sizeof(buffer) - 1, "join|%s|\nhistory|2048|\n", room_encoded);
    socket_send(s, buffer, strlen(buffer));
}

void v7_sendMsg(int s, const char *msg) {
    char buffer[4096] = {0};

    char msg_encoded[2048] = {0};
    v7_encode(msg_encoded, msg, sizeof(msg_encoded));

    snprintf(buffer, sizeof(buffer) - 1, "msg|%s|\n", msg_encoded);
    socket_send(s, buffer, strlen(buffer));
}