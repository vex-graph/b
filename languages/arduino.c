// MODULE: Arduino adapter; no owned class.
// DEFINITION: Builds a complete sketch using Arduino CLI and installed cores.
// Upload always recompiles successfully before flashing the explicitly supplied
// board from the mandatory first-line // b_build("vendor:arch:board") header
// and an explicit port. Never auto-selects hardware or downloads cores/libraries.
// CLI lookup: ARDUINO_CLI override, installed PATH tool, macOS IDE bundle fallback.
// Standalone .ino files are copied to a correctly named out-of-tree sketch;
// standard sketch directories retain all their tabs and libraries. No renaming
// or edits are made to the user's source.
// OVERVIEW: cli; stageFile; sketchDirectory; boardFromHeader; compileSketch; buildArduino; runArduino rejects
// host execution; Arduino_upload validates flags; ARDUINO_LANGUAGE.
#include "languages/arduino.h"
#include "b.h"

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
/* MODULE: Arduino adapter; public declarations: languages/arduino.h.
 * PUBLIC: Arduino_upload — validate flags, resolve/stage sketch, compile, flash.
 * EXPORTED RECORD: ARDUINO_LANGUAGE {name="arduino", extension=".ino",
 * build=buildArduino, run=runArduino}.
 * PRIVATE STATIC: cli — override/PATH/macOS-bundle tool resolution;
 * stageFile — copy one standalone source without changing the original;
 * sketchDirectory — validate file/folder and choose standard or staged sketch;
 * boardFromHeader — bounded first-line metadata validation and board allocation;
 * compileSketch — compile and optionally transfer the pinned board to uploader;
 * buildArduino — compile directory using primary header;
 * runArduino — reject host execution and direct caller to explicit upload.
 * OWNERSHIP: temporary paths/board strings are released after operation; staged
 * source and output files persist as external build state. No persistent class.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *cli(void) {
    const char *override = getenv("ARDUINO_CLI");
    if (override != nullptr && *override != '\0')
        return override;
#ifdef __APPLE__
    // Bundle location is an OS application convention, not a required install.
    const char *bundle = "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli";
    const char *path = getenv("PATH");
    if (path != nullptr) {
        char *copy = Util_combine(path, "");
        char *save = nullptr;
        for (char *part = strtok_r(copy, ":", &save); part != nullptr; part = strtok_r(nullptr, ":", &save)) {
            char *candidate = Util_combine(part, "/arduino-cli");
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

static char *stageFile(const char *file, const char *parent) {
    const char *name = strrchr(file, '/');
    name = name == nullptr ? file : name + 1;
    char *stem = Util_combine(name, "");
    stem[strlen(stem) - strlen(".ino")] = '\0';
    char *directory = Util_outputDirectory(parent);
    if (directory == nullptr) {
        free(stem);
        return nullptr;
    }
    char *base = Util_combine(directory, "/arduino-sketch/");
    char *sketch = Util_combine(base, stem);
    free(base);
    free(directory);
    free(stem);
    if (!Util_makeDirectory(sketch)) {
        free(sketch);
        return nullptr;
    }
    char *prefix = Util_combine(sketch, "/");
    char *dest = Util_combine(prefix, name);
    free(prefix);
    FILE *input = fopen(file, "rb");
    FILE *output = input == nullptr ? nullptr : fopen(dest, "wb");
    bool ok = input != nullptr && output != nullptr;
    // Fixed chunk size bounds temporary copy memory; it is not a file-size limit.
    char bytes[8192];
    while (ok) {
        size_t count = fread(bytes, 1, sizeof bytes, input);
        if (count == 0) {
            ok = !ferror(input);
            break;
        }
        ok = fwrite(bytes, 1, count, output) == count;
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

static char *sketchDirectory(const char *input) {
    char *path = realpath(input, nullptr);
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
    char *directory = Util_parentDirectory(path);
    size_t length = strlen(directory);
    if (length > 1 && directory[length - 1] == '/')
        directory[length - 1] = '\0';
    const char *folder = strrchr(directory, '/');
    folder = folder == nullptr ? directory : folder + 1;
    char *mainName = Util_combine(folder, ".ino");
    const char *name = strrchr(path, '/');
    if (strcmp(name == nullptr ? path : name + 1, mainName) != 0) {
        char *staged = stageFile(path, directory);
        free(directory);
        directory = staged;
    }
    free(mainName);
    free(path);
    return directory;
}

static char *boardFromHeader(const char *sketch) {
    const char *name = strrchr(sketch, '/');
    name = name == nullptr ? sketch : name + 1;
    char *prefix = Util_combine(sketch, "/");
    char *stem = Util_combine(prefix, name);
    char *file = Util_combine(stem, ".ino");
    free(prefix);
    free(stem);
    FILE *input = fopen(file, "rb");
    free(file);
    if (input == nullptr) {
        THROW("cannot read primary Arduino sketch header");
        return nullptr;
    }
    // External metadata safety bound; oversized headers reject, never truncate.
    enum { HEADER_CAP = 1024 };
    char header[HEADER_CAP];
    bool ok = fgets(header, sizeof header, input) != nullptr && !ferror(input);
    if (ok && strchr(header, '\n') == nullptr && !feof(input))
        ok = false;
    fclose(input);
    const char *marker = "// b_build(\"";
    if (!ok || strncmp(header, marker, strlen(marker)) != 0) {
        THROW("Arduino sketch must start with // b_build(\"vendor:arch:board\")");
        return nullptr;
    }
    char *board = header + strlen(marker);
    char *end = strchr(board, '"');
    if (end == nullptr || end[1] != ')' || strspn(end + 2, " \t\r\n") != strlen(end + 2)) {
        THROW("malformed b_build header; expected a quoted FQBN and closing parenthesis");
        return nullptr;
    }
    *end = '\0';
    size_t colons = 0;
    for (const char *p = board; *p != '\0'; ++p) {
        bool valid = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || strchr("_-:,.=", *p) != nullptr;
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

static int compileSketch(const char *sketch, const char *fqbn, char **output, char **outBoard) {
    char *board = boardFromHeader(sketch);
    if (board == nullptr)
        return EXIT_FAILURE;
    if (fqbn != nullptr && strcmp(fqbn, board) != 0) {
        THROW("--fqbn disagrees with the sketch's b_build header; refusing upload");
        free(board);
        return EXIT_FAILURE;
    }
    char *directory = Util_outputDirectory(sketch);
    if (directory == nullptr) {
        free(board);
        return EXIT_FAILURE;
    }
    *output = Util_combine(directory, "/arduino");
    char *build = Util_combine(directory, "/arduino-build");
    char *arguments[] = { (char*) cli(), "compile", "--fqbn", board,
        "--build-path", build, "--output-dir", *output, (char*) sketch, nullptr };
    int status = Util_executeBuild(arguments);
    free(build);
    free(directory);
    // Pin the admitted board through upload; do not re-read changing metadata.
    if (status == 0 && outBoard != nullptr)
        *outBoard = board;
    else
        free(board);
    return status;
}

static int buildArduino(const char *project, char **output) {
    return compileSketch(project, nullptr, output, nullptr);
}

static int runArduino(const char *file, int argc, char **argv, bool buildArtifact) {
    (void) file;
    (void) argc;
    (void) argv;
    (void) buildArtifact;
    THROW("Arduino sketches run on hardware; use b upload arduino <sketch> --port <port> with a b_build header");
    return EXIT_FAILURE;
}

int Arduino_upload(int argc, char **argv) {
    const char *fqbn = nullptr;
    const char *port = nullptr;
    if (argc < 1) {
        THROW("upload needs a sketch and --port");
        return EXIT_FAILURE;
    }
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc || *argv[i + 1] == '\0') {
            THROW("upload flag requires a nonempty value");
            return EXIT_FAILURE;
        }
        if (strcmp(argv[i], "--fqbn") == 0 && fqbn == nullptr)
            fqbn = argv[i + 1];
        else if (strcmp(argv[i], "--port") == 0 && port == nullptr)
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
    char *sketch = sketchDirectory(argv[0]);
    if (sketch == nullptr)
        return EXIT_FAILURE;
    char *output = nullptr;
    char *board = nullptr;
    int status = compileSketch(sketch, fqbn, &output, &board);
    if (status == 0) {
        char *arguments[] = { (char*) cli(), "upload", "--fqbn", board,
            "--port", (char*) port, "--input-dir", output, sketch, nullptr };
        status = Util_execute(arguments);
    }
    free(board);
    free(output);
    free(sketch);
    return status;
}

const Language ARDUINO_LANGUAGE = { "arduino", ".ino", buildArduino, runArduino };
