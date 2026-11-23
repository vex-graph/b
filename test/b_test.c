/**
 * Consolidated automated owner test suite for vex/b.
 * Usage: b_test [box|util]
 */

#include "vex.h"
#include "box.h"
#include "b.h"
#include "test/b_test.h"

;;DEFINITION
/* Consolidated owner proof for vex/b: BoxManifest untrusted-input validation
 * and portable export staging, plus Util_runCommand and Util_checkSources
 * boundary rejection and literal argument forwarding.
 */
;;OVERVIEW
/* MODULE: consolidated vex/b owner proof. PUBLIC ENTRY: main.
 * SUITES: box (BoxManifest_writeTemplate, BoxManifest_load, BoxManifest_free,
 * BoxManifest_export, BoxManifest_toString, BoxManifest_toStringStruct);
 * util (Util_runCommand, Util_checkSources).
 */

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const utf8 *const DIAGNOSTIC_LOG = "box-diagnostics.log";

/** Exactly one [vex] line per rejected cold case, plus the staged-export summary. */
static const bytes EXPECTED_DIAGNOSTICS = 33;

/** Write one fixture file with exact content. */
static fn writeText(const utf8 *path, const utf8 *text) {
    FILE *file = fopen(path, "wb");
    assert(file != nullptr);
    bytes length = strlen(text);
    assert(fwrite(text, 1, length, file) == length);
    assert(fclose(file) == 0);
}

/** Write a fixture of repeated bytes (used for the size-limit case). */
static fn writeRepetition(const utf8 *path, utf8 character, bytes count) {
    FILE *file = fopen(path, "wb");
    assert(file != nullptr);
    for (bytes index = 0; index < count; ++index)
        assert(fputc(character, file) != EOF);
    assert(fclose(file) == 0);
}

