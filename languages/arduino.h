// Arduino sketch builds and explicit compile-before-upload command.
#pragma once
#include "annotation.h"
;;DEFINITION
/* Arduino contract separates host build/check from explicit hardware upload.
 * The sketch's b_build header supplies the board; callers supply the port.
 */
;;OVERVIEW
/* PUBLIC RECORD: ARDUINO_LANGUAGE; fields name="arduino", extension=".ino",
 * build=buildArduino, run=runArduino (host execution rejects).
 * PUBLIC FUNCTION: Arduino_upload — validate argv and compile before flashing.
 * No owned state in this header; behavior lives in arduino.c.
 */
#include "languages/language.h"
extern const Language ARDUINO_LANGUAGE;
int Arduino_upload(int argc, char **argv);
