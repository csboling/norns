#pragma once

#include <unistd.h>

#define IO_NAME_SIZE 64

struct gpio_file_emu {
  int fd;
  char fname[IO_NAME_SIZE];
};

enum gpio_cmd_type {
  GPIO_KEY_UP,
  GPIO_KEY_DOWN,
  GPIO_ENC,
};

struct gpio_cmd {
  enum gpio_cmd_type type;
  int index;
  int value;
};

struct gpio_emu {
  struct gpio_file_emu keys;
  struct gpio_file_emu encs[3];
};

struct gpio_emu* gpio_emu_init(void);
void* gpio_emu_poll(void* gpio);
