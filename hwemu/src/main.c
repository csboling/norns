#include "args.h"
#include "screenemu.h"


int main(int argc, char **argv) {
    args_parse(argc, argv);
    struct screen_emu *screen = screen_emu_init();
    screen_emu_poll(screen);
}
