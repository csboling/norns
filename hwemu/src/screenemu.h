#pragma once

#include <png.h>
#include <unistd.h>

struct buffer_state {
    char *buf;
    size_t len;
};

struct screen_emu {
    int fd;
    int xres;
    int yres;
    int bits_per_pixel;
    useconds_t poll_interval;

    char *fb_mem;
    size_t fb_size;

    struct buffer_state outbuf;
    png_structp png_write;
    png_infop png_info;
    png_bytep* row_pointers;
};

struct screen_emu* screen_emu_init(void);
void* screen_emu_poll(void* screen);
