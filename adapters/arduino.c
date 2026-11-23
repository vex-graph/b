/**
 * MODULE: Arduino adapter; no owned class.
 * DEFINITION: Builds a complete sketch using Arduino CLI and installed cores.
 * Upload always recompiles successfully before flashing the explicitly supplied
 * board from the mandatory first-line // b_build("vendor:arch:board") header
 * and an explicit port. Never auto-selects hardware or downloads cores/libraries.
 * CLI lookup: ARDUINO_CLI override, installed PATH tool, macOS IDE bundle fallback.
 * Standalone .ino files are copied to a correctly named out-of-tree sketch;
 * standard sketch directories retain all their tabs and libraries. No renaming
 * or edits are made to the user's source.
 * OVERVIEW: cli; stageFile; sketchDirectory; boardFromHeader; compileSketch; buildArduino; runArduino rejects
 * host execution; Arduino_upload validates flags; ARDUINO_ADAPTER.
 */

#include "vex.h"
#include "b/b_str.h"

;;DEFINITION
/* Arduino firmware runs on a board, not the host. This adapter reads the
 * mandatory first-line b_build FQBN from the primary sketch and keeps that
 * admitted board value alive through compilation and upload. The caller must
 * supply a port; an optional CLI board must agree with the header. Upload never
 * launches after a failed compile. Standalone files are copied into a correctly
 * named out-of-tree sketch; standard sketch folders retain their normal tabs.
 * Installed CLI/IDE tooling owns cores and libraries; no downloads are inferred.
 */
;;OVERVIEW
/* MODULE: Arduino adapter; public declarations: adapters/arduino.h.
 * PUBLIC: Arduino_upload, validate flags, resolve/stage sketch, compile, flash.
 * EXPORTED RECORD: ARDUINO_ADAPTER {name="arduino", extension=".ino",
 * build=buildArduino, run=runArduino}.
 * METADATA: tools="ARDUINO_CLI=arduino-cli";
 * capabilities="firmware build/upload; no host run".
 * PRIVATE STATIC: cli, override/PATH/macOS-bundle tool resolution;
 * stageFile, copy one standalone source without changing the original;
 * sketchDirectory, validate file/folder and choose standard or staged sketch;
 * boardFromHeader, bounded first-line metadata validation and board allocation;
 * compileSketch, compile and optionally transfer the pinned board to uploader;
 * buildArduino, compile directory using primary header;
 * runArduino, reject host execution and direct caller to explicit upload.
 * OWNERSHIP: temporary paths/board strings are released after operation; staged
 * source and output files persist as external build state. No persistent class.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "b.h"
#include "adapters/lang/adapter.h"

/** Resolve the Arduino CLI override, PATH executable, or macOS IDE-bundled tool. */
static const utf8 *cli() {
    const utf8 *override = getenv("ARDUINO_CLI");
    if (override != nullptr && *override != '\0')
        return override;
#ifdef __APPLE__
    const utf8 *bundle = "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli";
    const utf8 *path = getenv("PATH");
    if (path != nullptr) {
        utf8 *copy = Util_combine(path, "");
        utf8 *save = nullptr;
        for (utf8 *part = strtok_r(copy, ":", &save); part != nullptr; part = strtok_r(nullptr, ":", &save)) {
            utf8 *candidate = Util_combine(part, "/arduino-cli");
            bool exists = access(candidate, X_OK) == 0;
            free(candidate);
            if (exists) {
                free(copy);
                return "arduino-cli";
            }
        }
        free(copy);
    }
    if (access(bundle, X_OK) == 0)
        return bundle;
#endif
    return "arduino-cli";
}

