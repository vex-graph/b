// Arduino sketch builds and explicit compile-before-upload command.
#pragma once
#include "languages/language.h"
extern const Language ARDUINO_LANGUAGE;
int Arduino_upload(int argc, char **argv);
