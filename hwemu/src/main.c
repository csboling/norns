#include <pthread.h>
#include <string.h>
#include <stdio.h>

#include "args.h"
#include "screenemu.h"
#include "gpioemu.h"


int main(int argc, char **argv) {
    int err;
    pthread_t screen_thread;

    args_parse(argc, argv);

    struct screen_emu *screen = screen_emu_init();
    if (!screen) {
        fprintf(stderr, "could not initialize screen\n");
        return -1;
    }

    struct gpio_emu *gpio = gpio_emu_init();
    if (!gpio) {
        fprintf(stderr, "could not initialize gpio\n");
        return -1;
    }

    err = pthread_create(&screen_thread, NULL, screen_emu_poll, (void*)screen);
    if (err) {
        fprintf(stderr, "failed to create screen thread: %s\n", strerror(err));
    }
    gpio_emu_poll(gpio);

    pthread_exit(0);
    return 0;
}
