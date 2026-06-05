#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "ui.h"

static UI::Context ui;

// ---- network ----

struct MemoryStruct { char* memory; size_t size; };

static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    MemoryStruct* mem = (MemoryStruct*)userp;
    char* ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->memory = ptr;
    memcpy(&mem->memory[mem->size], contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

static void network_request(const char* url, char** result, const char* method,
                             const char* body, const char* content_type, const char* authorization) {
    MemoryStruct chunk;
    chunk.memory = (char*)malloc(1);
    chunk.size   = 0;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL* curl = curl_easy_init();
    if (curl) {
        struct curl_slist* headers = NULL;
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "aurorachat-switch/6.0");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        }
        if (content_type) {
            char h[128];
            snprintf(h, sizeof(h), "Content-Type: %s", content_type);
            headers = curl_slist_append(headers, h);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }
        if (authorization) {
            char h[512];
            snprintf(h, sizeof(h), "auth: %s", authorization);
            headers = curl_slist_append(headers, h);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) { free(chunk.memory); *result = NULL; }
        else                  *result = chunk.memory;
        if (headers) curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    curl_global_cleanup();
}

// ---- keyboard ----

static char* openKeyboard(int maxlen, const char* guideText) {
    SwkbdConfig kbd;
    char* result = (char*)malloc(256);
    if (!result) return NULL;
    Result rc = swkbdCreate(&kbd, 0);
    if (R_SUCCEEDED(rc)) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetInitialText(&kbd, "");
        swkbdConfigSetGuideText(&kbd, guideText);
        swkbdConfigSetStringLenMax(&kbd, maxlen);
        rc = swkbdShow(&kbd, result, 256);
        swkbdClose(&kbd);
        if (R_SUCCEEDED(rc)) return result;
    }
    free(result);
    return NULL;
}

// ---- audio ----

static Mix_Chunk* sfx_cache[16];
static int sfx_count = 0;

static Mix_Chunk* loadSFX(const char* path) {
    Mix_Chunk* sfx = Mix_LoadWAV(path);
    if (sfx && sfx_count < 16) sfx_cache[sfx_count++] = sfx;
    return sfx;
}

static void freeSFX() {
    for (int i = 0; i < sfx_count; i++) { Mix_FreeChunk(sfx_cache[i]); sfx_cache[i] = NULL; }
    sfx_count = 0;
}

typedef struct { Mix_Chunk* sfx; int fade_ms; } SFXThreadData;

static int sfxThreadFunc(void* data) {
    SFXThreadData* d = (SFXThreadData*)data;
    Mix_Chunk* sfx = d->sfx;
    int fade_ms    = d->fade_ms;
    free(d);
    int full_vol = MIX_MAX_VOLUME, half_vol = MIX_MAX_VOLUME / 2;
    int steps = fade_ms / 10;
    for (int i = 0; i <= steps; i++) {
        Mix_VolumeMusic(full_vol - (int)((float)i / steps * (full_vol - half_vol)));
        SDL_Delay(10);
    }
    Mix_VolumeChunk(sfx, MIX_MAX_VOLUME);
    int ch = Mix_PlayChannel(-1, sfx, 0);
    if (ch != -1) while (Mix_Playing(ch)) SDL_Delay(10);
    for (int i = 0; i <= steps; i++) {
        Mix_VolumeMusic(half_vol + (int)((float)i / steps * (full_vol - half_vol)));
        SDL_Delay(10);
    }
    Mix_VolumeMusic(full_vol);
    return 0;
}

static void playSFX(Mix_Chunk* sfx, int fade_ms) {
    if (!sfx) return;
    SFXThreadData* d = (SFXThreadData*)malloc(sizeof(SFXThreadData));
    if (!d) return;
    d->sfx = sfx; d->fade_ms = fade_ms;
    SDL_Thread* t = SDL_CreateThread(sfxThreadFunc, "sfx", d);
    if (t) SDL_DetachThread(t); else free(d);
}

// ---- state ----

static int screen = 0; // 0=main 1=create 2=login 3=error 4=rooms 5=chat
static const char* username = "";
static const char* password = "";
static char token[512];
static int sock;
static struct sockaddr_in server_addr;
static const char* errmsg = "";
static const char* errcode = "";

static char*  roomresult   = NULL;
static char** rooms        = NULL;
static int    roomcount    = 0;
static char*  selectedRoom = (char*)"";

static int  mainmenusel    = 1;
static int  loginsel       = 1;
static bool loginAttempted = false;
static int  roomsel        = 1;

