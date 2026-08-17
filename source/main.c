#include <ft2build.h>
#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "display.h"
#include "v7.h"
#include "sockbuf.h"
#include "sockets.h"
#include FT_FREETYPE_H

#define RGBA(r,g,b,a) (((a) << 24) | ((b) << 16) | ((g) << 8) | (r))
#define COL_BG      RGBA(0xA2, 0x46, 0xF3, 0xFF)
#define COL_PANEL   RGBA(0x16, 0x21, 0x3E, 0xFF)
#define COL_HOVER   RGBA(0x2A, 0x3A, 0x5E, 0xFF)
#define COL_HEADER  RGBA(0x0F, 0x34, 0x60, 0xFF)
#define COL_WHITE   RGBA(0xFF, 0xFF, 0xFF, 0xFF)
#define COL_RED     RGBA(0xFF, 0x00, 0x00, 0xFF)
#define COL_BLACK   RGBA(0x00, 0x00, 0x00, 0xFF)

static FT_Library ft;
static FT_Face face;
static u32* framebuf;
static u32 framebuf_width;

const char* errmsg = "";
const char* errcode = "";

int s;
SOCKBUF_T sb;

Mix_Chunk* sfx_cache[16];
int sfx_count = 0;

int screen = 0;
char username[19];
char password[16];
char token[512];
int sock;
struct sockaddr_in server;

int ruleslinescroll = 0;
char *rules = NULL;

bool showpass = false;
int loginselection = 1;
bool loginAttempted = false;
char selectedRoom[23] = "general";

#define MAX_MESSAGES 20
#define MAX_MSG_LEN 350
char messages[MAX_MESSAGES][MAX_MSG_LEN];
int messageCount = 0;

void drawError(const char* message, const char* error_code) {
    drawText(10, 48, "oops, something went wrong :/", COL_RED, 50);
    drawText(10, 114, message, COL_WHITE, 35);
    drawText(10, 710, error_code, COL_WHITE, 22);
}

Mix_Chunk* loadSFX(const char* path) {
    Mix_Chunk* sfx = Mix_LoadWAV(path);
    if (sfx && sfx_count < 16) {
        sfx_cache[sfx_count++] = sfx;
    }
    return sfx;
}

void freeSFX() {
    for (int i = 0; i < sfx_count; i++) {
        Mix_FreeChunk(sfx_cache[i]);
        sfx_cache[i] = NULL;
    }
    sfx_count = 0;
}

typedef struct {
    Mix_Chunk* sfx;
    int fade_ms;
} SFXThreadData;

int sfxThreadFunc(void* data) {
    SFXThreadData* d = (SFXThreadData*)data;
    Mix_Chunk* sfx   = d->sfx;
    int fade_ms      = d->fade_ms;
    free(d);

    int full_vol  = MIX_MAX_VOLUME;
    int half_vol  = MIX_MAX_VOLUME / 2;
    int step_time = 10;
    int steps     = fade_ms / step_time;

    for (int i = 0; i <= steps; i++) {
        int vol = full_vol - (int)((float)i / steps * (full_vol - half_vol));
        Mix_VolumeMusic(vol);
        SDL_Delay(step_time);
    }

    Mix_VolumeChunk(sfx, MIX_MAX_VOLUME);
    int channel = Mix_PlayChannel(-1, sfx, 0);
    if (channel != -1) {
        while (Mix_Playing(channel)) {
            SDL_Delay(10);
        }
    }

    for (int i = 0; i <= steps; i++) {
        int vol = half_vol + (int)((float)i / steps * (full_vol - half_vol));
        Mix_VolumeMusic(vol);
        SDL_Delay(step_time);
    }

    Mix_VolumeMusic(full_vol);
    return 0;
}

void playSFX(Mix_Chunk* sfx, int fade_ms) {
    if (!sfx) return;

    SFXThreadData* d = malloc(sizeof(SFXThreadData));
    if (!d) return;
    d->sfx     = sfx;
    d->fade_ms = fade_ms;

    SDL_Thread* thread = SDL_CreateThread(sfxThreadFunc, "sfx_fade", d);
    if (thread) {
        SDL_DetachThread(thread);
    } else {
        free(d);
    }
}

