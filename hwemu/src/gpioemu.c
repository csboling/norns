#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <json.h>
#include <linux/input.h>
#include <sys/stat.h>

#include "args.h"
#include "gpioemu.h"

static int create_gpio(struct gpio_file_emu* info) {
    if (mkfifo(info->fname, 0666)) {
        if (errno != EEXIST) {
          fprintf(stderr, "ERROR (gpio) could not create fifo: %s (%s)\n", info->fname, strerror(errno));
          return errno;
        }
    }
    fprintf(stderr, "created fifo %s\n", info->fname);
    return 0;
}

static int open_gpio(struct gpio_file_emu* info) {
    info->fd = open(info->fname, O_WRONLY);
    if (info->fd <= 0) {
        fprintf(stderr, "ERROR (gpio) could not open: %s (%s)\n", info->fname, strerror(errno));
        return errno;
    }
    fprintf(stderr, "opened fifo at %s\n", info->fname);
    return 0;
}

struct gpio_emu* gpio_emu_init(void) {
    const char* gpio_dir = args_gpio();
    int err;

    struct gpio_emu* gpio = malloc(sizeof(struct gpio_emu));
    if (!gpio) {
        fprintf(stderr, "could not allocate gpio_emu\n");
        return NULL;
    }

    err = snprintf(gpio->keys.fname, IO_NAME_SIZE - 1, "%s/platform-keys-event", gpio_dir);
    if (err >= IO_NAME_SIZE) {
        fprintf(stderr, "ERROR (keys) filename too long: %s\n", gpio->keys.fname);
        goto file_error;
    }
    if (create_gpio(&gpio->keys)) {
        goto file_error;
    }

    for (int i = 0; i < 3; i++) {
        err = snprintf(gpio->encs[i].fname, IO_NAME_SIZE - 1, "%s/platform-soc:knob%d-event", gpio_dir, i + 1);
        if (err >= IO_NAME_SIZE) {
            fprintf(stderr, "ERROR (enc%d) filename too long: %s\n", i, gpio->encs[i].fname);
            goto file_error;
        }
        if (create_gpio(&gpio->encs[i])) {
            goto file_error;
        }
    }

    open_gpio(&gpio->keys);
    for (int i = 0; i < 3; i++) {
        open_gpio(&gpio->encs[i]);
    }

    return gpio;

file_error:
    free(gpio);
    return NULL;
}

static void gpio_write_event(struct gpio_emu* gpio, struct gpio_cmd* cmd) {
    struct gpio_file_emu* gpio_file;
    struct input_event event;
    int i;

    if (cmd->index < 1 || cmd->index > 3) {
        return;
    }
    i = cmd->index - 1;

    switch (cmd->type) {
    case GPIO_KEY_UP:
        gpio_file = &gpio->keys;
        event.type = EV_KEY;
        event.value = 0;
        break;
    case GPIO_KEY_DOWN:
        gpio_file = &gpio->keys;
        event.type = EV_KEY;
        event.value = 1;
        break;
    case GPIO_ENC:
        gpio_file = &gpio->encs[i];
        event.type = EV_KEY;
        event.value = cmd->value;
        break;
    default:
        fprintf(stderr, "ERROR (gpio) unknown cmd type: %d\n", cmd->type);
        return;
    }

    event.code = i;
    write(gpio_file->fd, &event, sizeof(struct input_event));
}

void* gpio_emu_poll(void* data) {
    struct gpio_emu* gpio = data;
    char *line = NULL;
    char *str;
    size_t size = 0;
    struct json_object *json;
    struct gpio_cmd cmd = { 0 };
    int val_type;

    while (1) {
        if (getline(&line, &size, stdin) == -1) {
            fprintf(stderr, "failed to read stdin: %s\n", strerror(errno));
        }

        json = json_tokener_parse(line);
        json_object_object_foreach(json, key, val) {
            val_type = json_object_get_type(val);

            if (strcmp("type", key) == 0) {
                if (val_type != json_type_string) {
                    fprintf(stderr, "ERROR (gpio) expected string for 'type', got %d\n", val_type);
                    goto input_err;
                }

                str = (char*)json_object_get_string(val);
                if (strcmp("keyUp", str) == 0) {
                    cmd.type = GPIO_KEY_UP;
                } else if (strcmp("keyDown", str) == 0) {
                    cmd.type = GPIO_KEY_DOWN;
                } else if (strcmp("enc", str) == 0) {
                    cmd.type = GPIO_ENC;
                } else {
                    fprintf(stderr, "ERROR (gpio) unknown input type '%s'\n", str);
                    goto input_err;
                }
            }

            if (strcmp("index", key) == 0) {
                if (val_type != json_type_int) {
                    fprintf(stderr, "ERROR (gpio) expected int for 'index', got %d\n", val_type);
                    goto input_err;
                }
                cmd.index = json_object_get_int(val);
            }

            if (strcmp("value", key) == 0) {
                if (val_type != json_type_int) {
                    fprintf(stderr, "ERROR (gpio) expected int for 'value', got %d\n", val_type);
                    goto input_err;
                }
                cmd.value = json_object_get_int(val);
            }
        }

        gpio_write_event(gpio, &cmd);
input_err:
        continue;
    }
}

