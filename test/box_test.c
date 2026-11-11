#include "box.h"
#include "b.h"

;;DEFINITION
/* BoxManifest is the untrusted-input seam: a manifest is user-authored JSON, so
 * every rejection path is exercised here with a real fixture, and every
 * diagnostic the class emits during the run is captured and checked. Export is
 * proved against a real payload tree: declared destinations, nested
 * directories, executable permission bits, published layout, and the refusal
 * cases that must leave no destination behind.
 */
;;OVERVIEW
/* MODULE: box manifest owner proof. PUBLIC ENTRY: main.
 * SUBJECTS: BoxManifest_writeTemplate; BoxManifest_load; BoxManifest_free;
 * BoxManifest_export; BoxManifest_toString; BoxManifest_toStringStruct.
 * PRIVATE STATIC: writeText — fixture writer; writeRepetition — oversized
 * fixture; readAll — owned log text; countOf — substring count;
 * captureDiagnostics — redirect one section's stderr into the log; checkText —
 * substring assertion helper.
 * The test captures its own stderr and asserts the exact diagnostic count, so a
 * silent rejection, a duplicate report or stray output fails the run.
 */

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const DIAGNOSTIC_LOG = "box-diagnostics.log";

// Exactly one [vex] line per rejected cold case, plus the staged-export summary.
static const bytes EXPECTED_DIAGNOSTICS = 33;

// Write one fixture file with exact content.
static void writeText(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    assert(file != nullptr);
    bytes length = strlen(text);
    assert(fwrite(text, 1, length, file) == length);
    assert(fclose(file) == 0);
}

// Write a fixture of repeated bytes (used for the size-limit case).
static void writeRepetition(const char *path, char character, bytes count) {
    FILE *file = fopen(path, "wb");
    assert(file != nullptr);
    for (bytes index = 0; index < count; ++index)
        assert(fputc(character, file) != EOF);
    assert(fclose(file) == 0);
}

// Read a whole file into owned text.
static char *readAll(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file != nullptr);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size >= 0);
    assert(fseek(file, 0, SEEK_SET) == 0);
    char *text = malloc((bytes) size + 1);
    assert(text != nullptr);
    assert(fread(text, 1, (bytes) size, file) == (bytes) size);
    text[size] = '\0';
    assert(fclose(file) == 0);
    return text;
}

// Count non-overlapping occurrences of a needle.
static bytes countOf(const char *text, const char *needle) {
    bytes found = 0;
    for (const char *position = text; (position = strstr(position, needle)) != nullptr; ++found)
        position += strlen(needle);
    return found;
}

// Assert one expected diagnostic fragment is present in the captured log.
static void checkText(const char *log, const char *needle, bytes expected) {
    assert(countOf(log, needle) == expected);
}

// Assert that no private staging directory survived next to the destinations.
static void checkNoStaging(const char *directory) {
    DIR *tree = opendir(directory);
    assert(tree != nullptr);
    struct dirent *entry;
    while ((entry = readdir(tree)) != nullptr)
        assert(strncmp((*entry).d_name, ".b.sh-box-staging-", 15) != 0);
    assert(closedir(tree) == 0);
}