/** Read a whole file into owned text. */
static utf8 *readAll(const utf8 *path) {
    FILE *file = fopen(path, "rb");
    assert(file != nullptr);
    assert(fseek(file, 0, SEEK_END) == 0);
    i64 size = ftell(file);
    assert(size >= 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    utf8 *text = malloc((bytes) size + 1);
    assert(text != nullptr);
    assert(fread(text, 1, (bytes) size, file) == (bytes) size);
    text[size] = '\0';
    assert(fclose(file) == 0);
    return text;
}

/** Count non-overlapping occurrences of a needle. */
static bytes countOf(const utf8 *text, const utf8 *needle) {
    bytes found = 0;
    for (const utf8 *position = text; (position = strstr(position, needle)) != nullptr; ++found)
        position += strlen(needle);
    return found;
}

/** Assert one expected diagnostic fragment is present in the captured log. */
static fn checkText(const utf8 *log, const utf8 *needle, bytes expected) {
    assert(countOf(log, needle) == expected);
}

;;GETTER
static inline const utf8 *dirent_d_name(const struct dirent *self)
{
    return (*self).d_name;
}
;;GETTER
static inline bytes BoxManifest_formatCount(const BoxManifest *self)
{
    return (*self).formatCount;
}
;;GETTER
static inline utf8 *const *BoxManifest_formats(const BoxManifest *self)
{
    return (*self).formats;
}
;;GETTER
static inline const utf8 *BoxManifest_entry(const BoxManifest *self)
{
    return (*self).entry;
}
;;GETTER
static inline const utf8 *BoxManifest_statePolicy(const BoxManifest *self)
{
    return (*self).statePolicy;
}
;;GETTER
static inline u32 BoxManifest_schemaVersion(const BoxManifest *self)
{
    return (*self).schemaVersion;
}
;;GETTER
static inline const utf8 *BoxManifest_baseDirectory(const BoxManifest *self)
{
    return (*self).baseDirectory;
}
;;GETTER
static inline const utf8 *BoxManifest_identifier(const BoxManifest *self)
{
    return (*self).identifier;
}
;;GETTER
static inline const utf8 *BoxManifest_name(const BoxManifest *self)
{
    return (*self).name;
}
;;GETTER
static inline const utf8 *BoxManifest_version(const BoxManifest *self)
{
    return (*self).version;
}
;;GETTER
static inline const utf8 *BoxManifest_publisher(const BoxManifest *self)
{
    return (*self).publisher;
}
;;GETTER
static inline const utf8 *BoxManifest_license(const BoxManifest *self)
{
    return (*self).license;
}
;;GETTER
static inline const utf8 *BoxManifest_homepage(const BoxManifest *self)
{
    return (*self).homepage;
}
;;GETTER
static inline const utf8 *BoxManifest_icon(const BoxManifest *self)
{
    return (*self).icon;
}
;;GETTER
static inline bytes BoxManifest_authorCount(const BoxManifest *self)
{
    return (*self).authorCount;
}
;;GETTER
static inline utf8 *const *BoxManifest_authors(const BoxManifest *self)
{
    return (*self).authors;
}
;;GETTER
static inline bytes BoxManifest_fileCount(const BoxManifest *self)
{
    return (*self).fileCount;
}
;;GETTER
static inline utf8 *const *BoxManifest_sources(const BoxManifest *self)
{
    return (*self).sources;
}
;;GETTER
static inline utf8 *const *BoxManifest_destinations(const BoxManifest *self)
{
    return (*self).destinations;
}

/** Assert that no private staging directory survived next to the destinations. */
static fn checkNoStaging(const utf8 *directory) {
    DIR *tree = opendir(directory);
    assert(tree != nullptr);
    struct dirent *entry;
    while ((entry = readdir(tree)) != nullptr)
        assert(strncmp(dirent_d_name(entry), ".b.sh-box-staging-", 15) != 0);
    assert(closedir(tree) == 0);
}

/** Proves BoxManifest template generation, schema validation, export staging, and diagnostics. */
static fn suite_box() {
    i32 saved = dup(STDERR_FILENO);
    assert(saved >= 0);
    i32 log = open(DIAGNOSTIC_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(log >= 0);
    assert(dup2(log, STDERR_FILENO) == STDERR_FILENO);
    assert(close(log) == 0);

    assert(mkdir("dist", 0755) == 0);
    assert(mkdir("dist/portable", 0755) == 0);
    assert(mkdir("dist/piped", 0755) == 0);
    assert(mkdir("assets", 0755) == 0);
    assert(mkdir("assets/nested", 0755) == 0);
    writeText("dist/portable/portable", "#!/bin/sh\necho portable\n");
    assert(chmod("dist/portable/portable", 0755) == 0);
    writeText("dist/portable/data.txt", "payload data\n");
    writeText("assets/logo.txt", "logo bytes\n");
    writeText("assets/nested/deep.txt", "deep bytes\n");
    writeText("assets/icon.png", "icon bytes\n");
    assert(mkfifo("dist/piped/pipe.fifo", 0644) == 0);
    writeText("plain.txt", "not a payload\n");
    assert(symlink("plain.txt", "linked.txt") == 0);

    assert(BoxManifest_writeTemplate("created.json") == 0);
    utf8 *created = readAll("created.json");
    assert(strstr(created, "\"schemaVersion\": 1") != nullptr);
    assert(strstr(created, "\"entry\": \"dist/myapp\"") != nullptr);
    BoxManifest *template = BoxManifest_load("created.json");
    assert(template != nullptr);
    assert(BoxManifest_formatCount(template) == 1);
    assert(strcmp(BoxManifest_formats(template)[0], "portable") == 0);
    assert(strcmp(BoxManifest_entry(template), "dist/myapp") == 0);
    assert(strcmp(BoxManifest_statePolicy(template), "clean") == 0);
    free(created);
    BoxManifest_free(template);
    assert(BoxManifest_writeTemplate("created.json") == 1);

    writeText("valid.json",
        "{\"schemaVersion\":1,"
        "\"application\":{\"id\":\"com.example.portable\",\"name\":\"Portable App\",\"version\":\"2.3.4\","
        "\"authors\":[\"Ada\",\"Grace\"],\"publisher\":\"Example Studio\",\"description\":\"Portable payload\","
        "\"license\":\"MIT\",\"homepage\":\"https://example.com\",\"icon\":\"assets/icon.png\"},"
        "\"payload\":{\"entry\":\"dist/portable\",\"files\":[{\"source\":\"assets/logo.txt\","
        "\"destination\":\"resources/logo.txt\"},{\"source\":\"assets/nested\"}]},"
        "\"targets\":[],\"formats\":[\"portable\"],\"statePolicy\":\"clean\"}");
    BoxManifest *manifest = BoxManifest_load("valid.json");
    assert(manifest != nullptr);
    assert(BoxManifest_schemaVersion(manifest) == 1);
    assert(strcmp(BoxManifest_baseDirectory(manifest), ".") == 0);
    assert(strcmp(BoxManifest_identifier(manifest), "com.example.portable") == 0);
    assert(strcmp(BoxManifest_name(manifest), "Portable App") == 0);
    assert(strcmp(BoxManifest_version(manifest), "2.3.4") == 0);
    assert(strcmp(BoxManifest_publisher(manifest), "Example Studio") == 0);
    assert(strcmp(BoxManifest_license(manifest), "MIT") == 0);
    assert(strcmp(BoxManifest_homepage(manifest), "https://example.com") == 0);
    assert(strcmp(BoxManifest_icon(manifest), "assets/icon.png") == 0);
    assert(BoxManifest_authorCount(manifest) == 2);
    assert(strcmp(BoxManifest_authors(manifest)[0], "Ada") == 0);
    assert(strcmp(BoxManifest_authors(manifest)[1], "Grace") == 0);
    assert(strcmp(BoxManifest_entry(manifest), "dist/portable") == 0);
    assert(BoxManifest_fileCount(manifest) == 2);
    assert(strcmp(BoxManifest_sources(manifest)[0], "assets/logo.txt") == 0);
    assert(strcmp(BoxManifest_destinations(manifest)[0], "resources/logo.txt") == 0);
    assert(strcmp(BoxManifest_sources(manifest)[1], "assets/nested") == 0);
    assert(BoxManifest_destinations(manifest)[1] == nullptr);

    utf8 value[512];
    bool truncated = true;
    BoxManifest_toString(nullptr, value, sizeof value, &truncated);
    assert(strcmp(value, "nullptr") == 0);
    assert(!truncated);
    BoxManifest_toStringStruct(nullptr, value, sizeof value, &truncated);
    assert(strcmp(value, "nullptr") == 0);
    utf8 small[16];
    BoxManifest_toString(manifest, small, sizeof small, &truncated);
    assert(truncated);
    assert(strlen(small) < sizeof small);
    utf8 full[2048];
    BoxManifest_toString(manifest, full, sizeof full, &truncated);
    assert(!truncated);
    assert(strstr(full, "com.example.portable") != nullptr);
    assert(strstr(full, "Portable App 2.3.4") != nullptr);
    assert(strstr(full, "authors=2") != nullptr);
    assert(strstr(full, "files=2") != nullptr);
    BoxManifest_toStringStruct(manifest, full, sizeof full, &truncated);
    assert(!truncated);
    assert(strstr(full, "baseDirectory: \".\"") != nullptr);
    assert(strstr(full, "identifier: \"com.example.portable\"") != nullptr);
    assert(strstr(full, "authorCount: 2") != nullptr);
    assert(strstr(full, "formatCount: 1") != nullptr);
    BoxManifest_toString(manifest, full, 0, &truncated);
    assert(truncated);

    assert(BoxManifest_export(manifest, "release") == 0);
    struct stat info;
    assert(stat("release/portable/portable", &info) == 0);
    assert(info.st_mode & S_IXUSR);
    assert(stat("release/portable/data.txt", &info) == 0);
    assert(stat("release/resources/logo.txt", &info) == 0);
    assert(stat("release/nested/deep.txt", &info) == 0);
    assert(stat("release/assets", &info) != 0);
    utf8 *published = readAll("release/resources/logo.txt");
    assert(strcmp(published, "logo bytes\n") == 0);
    free(published);
    published = readAll("release/nested/deep.txt");
    assert(strcmp(published, "deep bytes\n") == 0);
    free(published);

    assert(BoxManifest_export(manifest, "release") == 1);
    published = readAll("release/resources/logo.txt");
    assert(strcmp(published, "logo bytes\n") == 0);
    free(published);
    BoxManifest_free(manifest);
    assert(BoxManifest_export(nullptr, "release") == 1);

    assert(BoxManifest_load("missing.json") == nullptr);
    assert(BoxManifest_load("") == nullptr);
    writeRepetition("huge.json", 'x', 300000);
    assert(BoxManifest_load("huge.json") == nullptr);
    writeText("broken.json", "{\"schemaVersion\":1,");
    assert(BoxManifest_load("broken.json") == nullptr);
    writeText("typo.json", "{\"schemaVersion\":1,\"typo\":true}");
    assert(BoxManifest_load("typo.json") == nullptr);
    writeText("duplicate.json", "{\"schemaVersion\":1,\"schemaVersion\":1}");
    assert(BoxManifest_load("duplicate.json") == nullptr);
    writeText("escape.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"\\u0041\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\"},\"formats\":[\"portable\"]}");
    assert(BoxManifest_load("escape.json") == nullptr);
    writeText("control.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"bad\nname\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\"},\"formats\":[\"portable\"]}");
    assert(BoxManifest_load("control.json") == nullptr);
    writeText("schema.json",
        "{\"schemaVersion\":2,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\"},\"formats\":[\"portable\"]}");
    assert(BoxManifest_load("schema.json") == nullptr);
    writeText("format.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\"},\"formats\":[\"flatpak\"]}");
    assert(BoxManifest_load("format.json") == nullptr);
    writeText("emptyformats.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\"},\"formats\":[]}");
    assert(BoxManifest_load("emptyformats.json") == nullptr);
    writeText("traversal.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"../escape\"},\"formats\":[\"portable\"]}");
    assert(BoxManifest_load("traversal.json") == nullptr);
    writeText("absolute.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"/etc/passwd\"},\"formats\":[\"portable\"]}");
    assert(BoxManifest_load("absolute.json") == nullptr);
    writeText("state.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\"},\"formats\":[\"portable\"],\"statePolicy\":\"machine\"}");
    assert(BoxManifest_load("state.json") == nullptr);
    writeText("targets.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\"},\"targets\":[{\"platform\":\"macos\"}],\"formats\":[\"portable\"]}");
    assert(BoxManifest_load("targets.json") == nullptr);
    writeText("samedest.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"dist/portable/portable\",\"files\":[{\"source\":\"assets/logo.txt\","
        "\"destination\":\"portable\"}]},\"formats\":[\"portable\"]}");
    assert(BoxManifest_load("samedest.json") == nullptr);
    writeText("dupdest.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\",\"files\":[{\"source\":\"assets/logo.txt\","
        "\"destination\":\"dup.txt\"},{\"source\":\"assets/nested/deep.txt\","
        "\"destination\":\"dup.txt\"}]},\"formats\":[\"portable\"]}");
    assert(BoxManifest_load("dupdest.json") == nullptr);
    writeText("noentry.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"files\":[]},\"formats\":[\"portable\"]}");
    assert(BoxManifest_load("noentry.json") == nullptr);
    writeText("noid.json",
        "{\"schemaVersion\":1,\"application\":{\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\"},\"formats\":[\"portable\"]}");
    assert(BoxManifest_load("noid.json") == nullptr);
    writeText("badversion.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"abc\"},"
        "\"payload\":{\"entry\":\"plain.txt\"},\"formats\":[\"portable\"]}");
    assert(BoxManifest_load("badversion.json") == nullptr);
    writeText("trailing.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\"},\"formats\":[\"portable\"]} extra");
    assert(BoxManifest_load("trailing.json") == nullptr);
    writeText("filetype.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\",\"files\":[42]},\"formats\":[\"portable\"]}");
    assert(BoxManifest_load("filetype.json") == nullptr);

    writeText("missingentry.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"dist/absent\"},\"formats\":[\"portable\"]}");
    BoxManifest *missingEntry = BoxManifest_load("missingentry.json");
    assert(missingEntry != nullptr);
    assert(BoxManifest_export(missingEntry, "unused-missing") == 1);
    assert(stat("unused-missing", &info) != 0);
    BoxManifest_free(missingEntry);
    writeText("zipformat.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\"},\"formats\":[\"zip\"]}");
    BoxManifest *zipFormat = BoxManifest_load("zipformat.json");
    assert(zipFormat != nullptr);
    assert(BoxManifest_export(zipFormat, "unused-zip") == 1);
    assert(stat("unused-zip", &info) != 0);
    BoxManifest_free(zipFormat);
    writeText("missingsource.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\",\"files\":[{\"source\":\"assets/absent.txt\"}]},"
        "\"formats\":[\"portable\"]}");
    BoxManifest *missingSource = BoxManifest_load("missingsource.json");
    assert(missingSource != nullptr);
    assert(BoxManifest_export(missingSource, "unused-source") == 1);
    assert(stat("unused-source", &info) != 0);
    BoxManifest_free(missingSource);
    writeText("linksource.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\",\"files\":[{\"source\":\"linked.txt\"}]},"
        "\"formats\":[\"portable\"]}");
    BoxManifest *linkSource = BoxManifest_load("linksource.json");
    assert(linkSource != nullptr);
    assert(BoxManifest_export(linkSource, "unused-link") == 1);
    assert(stat("unused-link", &info) != 0);
    BoxManifest_free(linkSource);
    writeText("linkentry.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"linked.txt\"},\"formats\":[\"portable\"]}");
    BoxManifest *linkEntry = BoxManifest_load("linkentry.json");
    assert(linkEntry != nullptr);
    assert(BoxManifest_export(linkEntry, "unused-entry-link") == 1);
    assert(stat("unused-entry-link", &info) != 0);
    BoxManifest_free(linkEntry);
    writeText("parentfile.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"plain.txt\"},\"formats\":[\"portable\"]}");
    BoxManifest *parentFile = BoxManifest_load("parentfile.json");
    assert(parentFile != nullptr);
    assert(BoxManifest_export(parentFile, "plain.txt/inside") == 1);
    BoxManifest_free(parentFile);

    writeText("piped.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"dist/piped\"},\"formats\":[\"portable\"]}");
    BoxManifest *piped = BoxManifest_load("piped.json");
    assert(piped != nullptr);
    assert(BoxManifest_export(piped, "unused-piped") == 1);
    assert(stat("unused-piped", &info) != 0);
    BoxManifest_free(piped);

    checkNoStaging(".");

    assert(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
    assert(close(saved) == 0);
    utf8 *logText = readAll(DIAGNOSTIC_LOG);
    assert(countOf(logText, "[vex] ") == EXPECTED_DIAGNOSTICS);
    assert(countOf(logText, "\n") == EXPECTED_DIAGNOSTICS);
    checkText(logText, "manifest does not exist or is not a regular file", 1);
    checkText(logText, "manifest path must not be empty", 1);
    checkText(logText, "must be 1..262144 bytes", 1);
    checkText(logText, "manifest: manifest key must be a string", 1);
    checkText(logText, "unknown root key: typo", 1);
    checkText(logText, "duplicate schemaVersion", 1);
    checkText(logText, "unsupported escape sequence", 1);
    checkText(logText, "contains a control character", 1);
    checkText(logText, "schemaVersion must be 1, not 2", 1);
    checkText(logText, "unknown export format: flatpak", 1);
    checkText(logText, "formats must declare at least one export format", 1);
    checkText(logText, "must not contain '.' or '..' components", 1);
    checkText(logText, "must be relative", 1);
    checkText(logText, "statePolicy must be", 1);
    checkText(logText, "target-specific exports are not implemented", 1);
    checkText(logText, "share one export destination", 1);
    checkText(logText, "declares the same destination twice", 1);
    checkText(logText, "payload.entry is required", 1);
    checkText(logText, "application.id is required", 1);
    checkText(logText, "application.version must start with a digit", 1);
    checkText(logText, "unexpected trailing content", 1);
    checkText(logText, "payload.files entries must be a string or an object", 1);
    checkText(logText, "payload entry is unavailable or a symbolic link", 2);
    checkText(logText, "export format is not implemented yet: zip", 1);
    checkText(logText, "payload file is unavailable or a symbolic link", 2);
    checkText(logText, "export destination already exists", 1);
    checkText(logText, "export destination parent is not a directory", 1);
    checkText(logText, "not a regular file or directory", 1);
    checkText(logText, "export failed; no destination was created or replaced", 1);
    free(logText);
}

/** Checks command/source helper rejection, exit-status propagation, and literal ten-argument forwarding. */
static fn suite_util() {
#if defined(VEX_PLATFORM_WINDOWS)
    utf8 *prefix[] = { "sh", "-c", "exit 7" };
#else
    utf8 *prefix[] = { "/bin/sh", "-c", "exit 7" };
#endif
    utf8 *empty[] = { nullptr };
    assert(Util_runCommand(nullptr, 0, 0, nullptr) == 1);
    assert(Util_runCommand(prefix, 0, 0, nullptr) == 1);
    assert(Util_runCommand(prefix, 3, -1, nullptr) == 1);
    assert(Util_runCommand(prefix, SIZE_MAX, 0, nullptr) == 1);
    assert(Util_runCommand(empty, 1, 0, nullptr) == 1);
    assert(Util_runCommand(prefix, 3, 1, nullptr) == 1);
    assert(Util_runCommand(prefix, 3, 0, nullptr) == 7);
    utf8 *arguments[] = { "a space", "$(touch NEVER)", ";", "3", "4", "5", "6", "7", "8", "9" };
#if defined(VEX_PLATFORM_WINDOWS)
    utf8 *ten[] = { "sh", "-c", "test \"$#\" -eq 10 && test \"$1\" = 'a space'", "fixture" };
#else
    utf8 *ten[] = { "/bin/sh", "-c", "test \"$#\" -eq 10 && test \"$1\" = 'a space'", "fixture" };
#endif
    assert(Util_runCommand(ten, 4, 10, arguments) == 0);
    utf8 *output = nullptr;
    assert(Util_checkSources(nullptr, "/*.sh", prefix, 1, &output) == 1);
    assert(Util_checkSources(".", nullptr, prefix, 1, &output) == 1);
    assert(Util_checkSources(".", "/*.sh", prefix, 0, &output) == 1);
    assert(Util_checkSources(".", "/*.sh", prefix, 1, nullptr) == 1);
    assert(Util_checkSources(".", "/*.sh", prefix, SIZE_MAX, &output) == 1);
    assert(output == nullptr);
}

/** Runs all automated vex/b suites or a single named suite passed as argv[1]. */
i32 main(i32 argc, utf8 **argv) {
    const SuiteEntry suites[] = {
        { "box", suite_box },
        { "util", suite_util },
    };
    const utf8 *filter = argc > 1 ? argv[1] : nullptr;
    bytes ran = 0;

    for (bytes i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        if (filter && strcmp(filter, suites[i].name) != 0)
            continue;
        suites[i].run();
        ran++;
    }

    if (filter && ran == 0) {
        fprintf(stderr, "b_test: unknown suite '%s'\n", filter);
        return 2;
    }
    return 0;
}