/** Copy a standalone .ino into an out-of-tree, correctly named sketch directory. */
static utf8 *stageFile(const utf8 *file, const utf8 *parent) {
    const utf8 *name = strrchr(file, '/');
    name = name == nullptr ? file : name + 1;
    utf8 *stem = Util_combine(name, "");
    stem[strlen(stem) - strlen(".ino")] = '\0';
    utf8 *directory = Util_outputDirectory(parent);
    if (directory == nullptr) {
        free(stem);
        return nullptr;
    }
    utf8 *base = Util_combine(directory, "/arduino-sketch/");
    utf8 *sketch = Util_combine(base, stem);
    free(base);
    free(directory);
    free(stem);
    if (!Util_makeDirectory(sketch)) {
        free(sketch);
        return nullptr;
    }
    utf8 *prefix = Util_combine(sketch, "/");
    utf8 *dest = Util_combine(prefix, name);
    free(prefix);
    FILE *input = fopen(file, "rb");
    FILE *output = input == nullptr ? nullptr : fopen(dest, "wb");
    bool ok = input != nullptr && output != nullptr;
    utf8 Bytes[8192];
    while (ok) {
        bytes count = fread(Bytes, 1, sizeof Bytes, input);
        if (count == 0) {
            ok = !ferror(input);
            break;
        }
        ok = fwrite(Bytes, 1, count, output) == count;
    }
    if (input != nullptr && fclose(input) != 0)
        ok = false;
    if (output != nullptr && fclose(output) != 0)
        ok = false;
    free(dest);
    if (!ok) {
        THROW("cannot stage Arduino source");
        free(sketch);
        return nullptr;
    }
    return sketch;
}

/** Resolve a sketch directory, staging standalone or non-primary .ino inputs. */
static utf8 *sketchDirectory(const utf8 *input) {
    utf8 *path = realpath(input, nullptr);
    struct stat info;
    if (path == nullptr || stat(path, &info) != 0) {
        THROW("Arduino sketch does not exist: %s", input);
        free(path);
        return nullptr;
    }
    if (S_ISDIR(info.st_mode))
        return path;
    if (!S_ISREG(info.st_mode) || !Util_endsWith(path, ".ino")) {
        THROW("Arduino input must be a sketch directory or .ino file");
        free(path);
        return nullptr;
    }
    utf8 *directory = Util_parentDirectory(path);
    bytes length = strlen(directory);
    if (length > 1 && directory[length - 1] == '/')
        directory[length - 1] = '\0';
    const utf8 *folder = strrchr(directory, '/');
    folder = folder == nullptr ? directory : folder + 1;
    utf8 *mainName = Util_combine(folder, ".ino");
    const utf8 *name = strrchr(path, '/');
    if (!bStrEq(name == nullptr ? path : name + 1, mainName)) {
        utf8 *staged = stageFile(path, directory);
        free(directory);
        directory = staged;
    }
    free(mainName);
    free(path);
    return directory;
}

/** Read and validate the primary sketch's bounded first-line FQBN declaration. */
static utf8 *boardFromHeader(const utf8 *sketch) {
    const utf8 *name = strrchr(sketch, '/');
    name = name == nullptr ? sketch : name + 1;
    utf8 *prefix = Util_combine(sketch, "/");
    utf8 *stem = Util_combine(prefix, name);
    utf8 *file = Util_combine(stem, ".ino");
    free(prefix);
    free(stem);
    FILE *input = fopen(file, "rb");
    free(file);
    if (input == nullptr) {
        THROW("cannot read primary Arduino sketch header");
        return nullptr;
    }
    enum { HEADER_CAP = 1024 };
    utf8 header[HEADER_CAP];
    bool ok = fgets(header, sizeof header, input) != nullptr && !ferror(input);
    if (ok && bStrFindChar(header, '\n') == nullptr && !feof(input))
        ok = false;
    fclose(input);
    const utf8 *marker = "// b_build(\"";
    if (!ok || !bStrPrefix(header, marker)) {
        THROW("Arduino sketch must start with // b_build(\"vendor:arch:board\")");
        return nullptr;
    }
    utf8 *board = header + strlen(marker);
    utf8 *end = bStrFindCharMut(board, '"');
    if (end == nullptr || end[1] != ')' || strspn(end + 2, " \t\r\n") != strlen(end + 2)) {
        THROW("malformed b_build header; expected a quoted FQBN and closing parenthesis");
        return nullptr;
    }
    *end = '\0';
    bytes colons = 0;
    for (const utf8 *p = board; *p != '\0'; ++p) {
        bool valid = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || bStrFindChar("_-:,.=", *p) != nullptr;
        if (!valid) {
            THROW("b_build requires an Arduino FQBN, not a shell command or display name");
            return nullptr;
        }
        if (*p == ':')
            ++colons;
    }
    if (*board == '\0' || colons < 2 || *board == ':' || end[-1] == ':') {
        THROW("b_build requires a nonempty vendor:arch:board FQBN");
        return nullptr;
    }
    return Util_combine(board, "");
}