void drawMainMenu(u64 kDown) {
    AppletOperationMode mode = appletGetOperationMode();
    HidTouchScreenState touchState;
    if (hidGetTouchScreenStates(&touchState, 1) > 0 && touchState.count > 0) {
        u32 tx = touchState.touches[0].x;
        u32 ty = touchState.touches[0].y;
        if (isPointInRect(tx, ty, 470, 447, 341, 83)) {
            screen = 2;
            return;
        }
    }
    if (kDown & HidNpadButton_A) {
        screen = 2;
        return;
    }

    if (mode == AppletOperationMode_Console) drawText(0, 24, "AuroraChat works better in handheld mode!", COL_WHITE, 24);
    drawText(1180, 715, "v26.7.14", COL_WHITE, 24);
    drawImage("romfs:/images/aurorachat.png", 383, 190);
    drawImage("romfs:/images/buttons/enter.png", 470, 447);
}

void loadRules() {
    FILE *file = fopen("romfs:/rules.txt", "r");
    rules = malloc(1186);
    fread(rules, 1, 1185, file);
    rules[1185] = '\0';
    fclose(file);
}

void drawRules(u64 kDown) {
    HidTouchScreenState touchState;
    if ((kDown & HidNpadButton_Up) && ruleslinescroll != 0) ruleslinescroll--;
    else if ((kDown & HidNpadButton_Down) && ruleslinescroll != 30) ruleslinescroll++;
    else if (kDown & HidNpadButton_A) {
        screen = 3;
        return;
    }
    if (hidGetTouchScreenStates(&touchState, 1) > 0 && touchState.count > 0) {
        u32 tx = touchState.touches[0].x;
        u32 ty = touchState.touches[0].y;
        if (isPointInRect(tx, ty, 524, 598, 232, 73)) {
            screen = 3;
            return;
        }
    }
    drawText(0, 24, "Use the D-Pad to scroll\nA to agree with the Code of Conduct", COL_WHITE, 24);
    drawText(375, 100, "Code of Conduct:", COL_WHITE, 64);
    drawImage("romfs:/images/boxes/rules.png", 439, 203);

    char *copy = strdup(rules);
    int linenum = 0;
    char *line = strtok(copy, "\n");
    while (line != NULL) {
        int y = (235 - (ruleslinescroll * 20)) + (linenum * 20);
        if (y >= 235 && y <= 545) {
            drawText(447, y, line, COL_BLACK, 20);
        }
        linenum++;
        line = strtok(NULL, "\n");
    }
    free(copy);

    drawImage("romfs:/images/buttons/done.png", 524, 598);
}

void login() {
    if (strlen(username) == 0 || strlen(password) == 0) {
        errmsg = "Invalid username or password";
        errcode = "INV_AUTH";
        screen = 1;
        return;
    }

    if (loginAttempted) return;
    loginAttempted = true;
    v7_loginOrRegister(s, "login", username, password);
    char login_errorcode[512] = {0};
    char login_banreason[512] = {0};

    switch(v7_loginOKCheck(&sb, login_errorcode, login_banreason, sizeof(login_errorcode), sizeof(login_banreason))) {
        case 1:
            errmsg = socket_error();
            errcode = "SOCKET_ERROR";
            screen = 1;
            socket_destroy(s);
            return;

        case 3:
            errmsg = login_errorcode;
            errcode = "LOGIN_ERROR";
            screen = 1;
            socket_destroy(s);
            return;

        case 4:
            char err_msg[512];
            snprintf(err_msg, sizeof(err_msg), "You are banned!\n%s", login_banreason);
            errmsg = err_msg;
            errcode = "BANNED";
            screen = 1;
            socket_destroy(s);
            return;

        default:
        break;
    }
    // Play SFX
    Mix_Chunk* signedup_sfx = loadSFX("romfs:/sfx/signedup.mp3");
    playSFX(signedup_sfx, 150);

    // Change Music
    Mix_Music *audio = Mix_LoadMUS("romfs:/music/bgm.mp3");
    if (!audio) {
        errmsg = Mix_GetError();
        errcode = "MIX_LOAD_FAIL";
        screen = 1;
    } else if (Mix_PlayMusic(audio, -1) < 0) {
        errmsg = Mix_GetError();
        errcode = "MIX_PLAY_FAIL";
        screen = 1;
    }
    Mix_PlayMusic(audio, -1);

    screen = 4;
}