#define MAX_MESSAGES 26
#define MAX_MSG_LEN  350
static char messages[MAX_MESSAGES][MAX_MSG_LEN];
static int  messageCount = 0;

static void parseRooms(const char* data) {
    if (!data) return;
    if (rooms) {
        for (int i = 0; i < roomcount; i++) free(rooms[i]);
        free(rooms); rooms = NULL; roomcount = 0;
    }
    char* buf = strdup(data);
    char* tok = strtok(buf, "|");
    if (tok) {
        roomcount = atoi(tok);
        if (roomcount > 0) {
            rooms = (char**)malloc(roomcount * sizeof(char*));
            for (int i = 0; i < roomcount; i++) {
                tok = strtok(NULL, "|");
                rooms[i] = tok ? strdup(tok) : strdup("Unknown");
            }
        }
    }
    free(buf);
}

static void append_message(const char* uname, const char* msg, const char* room) {
    if (strcmp(room, selectedRoom) != 0) return;
    if (messageCount >= MAX_MESSAGES) {
        for (int i = 0; i < MAX_MESSAGES - 1; i++) memcpy(messages[i], messages[i+1], MAX_MSG_LEN);
        messageCount = MAX_MESSAGES - 1;
    }
    snprintf(messages[messageCount++], MAX_MSG_LEN, "<%s>: %s", uname, msg);
}

// ---- layout helpers ----

// Window content area offsets: title bar (22) + 3px outer + 3px outer + 2px separator + 4px padding
static inline int winCY(int wy) { return wy + 3 + UI::W98::TitleH + 6; }
static inline int winCX(int wx) { return wx + 8; }
static inline int winCW(int ww) { return ww - 16; }

static constexpr SDL_Color COL_BLACK = {0,   0,   0,   255};
static constexpr SDL_Color COL_RED   = {180, 0,   0,   255};
static constexpr SDL_Color COL_GRAY  = {80,  80,  80,  255};

// ---- hint sets ----

static const UI::HintPair kHintsMain[] = {
    {UI::Action::Navigate, "Navigate"},
    {UI::Action::Confirm,  "Select"},
};
static const UI::HintPair kHintsForm[] = {
    {UI::Action::Navigate, "Navigate"},
    {UI::Action::Confirm,  "Select / Enter"},
    {UI::Action::Back,     "Back"},
};
static const UI::HintPair kHintsChat[] = {
    {UI::Action::Tab,  "Type"},
    {UI::Action::Back, "Leave"},
};
static const UI::HintPair kHintsError[] = {
    {UI::Action::Confirm, "OK"},
    {UI::Action::Back,    "OK"},
};

// ---- screens ----

static void doMainMenu(bool kA, bool kUp, bool kDown) {
    if (kDown && ++mainmenusel > 2) mainmenusel = 1;
    if (kUp   && --mainmenusel < 1) mainmenusel = 2;
    if (kA) screen = mainmenusel;

    ui.drawDesktop();

    const int wx=340, wy=230, ww=600, wh=200;
    ui.drawWin98Window(wx, wy, ww, wh, "AuroraChat Switch");
    const int cy = winCY(wy);
    const int bx = wx + (ww - 260) / 2;
    ui.win98Button(0, "Create Account", bx, cy + 22, 260, 30, mainmenusel == 1);
    ui.win98Button(1, "Log In",         bx, cy + 66, 260, 30, mainmenusel == 2);

    ui.drawHintBar(kHintsMain, 2);
}