/** Compile for the declared board and optionally return its pinned identifier. */
static i32 compileSketch(const utf8 *sketch, const utf8 *fqbn, utf8 **output, utf8 **outBoard) {
    utf8 *board = boardFromHeader(sketch);
    if (board == nullptr)
        return EXIT_FAILURE;
    if (fqbn != nullptr && !bStrEq(fqbn, board)) {
        THROW("--fqbn disagrees with the sketch's b_build header; refusing upload");
        free(board);
        return EXIT_FAILURE;
    }
    utf8 *directory = Util_outputDirectory(sketch);
    if (directory == nullptr) {
        free(board);
        return EXIT_FAILURE;
    }
    *output = Util_combine(directory, "/arduino");
    utf8 *build = Util_combine(directory, "/arduino-build");
    utf8 *arguments[] = { (utf8*) cli(), "compile", "--fqbn", board,
        "--build-path", build, "--output-dir", *output, (utf8*) sketch, nullptr, nullptr };
    i32 status = Util_executeBuild(arguments);
    free(build);
    free(directory);
    if (status == 0 && outBoard != nullptr)
        *outBoard = board;
    else
        free(board);
    return status;
}

/** Build the selected sketch using its required b_build board declaration. */
static i32 buildArduino(const utf8 *project, utf8 **output) {
    return compileSketch(project, nullptr, output, nullptr);
}

/** Reject host execution because Arduino sketches require an explicit hardware upload. */
static i32 runArduino(const utf8 *file, i32 argc, utf8 **argv, bool buildArtifact) {
    (discard) file;
    (discard) argc;
    (discard) argv;
    (discard) buildArtifact;
    THROW("Arduino sketches run on hardware; use b.sh upload arduino <sketch> --port <port> with a b_build header");
    return EXIT_FAILURE;
}

/** Validate upload options, compile the sketch, then flash its explicitly chosen port. */
i32 Arduino_upload(i32 argc, utf8 **argv) {
    const utf8 *fqbn = nullptr;
    const utf8 *port = nullptr;
    if (argc < 1) {
        THROW("upload needs a sketch and --port");
        return EXIT_FAILURE;
    }
    for (i32 i = 1; i < argc; i += 2) {
        if (i + 1 >= argc || *argv[i + 1] == '\0') {
            THROW("upload flag requires a nonempty value");
            return EXIT_FAILURE;
        }
        if (bStrEq(argv[i], "--fqbn") && fqbn == nullptr)
            fqbn = argv[i + 1];
        else if (bStrEq(argv[i], "--port") && port == nullptr)
            port = argv[i + 1];
        else {
            THROW("unknown or duplicate upload flag: %s", argv[i]);
            return EXIT_FAILURE;
        }
    }
    if (port == nullptr) {
        THROW("upload requires explicit --port; the board comes from the b_build header");
        return EXIT_FAILURE;
    }
    utf8 *sketch = sketchDirectory(argv[0]);
    if (sketch == nullptr)
        return EXIT_FAILURE;
    utf8 *output = nullptr;
    utf8 *board = nullptr;
    i32 status = compileSketch(sketch, fqbn, &output, &board);
    if (status == 0) {
        utf8 *arguments[] = { (utf8*) cli(), "upload", "--fqbn", board,
            "--port", (utf8*) port, "--input-dir", output, sketch, nullptr, nullptr };
        status = Util_execute(arguments);
    }
    free(board);
    free(output);
    free(sketch);
    return status;
}

const Adapter ARDUINO_ADAPTER = { "arduino", ".ino", buildArduino, runArduino,
    "ARDUINO_CLI=arduino-cli", "firmware build/upload; no host run", nullptr, nullptr };