void createAccount() {
    if (strlen(username) == 0 || strlen(password) == 0) {
        errmsg = "Invalid username or password";
        errcode = "INV_AUTH";
        screen = 1;
        return;
    }

    if (loginAttempted) return;
    loginAttempted = true;
    v7_loginOrRegister(s, "register", username, password);
    char login_errorcode[512] = {0};
    char login_banreason[512] = {0};

    switch(v7_loginOKCheck(&sb, login_errorcode, login_banreason, sizeof(login_errorcode), sizeof(login_banreason))) {
        case 1:
            errmsg = socket_error();
            errcode = "SOCKET_ERROR";
            screen = 1;
            socket_destroy(s);
            return;

        case 3:
            errmsg = login_errorcode;
            errcode = "REGISTER_ERROR";
            screen = 1;
            socket_destroy(s);
            return;

        case 4:
            char err_msg[512];
            snprintf(err_msg, sizeof(err_msg), "You are banned!\n%s", login_banreason);
            errmsg = err_msg;
            errcode = "BANNED";
            screen = 1;
            socket_destroy(s);
            return;

        default:
        break;
    }
    // Play SFX
    Mix_Chunk* signedup_sfx = loadSFX("romfs:/sfx/signedup.mp3");
    playSFX(signedup_sfx, 150);

    // Change Music
    Mix_Music *audio = Mix_LoadMUS("romfs:/music/bgm.mp3");
    if (!audio) {
        errmsg = Mix_GetError();
        errcode = "MIX_LOAD_FAIL";
        screen = 1;
    } else if (Mix_PlayMusic(audio, -1) < 0) {
        errmsg = Mix_GetError();
        errcode = "MIX_PLAY_FAIL";
        screen = 1;
    }
    Mix_PlayMusic(audio, -1);

    screen = 4;
}

void drawLogin(u64 kDown) {
    HidTouchScreenState touchState;
    AppletOperationMode mode = appletGetOperationMode();
    if (mode == AppletOperationMode_Console) drawText(0, 24, "Y to show/hide password\nA to type the username\nB to type the password\nUP to Log In\nDOWN to Create an Account", COL_WHITE, 24);
    if (kDown & HidNpadButton_Y) {
        showpass = !showpass;
    } else if (kDown & HidNpadButton_A) {
        char* result = openKeyboard(19, "Enter your username");
        if (result) {
            strncpy(username, result, sizeof(username) - 1);
            username[sizeof(username) - 1] = '\0';
            free(result);
        }
    } else if (kDown & HidNpadButton_B) {
        char* result = openKeyboard(19, "Enter your password");
        if (result) {
            strncpy(password, result, sizeof(password) - 1);
            password[sizeof(password) - 1] = '\0';
            free(result);
        }
    } else if (kDown & HidNpadButton_Up) {
        login();
    } else if (kDown & HidNpadButton_Down) {
        createAccount();
    }
    static bool showTouchWasDown = false;
    bool showTouchDown = false;
    if (hidGetTouchScreenStates(&touchState, 1) > 0 && touchState.count > 0) {
        u32 tx = touchState.touches[0].x;
        u32 ty = touchState.touches[0].y;
        if (isPointInRect(tx, ty, 956, 266, 84, 73)) {
            showTouchDown = true;
        } else if (isPointInRect(tx, ty, 240, 162, 800, 73)) {
            char* result = openKeyboard(19, "Enter your username");
            if (result) {
                strncpy(username, result, sizeof(username) - 1);
                username[sizeof(username) - 1] = '\0';
                free(result);
            }
        } else if (isPointInRect(tx, ty, 240, 266, 800, 73)) {
            char* result = openKeyboard(19, "Enter your password");
            if (result) {
                strncpy(password, result, sizeof(password) - 1);
                password[sizeof(password) - 1] = '\0';
                free(result);
            }
        } else if (isPointInRect(tx, ty, 524, 420, 232, 73)) {
            login();
        } else if (isPointInRect(tx, ty, 506, 516, 267, 51)) {
            createAccount();
        }
    }
    if (showTouchDown && !showTouchWasDown) showpass = !showpass;
    showTouchWasDown = showTouchDown;
    drawImage("romfs:/images/boxes/username.png", 240, 162);
    drawText(514, 211, username, COL_WHITE, 48);
    drawImage(showpass ? "romfs:/images/boxes/password_hide.png" : "romfs:/images/boxes/password.png", 240, 266);
    drawText(514, 315, showpass ? password : "****************", COL_WHITE, 48);
    drawImage("romfs:/images/buttons/login.png", 524, 420);
    drawImage("romfs:/images/buttons/createacc.png", 506, 516);
}