static void doLoginCreate(bool kA, bool kB, bool kUp, bool kDown, bool isCreate) {
    if (kDown && ++loginsel > 3) loginsel = 1;
    if (kUp   && --loginsel < 1) loginsel = 3;

    if (kB) { screen = 0; loginsel = 1; loginAttempted = false; return; }

    if (kA) {
        if (loginsel == 1) {
            char* r = openKeyboard(255, "Enter your username");
            if (r) username = r;
        } else if (loginsel == 2) {
            char* r = openKeyboard(255, "Enter your password");
            if (r) password = r;
        } else {
            if (!strlen(username) || !strlen(password)) {
                errmsg = "Username and password cannot be empty.";
                errcode = "INV_AUTH"; screen = 3; return;
            }
            if (loginAttempted) return;
            loginAttempted = true;

            char sender[512];
            snprintf(sender, sizeof(sender), "%s|%s|", username, password);
            char* res = NULL;
            const char* ep = isCreate
                ? "http://104.236.25.60:6767/api/signup"
                : "http://104.236.25.60:6767/api/login";
            for (int i = 0; i < 3 && !res; i++)
                network_request(ep, &res, "POST", sender, "text/plain", NULL);

            if (!res) {
                errmsg = "The server never responded.";
                errcode = "SRV_UNREACH"; loginAttempted = false; screen = 3; return;
            }
            if (strstr(res, isCreate ? "ERR_USER_USED" : "ERR_WRONG_PASS")) {
                errmsg = isCreate ? "That username is already taken."
                                  : "Wrong password. Try again.";
                errcode = isCreate ? "USER_USED" : "WRONG_PASS";
                free(res); loginAttempted = false; screen = 3; return;
            }

            char buf[1024];
            strncpy(buf, res, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
            free(res);
            char* parsed = strtok(buf, "|");
            if (!parsed) {
                errmsg = "Invalid response from server.";
                errcode = "BAD_TOKEN"; loginAttempted = false; screen = 3; return;
            }
            strncpy(token, parsed, sizeof(token)-1); token[sizeof(token)-1] = '\0';

            char* roomres = NULL;
            network_request("http://104.236.25.60:6767/api/rooms", &roomres, "POST", NULL, NULL, NULL);
            roomresult = roomres;

            playSFX(loadSFX("romfs:/sfx/signedup.mp3"), 150);
            screen = 4;
        }
    }

    ui.drawDesktop();
    const char* title  = isCreate ? "Create Account" : "Log In";
    const char* submit = isCreate ? "Create Account" : "Log In";

    const int wx=290, wy=190, ww=700, wh=260;
    ui.drawWin98Window(wx, wy, ww, wh, title);
    const int cy = winCY(wy);
    const int bx = wx + 20, bw = ww - 40;

    // Username row
    char ufield[128];
    if (strlen(username))
        snprintf(ufield, sizeof(ufield), "Username:  %s", username);
    else
        snprintf(ufield, sizeof(ufield), "Username:");
    ui.win98Button(0, ufield, bx, cy + 8, bw, 32, loginsel == 1);

    // Password row (mask with asterisks)
    int plen = (int)strlen(password);
    char stars[65] = "";
    for (int i = 0; i < plen && i < 64; i++) stars[i] = '*';
    stars[plen] = '\0';
    char pfield[128];
    if (plen)
        snprintf(pfield, sizeof(pfield), "Password:  %s", stars);
    else
        snprintf(pfield, sizeof(pfield), "Password:");
    ui.win98Button(1, pfield, bx, cy + 52, bw, 32, loginsel == 2);

    // Submit button
    const int sbw = 200;
    ui.win98Button(2, submit, wx + (ww - sbw)/2, cy + 104, sbw, 28, loginsel == 3);

    ui.drawHintBar(kHintsForm, 3);
}

static void doError(bool kA, bool kB) {
    if (kA || kB) { screen = 0; loginsel = 1; loginAttempted = false; return; }

    ui.drawDesktop();
    const int wx=160, wy=230, ww=960, wh=210;
    ui.drawWin98Window(wx, wy, ww, wh, "Error");
    const int cy = winCY(wy);
    const int cx = winCX(wx);
    ui.drawText("oops, something went wrong :/", cx, cy + 8,  18, COL_RED);
    ui.drawText(errmsg,                          cx, cy + 40, 15, COL_BLACK);
    ui.drawText(errcode,                         cx, cy + 70, 12, COL_GRAY);

    ui.drawHintBar(kHintsError, 2);
}

static void doRoomSelection(bool kA, bool kUp, bool kDown) {
    if (roomresult && !rooms) { parseRooms(roomresult); roomsel = 1; }

    if (kDown && ++roomsel > roomcount) roomsel = 1;
    if (kUp   && --roomsel < 1)        roomsel = roomcount;
    if (kA && rooms && roomcount > 0)  { selectedRoom = rooms[roomsel-1]; screen = 5; }

    ui.drawDesktop();
    const int wx=300, wy=50, ww=680, wh=600;
    ui.drawWin98Window(wx, wy, ww, wh, "Room Selection");
    const int cy = winCY(wy);
    const int bx = wx + 8, bw = ww - 16;

    if (rooms && roomcount > 0) {
        for (int i = 0; i < roomcount; i++)
            ui.win98Button(i, rooms[i], bx, cy + i * 38, bw, 32, (i+1 == roomsel));
    } else {
        ui.drawText("Loading rooms...", bx, cy + 10, 14, COL_GRAY);
    }

    static const UI::HintPair hints[] = {
        {UI::Action::Navigate, "Navigate"},
        {UI::Action::Confirm,  "Join"},
    };
    ui.drawHintBar(hints, 2);
}

static void doChatScreen(bool kB, bool kY) {
    if (kB) {
        memset(messages, 0, sizeof(messages));
        messageCount = 0;
        screen = 4;
        return;
    }
    if (kY) {
        char* result = openKeyboard(300, "Enter your message");
        if (result) {
            char sender[512];
            snprintf(sender, sizeof(sender), "%s|%s|", result, selectedRoom);
            char* nr = NULL;
            network_request("http://104.236.25.60:6767/api/chat", &nr, "POST", sender, "text/plain", token);
            free(nr);
            free(result);
        }
    }

    ui.drawDesktop();

    char title[128];
    snprintf(title, sizeof(title), "Chat - #%s", selectedRoom);

    const int wx=20, wy=20, ww=1240, wh=640;
    ui.drawWin98Window(wx, wy, ww, wh, title);
    const int cy = winCY(wy);
    const int cx = winCX(wx);

    for (int i = 0; i < messageCount; i++)
        ui.drawText(messages[i], cx, cy + i * 22, 13, COL_BLACK);

    ui.drawWin98StatusBar(SCREEN_H - 22, "[X] Type a message    [A] Leave room");
}

// ---- main ----

int main(int argc, char* argv[]) {
    romfsInit();

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
    TTF_Init();

    SDL_Window* window = SDL_CreateWindow("AuroraChat Switch",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);

    ui.init(renderer);

    SDL_GameController* ctrl = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) { ctrl = SDL_GameControllerOpen(i); break; }
    }

    Mix_Init(MIX_INIT_MP3);
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, MIX_DEFAULT_CHANNELS, 4096);
    Mix_Music* bgm = Mix_LoadMUS("romfs:/music/loop.mp3");
    if (!bgm)                    { errmsg = Mix_GetError(); errcode = "MIX_LOAD"; screen = 3; }
    else if (Mix_PlayMusic(bgm, -1) < 0) { errmsg = Mix_GetError(); errcode = "MIX_PLAY"; screen = 3; }

    socketInitializeDefault();
    sock = socket(AF_INET, SOCK_STREAM, 0);
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(3033);
    server_addr.sin_addr.s_addr = inet_addr("104.236.25.60");
    connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    int nb = 1;
    ioctl(sock, FIONBIO, &nb);

    Uint32 lastTick = SDL_GetTicks();

    while (appletMainLoop()) {
        bool kA=false, kB=false, kY=false, kUp=false, kDown=false, kPlus=false;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) goto done;
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A:         kA    = true; break;
                    case SDL_CONTROLLER_BUTTON_B:         kB    = true; break;
                    case SDL_CONTROLLER_BUTTON_Y:         kY    = true; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:   kUp   = true; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: kDown = true; break;
                    case SDL_CONTROLLER_BUTTON_START:     kPlus = true; break;
                }
            }
        }
        if (kPlus) break;

        Uint32 now = SDL_GetTicks();
        float dt = (now - lastTick) / 1000.0f;
        lastTick = now;

        ui.beginFrame(dt, true);
        SDL_SetRenderDrawColor(renderer, UI::W98::Desktop.r, UI::W98::Desktop.g, UI::W98::Desktop.b, 255);
        SDL_RenderClear(renderer);

        // receive incoming messages
        char buf[1024] = {0};
        ssize_t len = recv(sock, buf, sizeof(buf)-1, 0);
        if (len > 0) {
            buf[len] = '\0';
            char* uname = strtok(buf, "|");
            char* msg   = strtok(NULL, "|");
            char* room  = strtok(NULL, "|");
            if (uname && msg && room) append_message(uname, msg, room);
        }

        switch (screen) {
            case 0: doMainMenu(kA, kUp, kDown); break;
            case 1: doLoginCreate(kA, kB, kUp, kDown, true); break;
            case 2: doLoginCreate(kA, kB, kUp, kDown, false); break;
            case 3: doError(kA, kB); break;
            case 4: doRoomSelection(kA, kUp, kDown); break;
            case 5: doChatScreen(kB, kY); break;
            default: errmsg = "Invalid screen"; errcode = "SCR_VAL_INV"; screen = 3; break;
        }

        ui.endFrame();
        SDL_RenderPresent(renderer);
    }

done:
    ui.shutdown();
    if (ctrl) SDL_GameControllerClose(ctrl);
    freeSFX();
    if (bgm) Mix_FreeMusic(bgm);
    Mix_CloseAudio();
    Mix_Quit();
    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    close(sock);
    socketExit();
    romfsExit();
    return 0;
}