int main() {
    // Capture this run's diagnostics; on success the process writes nothing.
    int saved = dup(STDERR_FILENO);
    assert(saved >= 0);
    int log = open(DIAGNOSTIC_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(log >= 0);
    assert(dup2(log, STDERR_FILENO) == STDERR_FILENO);
    assert(close(log) == 0);

    // Payload fixtures: an executable entry, a nested tree, and extra assets.
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

    // The template is exclusive: it never replaces an existing path.
    assert(BoxManifest_writeTemplate("created.json") == 0);
    char *created = readAll("created.json");
    assert(strstr(created, "\"schemaVersion\": 1") != nullptr);
    assert(strstr(created, "\"entry\": \"dist/myapp\"") != nullptr);
    BoxManifest *template = BoxManifest_load("created.json");
    assert(template != nullptr);
    assert((*template).formatCount == 1);
    assert(strcmp((*template).formats[0], "portable") == 0);
    assert(strcmp((*template).entry, "dist/myapp") == 0);
    assert(strcmp((*template).statePolicy, "clean") == 0);
    free(created);
    BoxManifest_free(template);
    assert(BoxManifest_writeTemplate("created.json") == 1);

    // A complete manifest parses, validates and projects.
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
    assert((*manifest).schemaVersion == 1);
    assert(strcmp((*manifest).baseDirectory, ".") == 0);
    assert(strcmp((*manifest).identifier, "com.example.portable") == 0);
    assert(strcmp((*manifest).name, "Portable App") == 0);
    assert(strcmp((*manifest).version, "2.3.4") == 0);
    assert(strcmp((*manifest).publisher, "Example Studio") == 0);
    assert(strcmp((*manifest).license, "MIT") == 0);
    assert(strcmp((*manifest).homepage, "https://example.com") == 0);
    assert(strcmp((*manifest).icon, "assets/icon.png") == 0);
    assert((*manifest).authorCount == 2);
    assert(strcmp((*manifest).authors[0], "Ada") == 0);
    assert(strcmp((*manifest).authors[1], "Grace") == 0);
    assert(strcmp((*manifest).entry, "dist/portable") == 0);
    assert((*manifest).fileCount == 2);
    assert(strcmp((*manifest).sources[0], "assets/logo.txt") == 0);
    assert(strcmp((*manifest).destinations[0], "resources/logo.txt") == 0);
    assert(strcmp((*manifest).sources[1], "assets/nested") == 0);
    assert((*manifest).destinations[1] == nullptr);

    char value[512];
    bool truncated = true;
    BoxManifest_toString(nullptr, value, sizeof value, &truncated);
    assert(strcmp(value, "nullptr") == 0);
    assert(!truncated);
    BoxManifest_toStringStruct(nullptr, value, sizeof value, &truncated);
    assert(strcmp(value, "nullptr") == 0);
    char small[16];
    BoxManifest_toString(manifest, small, sizeof small, &truncated);
    assert(truncated);
    assert(strlen(small) < sizeof small);
    char full[2048];
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

    // Portable export publishes the declared payload only.
    assert(BoxManifest_export(manifest, "release") == 0);
    struct stat info;
    assert(stat("release/portable/portable", &info) == 0);
    assert(info.st_mode & S_IXUSR);
    assert(stat("release/portable/data.txt", &info) == 0);
    assert(stat("release/resources/logo.txt", &info) == 0);
    assert(stat("release/nested/deep.txt", &info) == 0);
    assert(stat("release/assets", &info) != 0);
    char *published = readAll("release/resources/logo.txt");
    assert(strcmp(published, "logo bytes\n") == 0);
    free(published);
    published = readAll("release/nested/deep.txt");
    assert(strcmp(published, "deep bytes\n") == 0);
    free(published);

    // A second export to the same destination refuses and preserves the export.
    assert(BoxManifest_export(manifest, "release") == 1);
    published = readAll("release/resources/logo.txt");
    assert(strcmp(published, "logo bytes\n") == 0);
    free(published);
    BoxManifest_free(manifest);
    assert(BoxManifest_export(nullptr, "release") == 1);

    // Rejected cold inputs: each emits exactly one diagnostic.
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

    // Export refusals that must not create anything.
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

    // A payload tree containing a FIFO fails mid-copy and cleans its staging.
    writeText("piped.json",
        "{\"schemaVersion\":1,\"application\":{\"id\":\"com.x\",\"name\":\"X\",\"version\":\"1.0\"},"
        "\"payload\":{\"entry\":\"dist/piped\"},\"formats\":[\"portable\"]}");
    BoxManifest *piped = BoxManifest_load("piped.json");
    assert(piped != nullptr);
    assert(BoxManifest_export(piped, "unused-piped") == 1);
    assert(stat("unused-piped", &info) != 0);
    BoxManifest_free(piped);

    checkNoStaging(".");

    // Capture back and verify the diagnostics this run produced.
    assert(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
    assert(close(saved) == 0);
    char *logText = readAll(DIAGNOSTIC_LOG);
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
    return 0;
}
