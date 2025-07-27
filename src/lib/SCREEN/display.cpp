#include "display.h"
#include "helpers.h"

const char *Display::message_string[] = {
    "ExpressLRS",
    "[  Connected  ]",
    "[  ! Armed !  ]",
    "[  Mismatch!  ]"
};

const char *Display::main_menu_strings[][2] = {
    {"BIND", "MODE"},
    {"WIFI", "ADMIN"},
    {"TX", "WIFI"},
};
