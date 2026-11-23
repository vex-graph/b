#ifndef VEX_B_TEST_H
#define VEX_B_TEST_H

#include "vex.h"

/**
 * Single suite entry in the consolidated vex/b automated test runner.
 * name: CLI filter token matching argv[1].
 * run: suite entrypoint executed when selected.
 */
typedef struct SuiteEntry {
    const utf8 *name;
    fn (*run)();
} SuiteEntry;

#endif