void drawRoomSelection(u64 kDown) {
    AppletOperationMode mode = appletGetOperationMode();
    HidTouchScreenState touchState;
    if (kDown & HidNpadButton_A) {
        screen = 5;
        return;
    } else if (kDown & HidNpadButton_Y) {
        char* result = openKeyboard(23, "Enter the room's name");
        if (result) {
            strncpy(selectedRoom, result, sizeof(selectedRoom) - 1);
            selectedRoom[sizeof(selectedRoom) - 1] = '\0';
            free(result);
        }
    }
    if (hidGetTouchScreenStates(&touchState, 1) > 0 && touchState.count > 0) {
        u32 tx = touchState.touches[0].x;
        u32 ty = touchState.touches[0].y;
        if (isPointInRect(tx, ty, 102, 266, 1073, 73)) {
            char* result = openKeyboard(23, "Enter the room's name");
            if (result) {
                strncpy(selectedRoom, result, sizeof(selectedRoom) - 1);
                selectedRoom[sizeof(selectedRoom) - 1] = '\0';
                free(result);
            }
            return;
        } else if (isPointInRect(tx, ty, 495, 373, 298, 73)) {
            screen = 5;
            return;
        }
    }
    if (mode == AppletOperationMode_Console) drawText(0, 24, "Y to Input the Room's name\nA to Enter the Room", COL_WHITE, 24);
    drawText(463, 48, "Room Selection", COL_WHITE, 48);
    drawImage("romfs:/images/boxes/room.png", 102, 266);
    drawText(119, 321, selectedRoom, COL_WHITE, 48);
    drawImage("romfs:/images/buttons/joinroom.png", 495, 373);
}

void drawChatScreen(u64 kDown) {
    HidTouchScreenState touchState;
    if (kDown & HidNpadButton_B) {
        memset(messages, 0, sizeof(messages));
        messageCount = 0;
        screen = 4;
    }
    if (kDown & HidNpadButton_Y) {
        char* result = openKeyboard(300, "Enter your message");
        if (result) {
            char* msg = result;
            char sender[512];
            snprintf(sender, sizeof(sender), "%s|%s|", msg, selectedRoom);
            char* networkresult = NULL;
            // TODO: network_request("http://104.236.25.60:6767/api/chat", &networkresult, "POST", sender, "text/plain", token);
            free(networkresult);
        }
    }
    if (hidGetTouchScreenStates(&touchState, 1) > 0 && touchState.count > 0) {
        u32 tx = touchState.touches[0].x;
        u32 ty = touchState.touches[0].y;
        if (isPointInRect(tx, ty, 0, 647, 1280, 73)) {
            char* result = openKeyboard(300, "Enter your message");
            if (result) {
                char* msg = result;
                char sender[512];
                snprintf(sender, sizeof(sender), "%s|%s|", msg, selectedRoom);
                char* networkresult = NULL;
                // TODO: network_request("http://104.236.25.60:6767/api/chat", &networkresult, "POST", sender, "text/plain", token);
                free(networkresult);
            }
        }
    }
    char title[128];
    snprintf(title, sizeof(title), "Chat Screen - %s", selectedRoom);
    drawText(0, 48, title, COL_WHITE, 48);
    drawText(1043, 24, "Y to send a message\nB to go back", COL_WHITE, 24);
    for (int i = 0; i < messageCount; i++) {
        drawText(10, 82 + (i * 24), messages[i], COL_WHITE, 24);
    }
    drawImage("romfs:/images/boxes/sendmessage.png", 0, 647);
}

