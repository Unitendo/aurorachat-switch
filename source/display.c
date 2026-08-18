#include <ft2build.h>
#include <stdlib.h>
#include <switch.h>
#include <png.h>
#include <setjmp.h>
#include FT_FREETYPE_H

#define RGBA(r,g,b,a) (((a) << 24) | ((b) << 16) | ((g) << 8) | (r))
static u32* framebuf;
static u32 framebuf_width;
static FT_Face face;

void display_setFace(FT_Face f) {
    face = f;
}

void display_setFramebuffer(u32* fb, u32 width) {
    framebuf = fb;
    framebuf_width = width;
}

void drawPixel(int x, int y, u32 color) {
    if (x >= 0 && x < 1280 && y >= 0 && y < 720)
        framebuf[y * framebuf_width + x] = color;
}

void drawRect(int x, int y, int w, int h, u32 color) {
    for (int row = y; row < y + h && row < 720; row++)
        for (int col = x; col < x + w && col < 1280; col++)
            if (row >= 0 && col >= 0)
                framebuf[row * framebuf_width + col] = color;
}

void clearScreen(u32 color) {
    for (int y = 0; y < 720; y++)
        for (int x = 0; x < 1280; x++)
            framebuf[y * framebuf_width + x] = color;
}

void drawGlyph(int x, int y, FT_Bitmap* bmp, u32 color) {
    u8 cr = (color >>  0) & 0xFF;
    u8 cg = (color >>  8) & 0xFF;
    u8 cb = (color >> 16) & 0xFF;

    for (int row = 0; row < (int)bmp->rows; row++) {
        for (int col = 0; col < (int)bmp->width; col++) {
            int px = x + col;
            int py = y + row;
            if (px < 0 || px >= 1280 || py < 0 || py >= 720) continue;

            u8 alpha = bmp->buffer[row * bmp->pitch + col];
            if (alpha == 0) continue;

            u32 existing = framebuf[py * framebuf_width + px];
            u8 er = (existing >>  0) & 0xFF;
            u8 eg = (existing >>  8) & 0xFF;
            u8 eb = (existing >> 16) & 0xFF;

            u8 a = alpha;
            u8 r = (cr * a + er * (255 - a)) / 255;
            u8 g = (cg * a + eg * (255 - a)) / 255;
            u8 b = (cb * a + eb * (255 - a)) / 255;

            framebuf[py * framebuf_width + px] = RGBA(r, g, b, 0xFF);
        }
    }
}

void drawImage(const char* path, int x, int y) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return;
    }
    png_init_io(png, fp);
    png_read_info(png, info);

    int width = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE) png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);

    png_read_update_info(png, info);
    png_bytep* row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
    for(int i = 0; i < height; i++) row_pointers[i] = (png_byte*)malloc(png_get_rowbytes(png,info));
    png_read_image(png, row_pointers);

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            int px = x + col;
            int py = y + row;
            if (px < 0 || px >= 1280 || py < 0 || py >= 720) continue;
            png_byte* pixel = &(row_pointers[row][col * 4]);
            u8 r = pixel[0];
            u8 g = pixel[1];
            u8 b = pixel[2];
            u8 a = pixel[3];
            if (a == 0) continue;
            u32 existing = framebuf[py * framebuf_width + px];
            u8 er = (existing >>  0) & 0xFF;
            u8 eg = (existing >>  8) & 0xFF;
            u8 eb = (existing >> 16) & 0xFF;
            u8 dr = (r * a + er * (255 - a)) / 255;
            u8 dg = (g * a + eg * (255 - a)) / 255;
            u8 db = (b * a + eb * (255 - a)) / 255;
            framebuf[py * framebuf_width + px] = RGBA(dr, dg, db, 0xFF);
        }
    }
    for (int i = 0; i < height; i++) free(row_pointers[i]);
    free(row_pointers);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
}

void drawText(int x, int y, const char* text, u32 color, int size) {
    FT_Set_Pixel_Sizes(face, 0, size);

    int cx = x;
    int cy = y;

    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            cx = x;
            cy += size + 4;
            continue;
        }

        if (FT_Load_Char(face, *p, FT_LOAD_RENDER)) continue;

        FT_GlyphSlot g = face->glyph;
        int bx = cx + g->bitmap_left;
        int by = cy - g->bitmap_top;

        drawGlyph(bx, by, &g->bitmap, color);

        cx += g->advance.x >> 6;
    }
}

bool isPointInRect(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

char* openKeyboard(int maxlen, const char* guideText) {
    Result rc = 0;
    SwkbdConfig kbd;
    char* result = malloc(256);
    
    if (!result) return NULL;
    
    rc = swkbdCreate(&kbd, 0);
    if (R_SUCCEEDED(rc)) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetInitialText(&kbd, "");
        swkbdConfigSetGuideText(&kbd, guideText);
        swkbdConfigSetStringLenMax(&kbd, maxlen);
        rc = swkbdShow(&kbd, result, 256);
        swkbdClose(&kbd);
        
        if (R_SUCCEEDED(rc)) {
            return result;
        }
    }
    
    free(result);
    return NULL;
}
