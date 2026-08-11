#include <switch.h>
#include FT_FREETYPE_H

#ifndef DISPLAY_H
#define DISPLAY_H

void display_setFace(FT_Face f);
void display_setFramebuffer(u32* fb, u32 width);
void drawPixel(int x, int y, u32 color);
void drawRect(int x, int y, int w, int h, u32 color);
void clearScreen(u32 color);
void drawGlyph(int x, int y, FT_Bitmap* bmp, u32 color);
void drawImage(const char* path, int x, int y);
void drawText(int x, int y, const char* text, u32 color, int size);
bool isPointInRect(int px, int py, int rx, int ry, int rw, int rh);
char* openKeyboard(int maxlen, const char* guideText);

#endif