#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/fb.h>
#include <png.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "args.h"
#include "base64.h"

static const char json_hdr[] =
    "{\"type\":\"fb\",\"data\":\"data:image/png;base64,";
static const char json_ftr[] =
    "\"}";

struct buffer_state {
    char *buf;
    size_t len;
};

struct screen_emu {
    int fd;
    int xres;
    int yres;
    int bits_per_pixel;
    char *fb_mem;
    size_t fb_size;
    png_structp png_write;
    png_infop png_info;
    struct buffer_state outbuf;

    png_bytep* row_pointers;
};

void png_write_to_buf(png_structp png_ptr, png_bytep buf, png_size_t len) {
    struct buffer_state *state = png_get_io_ptr(png_ptr);
    size_t nsize = state->len + len;

    if (state->buf) {
        state->buf = realloc(state->buf, nsize);
    } else {
        state->buf = malloc(nsize);
    }

    if (!state->buf) {
        fprintf(stderr, "alloc failed: %s\n", strerror(errno));
        png_error(png_ptr, "alloc failed");
    }

    memcpy(state->buf + state->len, buf, len);
    state->len += len;
}

static struct screen_emu* screen_emu_init() {
    struct screen_emu *screen = malloc(sizeof(struct screen_emu));
    if (screen == NULL) {
        fprintf(stderr, "allocation error\n");
        return NULL;
    }

    const char *fb_name = args_framebuffer();
    fprintf(stderr, "opening screen at %s\n", fb_name);

    screen->fd = open(fb_name, O_RDWR);
    if (screen->fd == -1) {
        fprintf(stderr, "ERROR (screen) cannot open framebuffer device\n");
        goto handle_file_error;
    }

    screen->xres = 128;
    screen->yres = 64;
    screen->bits_per_pixel = 32;
    screen->fb_size = screen->xres * screen->yres * screen->bits_per_pixel / 8;

    screen->fb_mem =
        (char *)mmap(0, screen->fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, screen->fd, 0);

    if (screen->fb_mem == (char *)-1) {
        fprintf(stderr, "ERROR (screen) failed to map framebuffer device to memory\n");
        goto handle_mmap_error;
    }

    screen->row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * screen->yres);
    if (!screen->row_pointers) {
        fprintf(stderr, "could not allocate row pointers\n");
    }
    for (int y = 0; y < screen->yres; y++) {
        screen->row_pointers[y] = (png_byte*)malloc(screen->xres * 3);
        if (!screen->row_pointers[y]) {
            fprintf(stderr, "could not allocate row pointer %d\n", y);
            abort();
        }
    }

    return screen;

/* handle_png_error: */
/*     png_destroy_write_struct(&screen->png_write, &screen->png_info); */
handle_mmap_error:
    close(screen->fd);
handle_file_error:
    free(screen);
    return NULL;
}

static int luma_8bit(uint16_t pixel_565) {
    float r, g, b, y;

    if (pixel_565 == 0) return 0;

    /* return (pixel_565 & 0x1F) >> 1; */
    /* return (pixel_565 & (0x3F << 5)) >> 3; */

    b = (float)(pixel_565 & 0x1F) / 31.0;
    pixel_565 >>= 5;
    g = (float)(pixel_565 & 0x3F) / 63.0;
    pixel_565 >>= 6;
    r = (float)(pixel_565 & 0x1F) / 31.0;

    y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    /* printf("red %f, green %f, blue %f -> luma %f (%d)\n", r, g, b, y, (int)(y * 15)); */
    return (int)(y * 255);
}

/* static void rgb565_to_rgb888(uint16_t rgb565, uint8_t *rgb888) { */
/*     rgb888[0] = (rgb565 >> 8) & 0xF8; */
/*     rgb888[1] = (rgb565 >> 3) & 0xFC; */
/*     rgb888[2] = (rgb565 << 3) & 0xF8; */
/*     /\* rgb888[2] = (rgb565 & 0x1F) << 3; *\/ */
/*     /\* rgb565 >>= 5; *\/ */
/*     /\* rgb888[1] = (rgb565 & 0x3F) << 2; *\/ */
/*     /\* rgb565 >>= 6; *\/ */
/*     /\* rgb888[0] = (rgb565 & 0x1F) << 3; *\/ */
/* } */

static void write_png_to_buf(struct screen_emu* screen) {
    screen->png_write = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!screen->png_write) {
        fprintf(stderr, "ERROR (screen) failed to create png write struct\n");
        return;
    }

    screen->png_info = png_create_info_struct(screen->png_write);
    if (!screen->png_info) {
        fprintf(stderr, "ERROR (screen) failed to create png info struct\n");
        png_destroy_write_struct(&screen->png_write, NULL);
        return;
    }

    if (setjmp(png_jmpbuf(screen->png_write))) {
        fprintf(stderr, "png: could not set write func\n");
        goto png_error;
    }

    png_set_write_fn(screen->png_write, &screen->outbuf, png_write_to_buf, NULL);

    if (setjmp(png_jmpbuf(screen->png_write))) {
        fprintf(stderr, "png: could not write IHDR\n");
        goto png_error;
    }

    png_set_IHDR(screen->png_write, screen->png_info,
                 screen->xres, screen->yres, 8,
                 PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    if (setjmp(png_jmpbuf(screen->png_write))) {
        fprintf(stderr, "png: could not write info\n");
        goto png_error;
    }

    png_write_info(screen->png_write, screen->png_info);
    
    if (setjmp(png_jmpbuf(screen->png_write))) {
        fprintf(stderr, "png: could not write content\n");
        goto png_error;
    }

    png_write_image(screen->png_write, screen->row_pointers);

    if (setjmp(png_jmpbuf(screen->png_write))) {
        fprintf(stderr, "png: could not write footer\n");
        goto png_error;
    }

    png_write_end(screen->png_write, NULL);

    png_destroy_write_struct(&screen->png_write, &screen->png_info);

png_error:
    png_destroy_write_struct(&screen->png_write, &screen->png_info);
    return;
}

static void* screen_poll_loop(void* data) {
    struct screen_emu *screen = data;
    uint16_t *p;
    uint16_t luma;

    screen->outbuf.buf = malloc(sizeof(json_hdr) + 1024 + sizeof(json_ftr));
    while (1) {
        p = (uint16_t*)screen->fb_mem;
        for (int y = 0; y < screen->yres; y++) {
            for (int x = 0; x < 128; x++) {
                luma = luma_8bit(*p++);
                screen->row_pointers[y][x] = luma;
                /* printf("%c", luma > 9 ? (luma - 10) + 'a' : luma + '0'); */
            }
            /* printf("\n"); */
        }

        screen->outbuf.buf = NULL;
        screen->outbuf.len = 0;
        write_png_to_buf(screen);
        
        if (screen->outbuf.len == 0) {
            fprintf(stderr, "failed to write png\n");
        } else {
            unsigned int b64_size = b64e_size(screen->outbuf.len);
            unsigned char *output = calloc(1, sizeof(json_hdr) + b64_size + sizeof(json_ftr) - 1);
            char *p = (char*)output;
            strcpy(p, json_hdr);
            p += sizeof(json_hdr) - 1;
            p += b64_encode((unsigned char*)screen->outbuf.buf, screen->outbuf.len, (unsigned char*)p);
            strcpy(p, json_ftr);
            printf("%s\n", output);
        }
        usleep(1000 * 1000);
    }
    return NULL;
}

int main(int argc, char **argv) {
    args_parse(argc, argv);
    struct screen_emu *screen = screen_emu_init();
    screen_poll_loop(screen);
}