void append_message(char* msg_username, char* msg, char* msg_room) {
    if (strcmp(msg_room, selectedRoom) != 0) return;
    if (messageCount >= MAX_MESSAGES) {
        for (int i = 0; i < MAX_MESSAGES - 1; i++) {
            memcpy(messages[i], messages[i+1], MAX_MSG_LEN);
        }
        messageCount = MAX_MESSAGES - 1;
    }
    snprintf(messages[messageCount], MAX_MSG_LEN, "<%s>: %s", msg_username, msg);
    messageCount++;
}

int main(int argc, char* argv[]) {
    romfsInit();

    FT_Init_FreeType(&ft);
    FT_New_Face(ft, "romfs:/fonts/OpenSans-Regular.ttf", 0, &face);
    display_setFace(face);

    NWindow* win = nwindowGetDefault();
    Framebuffer fb;
    framebufferCreate(&fb, win, 1280, 720, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&fb);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    s = socket_create("192.168.0.194", 7070);
    if (s == -1) {
        errmsg = "Failed to connect to server.";
        errcode = socket_error();
        screen = 1;
    }
    sockbuf_init(&sb, s);
    char servername[512] = {0}; // i dont need this but i also do for some reason
    v7_waitforhello(&sb, servername, sizeof(servername));

    loadRules();
    SDL_Init(SDL_INIT_AUDIO);
    Mix_Init(MIX_INIT_MP3);
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, MIX_DEFAULT_CHANNELS, 4096);
    Mix_Music *audio = Mix_LoadMUS("romfs:/music/setup.mp3");
    if (!audio) {
        errmsg = Mix_GetError();
        errcode = "MIX_LOAD_FAIL";
        screen = 1;
    } else if (Mix_PlayMusic(audio, -1) < 0) {
        errmsg = Mix_GetError();
        errcode = "MIX_PLAY_FAIL";
        screen = 1;
    }
    Mix_PlayMusic(audio, -1);
    
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus) break;

        u32 stride;
        framebuf = (u32*)framebufferBegin(&fb, &stride);
        framebuf_width = stride / sizeof(u32);
        display_setFramebuffer(framebuf, framebuf_width);
        clearScreen(COL_BG);

        char buffer[1024] = {0};
        ssize_t len = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (len > 0) {
            buffer[len] = '\0';
            char* username = strtok(buffer, "|");
            char* message  = strtok(NULL, "|");
            char* room     = strtok(NULL, "|");
            if (username && message && room) {
                append_message(username, message, room);
            }
        } else if (len == 0) {}
        
        if (screen == 0) {
            drawMainMenu(kDown);
        } else if (screen == 1) {
            drawError(errmsg, errcode);
        } else if (screen == 2) {
            drawRules(kDown);
        } else if (screen == 3) {
            drawLogin(kDown);
        } else if (screen == 4) {
            drawRoomSelection(kDown);
        } else if (screen == 5) {
            drawChatScreen(kDown);
        } else {
            drawError("Invalid screen value", "SCR_VAL_INV");
        }

        framebufferEnd(&fb);
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    Mix_FreeMusic(audio);
    SDL_Quit();
    framebufferClose(&fb);
    socketExit();
    romfsExit();
    return 0;
}