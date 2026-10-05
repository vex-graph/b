// workspace.c — the existing vexgraph build graph, now owned by vex-graph/b.
//
// One C23 program that:
//   1. holds the target graph in C,
//   2. asks the compiler what is stale (-MMD depfiles),
//   3. reuses a content-addressed object cache,
//   4. drives clang / ar / ld directly, and
//   5. launches the result as a real, release-quality binary — directly.
//
// North star: `b run <app>` should feel instant and ship-grade on any machine
// — never a CLion/IDE runtime process.
//
// v0.1 proof: the vexspoke repo (static lib + owner tests).
//
// Laws honoured: Platform Support Floor Law (arm64 / macOS 14.0 / apple-m1),
// Build & Naming Conventions Law (-Wall -Wextra -Werror, C23).
// Constitution: ecosystem/vexspoke/preferences.md in the owning workspace.
// Pointer members follow the Semantic Consistency Law (Reference form).
//
// Subcommands:
//   b build [target...]     build targets (default: all)
//   b run   <target> [args] build then exec the binary directly
//   b test  [substr]        build + run every test
//   b cc                    (re)write b.json (clangd database)
//   b clean                 remove build state
//   b doctor                print the resolved toolchain + flags
//   b targets               list declared targets
//
// Flags: -j N  --release  -v/--verbose

#define _DARWIN_C_SOURCE

#include "annotation.h"

;;DEFINITION
/* The preserved workspace coordinator owns a concrete target graph and the
 * build-state lifecycle, not a replacement compiler. Targets become compile
 * units; compiler depfiles and content hashes decide which objects can be
 * reused. Child jobs produce logs, then libraries/programs are linked after
 * dependencies. Generators stage shaders, runnable targets can be packaged,
 * and explicit test/coverage commands collect scoped execution evidence.
 * Its process-global records are private to this one-shot CLI. This overview
 * documents the legacy engine; explicit pointer dereferences preserve its
 * behavior while following the Semantic Consistency Law (Reference form).
 */
;;OVERVIEW
/* MODULE: workspace compatibility graph. PUBLIC ENTRY: main.
 * PRIVATE STATIC FUNCTION REGISTRY:
 * Allocation/diagnostics: die, xmalloc, xrealloc, xstrdup, strf.
 * Files/paths: path_exists, is_dir, mkdir_p, mkdir_parent, read_file, write_file,
 * relativize, json_escape, abspath, glob_rec.
 * Lists/argv: strl_push, strl_pushf, strl_extend, cmp_str, strl_sort, cmd_argv,
 * cmd_join, strl_push_unique.
 * Hash/time: fnv1a, hash_str, hash_file, stamp_of.
 * Processes: run_sync, run_capture, job_start, run_jobs, run_test.
 * Target graph: tl_push, find_target, target_new, add_exe_libs, setup_targets.
 * Toolchain: apple_frameworks, setup_paths, cc_version, base_cflags, base_lflags.
 * Target declarations: setup_vexspoke, setup_darling, setup_graphvex_tests,
 * setup_vexspoke_tests, setup_sesh, setup_impedance, setup_apihaven, setup_hotcwap,
 * has_main, setup_apps, setup_graphvex, setup_darling_tests.
 * Compilation/cache: mangle, unit_paths, collect_pub, unit_build_cmd,
 * parse_depfile, unit_up_to_date, write_meta, unit_content_hash, cache_path,
 * compile_all, target_objs_changed, target_obj_hash.
 * Link/package/generation: make_app_bundle, collect_link_libs, target_dep_sig,
 * link_target, add_gen, run_gens, build_all.
 * Coverage parsing: ident_char, sl_has, load_lines, load_pairs, is_type_word,
 * scrub_header, header_functions, word_in, unit_rel, own_header, owner_test_for,
 * src_matches, cov_add, cov_has, load_lcov, baseline_path, run_coverage.
 * Watch/IDE export: watch_snapshot, gen_compile_commands, ide_strings,
 * export_ide_graph, rebuild_self.
 * Runnable selection: target_runnable, target_label, target_group, list_runnables,
 * match_targets, resolve_runnable, match_sources, find_source_for, synth_target,
 * usage.
 * PRIVATE HELPERS (file-local fields in declaration order):
 * StrList: char **items — string rows; int count — used rows; int cap — storage.
 * Cmd: alias of StrList — child argument collection, same fields.
 * Stamp: long long sec — file modification seconds; long long nsec — fraction;
 * long long size — file bytes.
 * Job: Cmd cmd — arguments; char *log_path — captured output; pid_t pid — child;
 * int status — completion; bool started — admitted; const char *label — job name.
 * Target: const char *name — identity; TKind kind — lib/module/exe/app;
 * StrList srcs — source files; StrList includes — include directories;
 * StrList defs — own definitions; StrList pub_defs — propagated definitions;
 * StrList cflags — compile options; StrList deps — dependency target names;
 * StrList syslibs — platform link arguments; StrList objc_arc — ARC source files;
 * char *out_path — output; bool is_test — test admission; bool selected — build set.
 * TargetList: Target *items — target rows; int count — used; int cap — capacity.
 * Unit: Target *t — owner; const char *src — source; char *obj — object path;
 * char *dep — dependency file; char *meta — saved inputs; char *ohash_path — hash;
 * Cmd cmd — compiler argv; uint64_t cmdhash — options identity;
 * bool need_build — stale decision; Job job — child compile state.
 * GenStep: char *src — input; char *out — generated output; Cmd cmd — tool argv.
 * CovHit: char *test — owner test; char *src — subject; char *fn — covered function.
 * Runnable: const char *group — menu group; const char *name — target identity;
 * const char *label — display text.
 * Private global configuration/target/unit/generator/coverage tables persist
 * for this CLI invocation. No public class or cross-file ownership is added.
 * setup_graphvex registers six compositor shader entrypoints; color.frag also
 * watches filter/filter_type.h so its shared operation IDs invalidate SPIR-V.
 * Compositor GLSL resolves canonical includes from Graphvex's src root.
 * setup_graphvex_tests gives explicit Vulkan tests, including color_pass_test
 * gpu_scope_test and filter_gallery_fixture_test, the Homebrew headers and
 * loader link/rpath. Darling gallery apps already link that loader.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// ─────────────────────────────────────────────────────────────────────────────
// small utilities
// ─────────────────────────────────────────────────────────────────────────────

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "b: error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) die("out of memory");
    return p;
}

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) die("out of memory");
    return q;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

static char *strf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = xmalloc((size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return s;
}

static bool path_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static bool is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static void mkdir_p(const char *path) {
    char *tmp = xstrdup(path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    free(tmp);
}

static void mkdir_parent(const char *path) {
    char *tmp = xstrdup(path);
    char *slash = strrchr(tmp, '/');
    if (slash) {
        *slash = 0;
        mkdir_p(tmp);
    }
    free(tmp);
}

static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = xmalloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    if (len_out) *len_out = got;
    return buf;
}

static bool write_file(const char *path, const char *data) {
    mkdir_parent(path);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite(data, 1, strlen(data), f);
    fclose(f);
    return true;
}

// strip the workspace-root prefix from a path/command so generated files are
// portable: "/root/eco/x.c" -> "eco/x.c" (resolved against the db dir).
static char *relativize(const char *s, const char *root) {
    size_t rl = strlen(root), n = strlen(s);
    char *out = xmalloc(n + 1);
    size_t w = 0;
    for (size_t i = 0; i < n;) {
        if (rl > 0 && i + rl <= n && strncmp(s + i, root, rl) == 0 &&
            (s[i + rl] == '/' || s[i + rl] == '\0')) {
            i += rl;
            if (i < n && s[i] == '/') i++;   // drop the separator too
        } else {
            out[w++] = s[i++];
        }
    }
    out[w] = '\0';
    return out;
}

// escape a string for a JSON string literal (quotes/backslashes break b.json)
static char *json_escape(const char *s) {
    size_t n = 1;
    for (const char *p = s; *p; p++) n += (*p == '"' || *p == '\\') ? 2 : 1;
    char *out = xmalloc(n);
    char *w = out;
    for (const char *p = s; *p; p++) {
        if (*p == '"' || *p == '\\') *w++ = '\\';
        *w++ = *p;
    }
    *w = 0;
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// string list + command
// ─────────────────────────────────────────────────────────────────────────────

typedef struct {
    char **items;
    int count, cap;
} StrList;

static void strl_push(StrList *l, const char *s) {
    if ((*l).count == (*l).cap) {
        (*l).cap = (*l).cap ? (*l).cap * 2 : 8;
        (*l).items = xrealloc((*l).items, (size_t) (*l).cap * sizeof(char *));
    }
    (*l).items[(*l).count++] = xstrdup(s);
}

static void strl_pushf(StrList *l, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = xmalloc((size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    strl_push(l, s);
    free(s);
}

static void strl_extend(StrList *dst, const StrList *src) {
    for (int i = 0; i < (*src).count; i++) strl_push(dst, (*src).items[i]);
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void strl_sort(StrList *l) {
    if ((*l).count) qsort((*l).items, (size_t) (*l).count, sizeof(char *), cmp_str);
}

// a command is just a list of argv entries
typedef StrList Cmd;

static char **cmd_argv(const Cmd *c) {
    char **argv = xmalloc(((size_t) (*c).count + 1) * sizeof(char *));
    for (int i = 0; i < (*c).count; i++) argv[i] = (*c).items[i];
    argv[(*c).count] = NULL;
    return argv;
}

static char *cmd_join(const Cmd *c) {
    size_t n = 1;
    for (int i = 0; i < (*c).count; i++) n += strlen((*c).items[i]) + 1;
    char *s = xmalloc(n);
    s[0] = 0;
    for (int i = 0; i < (*c).count; i++) {
        if (i) strcat(s, " ");
        strcat(s, (*c).items[i]);
    }
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// hashing (FNV-1a 64)
// ─────────────────────────────────────────────────────────────────────────────

static uint64_t fnv1a(uint64_t h, const void *data, size_t n) {
    const unsigned char *p = data;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t hash_str(uint64_t h, const char *s) {
    return fnv1a(h, s, strlen(s));
}

#define HASH_SEED 1469598103934665603ULL

// hash file contents; returns false if unreadable
static bool hash_file(uint64_t *h, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) *h = fnv1a(*h, buf, n);
    fclose(f);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// file mtime/size signature (for the fast up-to-date path)
// ─────────────────────────────────────────────────────────────────────────────

typedef struct {
    long long sec, nsec, size;
} Stamp;

static bool stamp_of(const char *path, Stamp *out) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    (*out).sec = (long long)st.st_mtime;
    (*out).nsec = (long long)st.st_mtimespec.tv_nsec;
    (*out).size = (long long)st.st_size;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// process execution (fork/exec, output captured to a log file)
// ─────────────────────────────────────────────────────────────────────────────

typedef struct {
    Cmd cmd;
    char *log_path;
    pid_t pid;
    int status;
    bool started;
    const char *label;
} Job;

static int g_max_jobs = 0;

static void run_sync(const Cmd *cmd) {
    char **argv = cmd_argv(cmd);
    pid_t pid = fork();
    if (pid < 0) die("fork failed: %s", strerror(errno));
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "b: exec failed: %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    free(argv);
    int st;
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
        die("command failed: %s", cmd_join(cmd));
}

static int run_capture(const Cmd *cmd, char **out_stdout) {
    int pipefd[2];
    if (pipe(pipefd) != 0) die("pipe failed");
    char **argv = cmd_argv(cmd);
    pid_t pid = fork();
    if (pid < 0) die("fork failed");
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], 1);
        close(pipefd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    free(argv);
    close(pipefd[1]);
    size_t cap = 4096, len = 0;
    char *buf = xmalloc(cap);
    ssize_t n;
    while ((n = read(pipefd[0], buf + len, cap - len - 1)) > 0) {
        len += (size_t)n;
        if (len + 1 >= cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
    }
    buf[len] = 0;
    close(pipefd[0]);
    int st;
    waitpid(pid, &st, 0);
    if (out_stdout) *out_stdout = buf;
    else free(buf);
    if (!WIFEXITED(st)) return -1;
    return WEXITSTATUS(st);
}

static void job_start(Job *j) {
    mkdir_parent((*j).log_path);
    int fd = open((*j).log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) die("cannot open log %s", (*j).log_path);
    char **argv = cmd_argv(&(*j).cmd);
    pid_t pid = fork();
    if (pid < 0) die("fork failed");
    if (pid == 0) {
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
        execvp(argv[0], argv);
        fprintf(stderr, "exec failed: %s\n", strerror(errno));
        _exit(127);
    }
    free(argv);
    close(fd);
    (*j).pid = pid;
    (*j).started = true;
}

// run a batch of jobs with at most max_par in flight; returns failure count
static int run_jobs(Job *jobs, int n, int max_par, bool verbose) {
    if (n == 0) return 0;
    int next = 0, running = 0, failed = 0;
    while (next < n || running > 0) {
        while (running < max_par && next < n) {
            job_start(&jobs[next]);
            running++;
            next++;
        }
        int st;
        pid_t done = waitpid(-1, &st, 0);
        if (done < 0) break;
        running--;
        for (int i = 0; i < n; i++) {
            if (jobs[i].started && jobs[i].pid == done) {
                jobs[i].status = st;
                bool ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
                if (!ok) {
                    failed++;
                    fprintf(stderr, "\nb: FAILED: %s\n", cmd_join(&jobs[i].cmd));
                    size_t len = 0;
                    char *log = read_file(jobs[i].log_path, &len);
                    if (log) {
                        fwrite(log, 1, len, stderr);
                        free(log);
                    }
                } else if (verbose) {
                    printf("  ok  %s\n", jobs[i].label);
                }
                break;
            }
        }
    }
    return failed;
}

// ─────────────────────────────────────────────────────────────────────────────
// recursive source glob
// ─────────────────────────────────────────────────────────────────────────────

static void glob_rec(const char *dir, const char *suffix, StrList *out) {
    DIR *d = opendir(dir);
    if (!d) return;
    char *names[4096];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 4096) {
        if (!strcmp((*e).d_name, ".") || !strcmp((*e).d_name, "..")) continue;
        names[n++] = xstrdup((*e).d_name);
    }
    closedir(d);
    for (int i = 0; i < n; i++) {
        char *full = strf("%s/%s", dir, names[i]);
        if (is_dir(full)) {
            glob_rec(full, suffix, out);
        } else {
            size_t lf = strlen(full), ls = strlen(suffix);
            if (lf >= ls && !strcmp(full + lf - ls, suffix)) strl_push(out, full);
        }
        free(full);
        free(names[i]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// target graph
// ─────────────────────────────────────────────────────────────────────────────

typedef enum { T_LIB, T_MOD, T_EXE, T_APP } TKind;

typedef struct {
    const char *name;
    TKind kind;
    StrList srcs;        // absolute source paths
    StrList includes;    // absolute include dirs
    StrList defs;        // -D... applied to this target
    StrList pub_defs;    // -D... propagated to dependents (PUBLIC)
    StrList cflags;      // extra compile flags
    StrList deps;        // names of target-level libs this links
    StrList syslibs;     // raw link args (-lpthread, "-framework Cocoa", ...)
    StrList objc_arc;    // sources needing -fobjc-arc
    char *out_path;      // resolved archive/binary path
    bool is_test;        // runnable by `b test`
    bool selected;       // included in the current build
} Target;

typedef struct {
    Target *items;
    int count, cap;
} TargetList;

static void tl_push(TargetList *tl, Target t) {
    if ((*tl).count == (*tl).cap) {
        (*tl).cap = (*tl).cap ? (*tl).cap * 2 : 8;
        (*tl).items = xrealloc((*tl).items, (size_t) (*tl).cap * sizeof(Target));
    }
    (*tl).items[(*tl).count++] = t;
}

static Target *find_target(TargetList *tl, const char *name) {
    for (int i = 0; i < (*tl).count; i++) {
        Target *target = &(*tl).items[i];
        if (!strcmp((*target).name, name)) return target;
    }
    return NULL;
}

// the global target list (declared here so unit_build_cmd can resolve deps)
static TargetList g_targets_ref;

#ifdef __APPLE__
static void apple_frameworks(StrList *l) {
    const char *fw[] = {
        "Foundation", "LocalAuthentication", "Network", "Security", "AVFoundation",
        "Cocoa", "AppKit", "CoreGraphics", "QuartzCore", "Metal", "IOKit", "CoreAudio",
        "AudioToolbox", "Contacts", "EventKit", "Photos", "CoreLocation",
        "UserNotifications", "CoreServices", "ImageIO", "IOSurface", "CoreVideo",
    };
    for (size_t i = 0; i < sizeof fw / sizeof fw[0]; i++) {
        strl_push(l, "-framework");
        strl_push(l, fw[i]);
    }
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// workspace + configuration
// ─────────────────────────────────────────────────────────────────────────────

static char *g_root;             // repo root (absolute)
static char *g_state;            // b home: binary + build state (out of the source tree)
static char *g_out;              // <state>/out/<cfg>
static char *g_deps;             // <state>/deps/<cfg>
static char *g_meta;             // <state>/meta/<cfg>
static char *g_cache;            // <state>/cache/objects
static bool g_release = false;
static bool g_verbose = false;
static bool g_coverage = false;          // build + run with clang source coverage
static const char *g_only = NULL;        // restrict the target graph to one subsystem
static const char *g_profraw = NULL;     // LLVM_PROFILE_FILE for the next test run
static char *g_cc_version = NULL;

static const char *VEXSPOKE = "ecosystem/vexspoke";

static void setup_paths(void) {
    const char *cfg = g_coverage ? "coverage" : (g_release ? "release" : "debug");
    g_out = strf("%s/out/%s", g_state, cfg);
    g_deps = strf("%s/deps/%s", g_state, cfg);
    g_meta = strf("%s/meta/%s", g_state, cfg);
    g_cache = strf("%s/cache/objects", g_state);
    mkdir_p(g_out);
    mkdir_p(strf("%s/obj", g_out));
    mkdir_p(strf("%s/lib", g_out));
    mkdir_p(strf("%s/bin", g_out));
    mkdir_p(g_deps);
    mkdir_p(g_meta);
    mkdir_p(g_cache);
    mkdir_p(strf("%s/logs", g_state));
}

// run one test: capture output to a log, enforce a wall-clock timeout, and
// return the exit status (-1 on timeout, -2 on signal).
static int run_test(const char *name, Cmd *cmd, int timeout_s) {
    char *log = strf("%s/logs/test_%s.log", g_state, name);
    mkdir_parent(log);
    int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) die("cannot open %s", log);
    char **argv = cmd_argv(cmd);
    pid_t pid = fork();
    if (pid < 0) die("fork failed");
    if (pid == 0) {
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
        if (g_profraw) setenv("LLVM_PROFILE_FILE", g_profraw, 1);
        execvp(argv[0], argv);
        _exit(127);
    }
    free(argv);
    close(fd);
    int st = 0;
    bool done = false;
    for (int i = 0; i < timeout_s * 20; i++) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) { done = true; break; }
        usleep(50000);
    }
    if (!done) {
        kill(pid, SIGKILL);
        waitpid(pid, &st, 0);
        return -1;
    }
    return WIFEXITED(st) ? WEXITSTATUS(st) : -2;
}

static char *cc_version(void) {
    if (g_cc_version) return g_cc_version;
    Cmd c = {0};
    strl_push(&c, "cc");
    strl_push(&c, "--version");
    char *out = NULL;
    if (run_capture(&c, &out) != 0) out = xstrdup("unknown");
    char *nl = strchr(out, '\n');
    if (nl) *nl = 0;
    g_cc_version = out;
    return out;
}

// base flags: the Build & Naming Conventions Law + Platform Support Floor Law
static void base_cflags(StrList *out) {
    strl_push(out, "-std=gnu23");
    strl_push(out, "-Wall");
    strl_push(out, "-Wextra");
    strl_push(out, "-Werror");
#ifdef __APPLE__
    strl_push(out, "-arch");
    strl_push(out, "arm64");
    strl_push(out, "-mmacosx-version-min=14.0");
#endif
    if (g_coverage) {
        // clang source-based coverage: every function/region gets counters.
        strl_push(out, "-fprofile-instr-generate");
        strl_push(out, "-fcoverage-mapping");
    }
    if (g_release) {
        strl_push(out, "-O2");
#ifdef __APPLE__
        strl_push(out, "-ffunction-sections");
        strl_push(out, "-fdata-sections");
#endif
    } else {
        strl_push(out, "-O0");
        strl_push(out, "-g");
    }
}

static void base_lflags(StrList *out) {
    if (g_release) {
#ifdef __APPLE__
        strl_push(out, "-Wl,-dead_strip");
#endif
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// the vexspoke module (v0.1 proof; later this becomes per-repo declarations)
// ─────────────────────────────────────────────────────────────────────────────

static char *abspath(const char *rel) {
    return strf("%s/%s", g_root, rel);
}

static Target *target_new(TargetList *tl, const char *name, TKind kind) {
    Target t = {0};
    t.name = name;
    t.kind = kind;
    t.selected = true;
    t.out_path = kind == T_LIB ? strf("%s/lib/lib%s.a", g_out, name)
               : kind == T_MOD ? strf("%s/modules/%s.so", g_out, name)
                               : strf("%s/bin/%s", g_out, name);
    tl_push(tl, t);
    return &(*tl).items[(*tl).count - 1];
}

static void add_exe_libs(Target *t) {
    strl_push(&(*t).syslibs, "-lpthread");
#ifdef __APPLE__
    apple_frameworks(&(*t).syslibs);
#endif
}

// ── ecosystem/vexspoke ──────────────────────────────────────────────────────
static void setup_vexspoke(TargetList *tl) {
    Target *v = target_new(tl, "vexspoke", T_LIB);
    StrList c = {0};
    glob_rec(abspath(strf("%s/src", VEXSPOKE)), ".c", &c);
#ifdef __APPLE__
    glob_rec(abspath(strf("%s/src", VEXSPOKE)), ".m", &c);
#endif
    strl_sort(&c);
    (*v).srcs = c;
    strl_push(&(*v).includes, abspath(strf("%s/src", VEXSPOKE)));
#ifdef __APPLE__
    strl_push(&(*v).cflags, "-mcpu=apple-m1");
    apple_frameworks(&(*v).syslibs);
#endif
    strl_push(&(*v).syslibs, "-lpthread");
    if (!g_release) strl_push(&(*v).pub_defs, "DEBUG_BORROW_CHECK=1");
}

// ── ecosystem/interface/darling-framework (R4: the UI framework — Frame, Panel) ───────
static void setup_darling(TargetList *tl) {
    char *base = abspath("ecosystem/interface/darling-framework");
    Target *lib = target_new(tl, "darling", T_LIB);
    StrList c = {0};
    // R4 is split by concern: src/ (Frame, Panel), src/input (accessibility),
    // src/event (coordinate resolution).
    glob_rec(strf("%s/src", base), ".c", &c);
    strl_sort(&c);
    (*lib).srcs = c;
    strl_push(&(*lib).includes, strf("%s/src", base));
    strl_push(&(*lib).deps, "hotcwap");   // R1: the OS window
    strl_push(&(*lib).deps, "graphvex");  // R3: Element + the renderer
}

// ── graphvex tests (tests/graphvex mirrors src/) ────────────────────────────
static void setup_graphvex_tests(TargetList *tl) {
    char *tdir = strf("%s/tests/graphvex", g_root);
    StrList ts = {0};
    glob_rec(tdir, "_test.c", &ts);
    strl_sort(&ts);
    for (int i = 0; i < ts.count; i++) {
        const char *bn = strrchr(ts.items[i], '/');
        bn = bn ? bn + 1 : ts.items[i];
        char *name = xstrdup(bn);
        name[strlen(name) - 2] = 0;
        Target *t = target_new(tl, name, T_EXE);
        (*t).is_test = true;
        strl_push(&(*t).srcs, ts.items[i]);
        strl_push(&(*t).includes, abspath("ecosystem/drivers/graphvex/src"));
        strl_push(&(*t).includes, abspath("tests"));   // test_support.h
        strl_push(&(*t).defs, "UNDEBUG");
        strl_push(&(*t).deps, "graphvex");
        add_exe_libs(t);
        // Explicit Vulkan clients require both SDK headers and the loader.
        if (!strcmp(name, "vk_renderer_test") || !strcmp(name, "device_test") ||
            !strcmp(name, "gpu_render_test") || !strcmp(name, "resize_clip_test") ||
            !strcmp(name, "surface_gpu_test") || !strcmp(name, "clip_rounded_test") ||
            !strcmp(name, "color_pass_test") || !strcmp(name, "gpu_scope_test") ||
            !strcmp(name, "filter_gallery_fixture_test")) {
            strl_push(&(*t).includes, "/opt/homebrew/include");
            strl_push(&(*t).syslibs, "-L/opt/homebrew/lib");
            strl_push(&(*t).syslibs, "-lvulkan");
            strl_push(&(*t).syslibs, "-Wl,-rpath,/opt/homebrew/lib");
        }
        if (!strcmp(name, "surface_gpu_test")) {
            // the zero-copy seam test creates a real IOSurface (Foundation).
            // The host helper is its own TU so the Apple headers never meet
            // graphvex's Rect in one translation unit.
            strl_push(&(*t).srcs, strf("%s/vulkan/iosurface_host.c", tdir));
            strl_push(&(*t).includes, strf("%s/vulkan", tdir));
            strl_push(&(*t).syslibs, "-framework");
            strl_push(&(*t).syslibs, "IOSurface");
            strl_push(&(*t).syslibs, "-framework");
            strl_push(&(*t).syslibs, "CoreFoundation");
        }
    }
}

static void setup_vexspoke_tests(TargetList *tl) {
    char *tests = strf("%s/tests/vexspoke", g_root);
    StrList srcs = {0};
    glob_rec(tests, ".c", &srcs);
    strl_sort(&srcs);
    StrList seen = {0};
    for (int i = 0; i < srcs.count; i++) {
        const char *s = srcs.items[i];
        const char *base = strrchr(s, '/');
        base = base ? base + 1 : s;
        size_t bl = strlen(base);
        bool is_test = bl > 7 && !strcmp(base + bl - 7, "_test.c");
        if (!is_test && strcmp(base, "touchid_demo.c")) continue;
        char *name = xstrdup(base);
        name[strlen(name) - 2] = 0;
        bool dup = false;
        for (int k = 0; k < seen.count; k++)
            if (!strcmp(seen.items[k], name)) { dup = true; break; }
        if (dup) { free(name); continue; }
        strl_push(&seen, name);

        Target *t = target_new(tl, name, T_EXE);
        (*t).is_test = is_test;
        strl_push(&(*t).srcs, s);
        strl_push(&(*t).includes, abspath(strf("%s/src", VEXSPOKE)));
        strl_push(&(*t).includes, abspath("tests"));   // test_support.h
        strl_push(&(*t).defs, "UNDEBUG");
        strl_push(&(*t).deps, "vexspoke");
        add_exe_libs(t);
    }
}

// ── ecosystem/interface/sesh (header-only until sources land) ─────────────────────────
static void setup_sesh(TargetList *tl) {
    char *src = abspath("ecosystem/interface/sesh/src");
    StrList c = {0};
    glob_rec(src, ".c", &c);
    if (c.count == 0) return;
    strl_sort(&c);
    Target *t = target_new(tl, "sesh", T_LIB);
    (*t).srcs = c;
    strl_push(&(*t).includes, src);
    strl_push(&(*t).includes, abspath("ecosystem/interface/sesh"));
    strl_push(&(*t).deps, "vexspoke");
}

// ── projects/impedance ──────────────────────────────────────────────────────
static void setup_impedance(TargetList *tl) {
    char *base = abspath("projects/impedance");
    Target *lib = target_new(tl, "impedance", T_LIB);
    strl_push(&(*lib).srcs, strf("%s/src/impedance.c", base));
    strl_push(&(*lib).includes, strf("%s/src", base));
    strl_push(&(*lib).includes, abspath(strf("%s/src", VEXSPOKE)));
    strl_push(&(*lib).deps, "vexspoke");

    char *mainc = strf("%s/src/main.c", base);
    if (path_exists(mainc)) {
        Target *app = target_new(tl, "impedance_app", T_EXE);
        strl_push(&(*app).srcs, mainc);
        strl_push(&(*app).includes, strf("%s/src", base));
        strl_push(&(*app).includes, abspath(strf("%s/src", VEXSPOKE)));
        strl_push(&(*app).deps, "impedance");
        strl_push(&(*app).deps, "vexspoke");
        add_exe_libs(app);
    }
}

// ── ecosystem/drivers/api-haven ─────────────────────────────────────────────────────
static void setup_apihaven(TargetList *tl) {
    char *base = abspath("ecosystem/drivers/api-haven");
    const char *libs[] = {
        "src/api/client.c", "src/api/auth.c", "src/api/rest.c",
        "src/api/haven_ws_fanout.c", "src/com/discord/discord.c",
        "src/com/slack/slack.c", "src/ai/ai_provider.c", "src/ai/ai_chat.c",
        "src/ai/ai_chat_anthropic.c", "src/ai/ai_chat_gemini.c", "src/ai/ai_sse.c",
        "src/asset/asset_provider.c", "src/asset/asset_broker.c",
        "src/database/db_provider.c", "src/database/db_sqlite_file.c",
        "src/harness/engine_provider.c", "src/harness/harness.c",
        "src/app/app_provider.c", "src/app/app_broker.c",
        "src/search/search_provider.c", "src/mcp/mcp_server.c",
    };
    Target *lib = target_new(tl, "api_haven", T_LIB);
    for (size_t i = 0; i < sizeof libs / sizeof libs[0]; i++)
        strl_push(&(*lib).srcs, strf("%s/%s", base, libs[i]));
    strl_push(&(*lib).includes, strf("%s/src", base));
    strl_push(&(*lib).deps, "vexspoke");

    Target *mcp = target_new(tl, "mcp_server", T_EXE);
    strl_push(&(*mcp).srcs, strf("%s/src/main/mcp_main.c", base));
    strl_push(&(*mcp).includes, strf("%s/src", base));
    strl_push(&(*mcp).includes, abspath(strf("%s/src", VEXSPOKE)));
    strl_push(&(*mcp).deps, "api_haven");
    strl_push(&(*mcp).deps, "vexspoke");
    add_exe_libs(mcp);

    char *tdir = strf("%s/tests/api-haven", g_root);
    StrList ts = {0};
    glob_rec(tdir, "_test.c", &ts);
    strl_sort(&ts);
    for (int i = 0; i < ts.count; i++) {
        const char *bn = strrchr(ts.items[i], '/');
        bn = bn ? bn + 1 : ts.items[i];
        char *name = xstrdup(bn);
        name[strlen(name) - 2] = 0;
        Target *t = target_new(tl, name, T_EXE);
        (*t).is_test = true;
        strl_push(&(*t).srcs, ts.items[i]);
        strl_push(&(*t).includes, strf("%s/src", base));
        strl_push(&(*t).includes, abspath(strf("%s/src", VEXSPOKE)));
        strl_push(&(*t).includes, abspath("tests"));   // test_support.h
        strl_push(&(*t).deps, "api_haven");
        strl_push(&(*t).deps, "vexspoke");
        add_exe_libs(t);
    }
}

// ── ecosystem/hotcwap (R1: kernel host + hotload machinery) ─────────────────
static void setup_hotcwap(TargetList *tl) {
    char *base = abspath("ecosystem/hotcwap");
    Target *lib = target_new(tl, "hotcwap", T_LIB);
    const char *csrc[] = {
        "kernel/application.c", "kernel/process.c", "kernel/console.c",
        "kernel/kernel.c", "spoke/lifetime.c", "hot/hot.c",
        "hot/hot_trampoline.c", "hot/hot_retire.c", "hot/manifest.c",
        "hot/ledger.c", "hot/throwable.c", "window/window_event.c",
        "permission/permission.c", "capability/capability.c",
    };
    for (size_t i = 0; i < sizeof csrc / sizeof csrc[0]; i++)
        strl_push(&(*lib).srcs, strf("%s/%s", base, csrc[i]));
    strl_push(&(*lib).includes, base);
    strl_push(&(*lib).deps, "vexspoke");
#ifdef __APPLE__
    const char *msrc[] = {
        "window/window_cocoa.m", "window/traffic_light_cocoa.m",
        "permission/objc/permission_cocoa.m",
    };
    for (size_t i = 0; i < sizeof msrc / sizeof msrc[0]; i++)
        strl_push(&(*lib).srcs, strf("%s/%s", base, msrc[i]));
    apple_frameworks(&(*lib).syslibs);
#endif
    strl_push(&(*lib).syslibs, "-lpthread");

    // hotload modules: the same source twice, once with a foreign schema magic
    Target *mod = target_new(tl, "hot_behavior", T_MOD);
    strl_push(&(*mod).srcs, strf("%s/hot/hot_behavior.c", base));
    strl_push(&(*mod).includes, base);
    strl_push(&(*mod).includes, abspath(strf("%s/src", VEXSPOKE)));
    char *modpath = xstrdup((*mod).out_path);  // copying: tl_push below may realloc

    Target *bad = target_new(tl, "hot_behavior_bad", T_MOD);
    strl_push(&(*bad).srcs, strf("%s/hot/hot_behavior.c", base));
    strl_push(&(*bad).includes, base);
    strl_push(&(*bad).includes, abspath(strf("%s/src", VEXSPOKE)));
    strl_push(&(*bad).defs, "HOT_BEHAVIOR_SCHEMA_MAGIC=0xBADC0DE5u");
    (*bad).out_path = strf("%s/modules_bad/hot_behavior.so", g_out);
    char *badpath = xstrdup((*bad).out_path);

    const char *needs_mod[] = {
        "manifest_hot_test", "two_dylib_swap_test", "wrong_binary_test",
        "retire_ring_overflow_test", "shutdown_order_test", "manifest_rollback_test",
        "hot_behavior_test",
    };

    char *tdir = strf("%s/tests/hotcwap", g_root);
    StrList ts = {0};
    glob_rec(tdir, "_test.c", &ts);
    strl_sort(&ts);
    for (int i = 0; i < ts.count; i++) {
        const char *bn = strrchr(ts.items[i], '/');
        bn = bn ? bn + 1 : ts.items[i];
        char *name = xstrdup(bn);
        name[strlen(name) - 2] = 0;
        Target *t = target_new(tl, name, T_EXE);
        (*t).is_test = true;
        strl_push(&(*t).srcs, ts.items[i]);
        strl_push(&(*t).includes, base);
        strl_push(&(*t).includes, abspath("tests"));   // test_support.h
        strl_push(&(*t).defs, "UNDEBUG");
        if (!strcmp(name, "spoke_test")) {
            strl_push(&(*t).srcs, strf("%s/spoke/lifetime.c", base));
            strl_push(&(*t).deps, "vexspoke");
        } else {
            strl_push(&(*t).deps, "hotcwap");
        }
        add_exe_libs(t);
        for (size_t k = 0; k < sizeof needs_mod / sizeof needs_mod[0]; k++)
            if (!strcmp(name, needs_mod[k]))
                strl_pushf(&(*t).defs, "HOT_BEHAVIOR_MODULE=\"%s\"", modpath);
        if (!strcmp(name, "manifest_rollback_test"))
            strl_pushf(&(*t).defs, "HOT_BEHAVIOR_BAD_MODULE=\"%s\"", badpath);
    }
}

static bool has_main(const char *path) {
    char *s = read_file(path, NULL);
    if (!s) return false;
    bool found = strstr(s, "int main") != NULL;
    free(s);
    return found;
}

// auto-discover apps: every tests/darling main() that is not a *_test.c
// becomes a bundleable app, built with any main-less sibling TU. The *_test.c
// files are the test suite (wired separately).
static void setup_apps(TargetList *tl) {
    char *dir = abspath("tests/darling");
    StrList all = {0};
    glob_rec(dir, ".c", &all);
    strl_sort(&all);
    StrList apps = {0}, shared = {0};
    for (int i = 0; i < all.count; i++) {
        const char *p = all.items[i];
        size_t plen = strlen(p);
        if (plen > 7 && !strcmp(p + plen - 7, "_test.c")) continue;   // the suite
        if (has_main(p)) strl_push(&apps, p);
        else strl_push(&shared, p);
    }
    for (int i = 0; i < apps.count; i++) {
        const char *bn = strrchr(apps.items[i], '/');
        bn = bn ? bn + 1 : apps.items[i];
        char *name = xstrdup(bn);
        name[strlen(name) - 2] = 0;
        Target *t = target_new(tl, name, T_APP);
        strl_push(&(*t).srcs, apps.items[i]);
        for (int k = 0; k < shared.count; k++) strl_push(&(*t).srcs, shared.items[k]);
        strl_push(&(*t).includes, dir);
        strl_push(&(*t).includes, abspath("tests")); // shared Application test starter
        strl_push(&(*t).includes, abspath(strf("%s/src", VEXSPOKE)));
        strl_push(&(*t).deps, "*");  // auto-link every library that exists
        add_exe_libs(t);
        // darling pulls in the Vulkan backend, so apps link the loader
        strl_push(&(*t).syslibs, "-L/opt/homebrew/lib");
        strl_push(&(*t).syslibs, "-lvulkan");
        strl_push(&(*t).syslibs, "-Wl,-rpath,/opt/homebrew/lib");
    }
}

// ── ecosystem/drivers/graphvex (R3: GPU driver + graphics core) ─────────────────────
static void add_gen(const char *src, const char *out, Cmd cmd);   // defined below

static void setup_graphvex(TargetList *tl) {
    char *base = abspath("ecosystem/drivers/graphvex");
    Target *lib = target_new(tl, "graphvex", T_LIB);
    StrList c = {0};
    glob_rec(strf("%s/src", base), ".c", &c);
    strl_sort(&c);
    (*lib).srcs = c;
    strl_push(&(*lib).includes, strf("%s/src", base));
    strl_push(&(*lib).includes, abspath(strf("%s/src", VEXSPOKE)));   // R3 borrows R2
    strl_push(&(*lib).cflags, "-I/opt/homebrew/include");             // Vulkan headers
    strl_push(&(*lib).cflags, strf("-I%s/shader", g_out));           // embedded SPIR-V header
    strl_push(&(*lib).deps, "vexspoke");

    // shaders -> SPIR-V (regenerated only when the .vert/.frag changes)
    const char *names[] = {"quad.vert", "quad.frag"};
    const char *dirs[]  = {"vert", "frag"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char *src = strf("%s/src/shaders/%s/%s", base, dirs[i], names[i]);
        char *out = strf("%s/shader/%s.spv", g_out, names[i]);
        Cmd g = {0};
        strl_push(&g, "glslangValidator");
        strl_push(&g, "-V");
        strl_push(&g, src);
        strl_push(&g, "-o");
        strl_push(&g, out);
        add_gen(src, out, g);
    }
    // Modular compositor entrypoints are compile-checked now; these blobs are
    // not embedded/bound by the flat renderer and do not confer GPU runtime
    // support. color.frag includes shared filter IDs; watch that header as well.
    const char *compositorShaders[] = {
        "scatter.vert", "scatter.frag", "resolve.vert", "resolve.frag", "color.frag", "scope.frag"
    };
    for (size_t i = 0; i < sizeof compositorShaders / sizeof compositorShaders[0]; i++) {
        char *src = strf("%s/src/shaders/compositor/%s", base, compositorShaders[i]);
        char *out = strf("%s/shader/compositor/%s.spv", g_out, compositorShaders[i]);
        Cmd g = {0};
        strl_push(&g, "glslangValidator");
        strl_push(&g, "-V");
        strl_pushf(&g, "-I%s/src", base);
        strl_push(&g, src);
        strl_push(&g, "-o");
        strl_push(&g, out);
        add_gen(src, out, g);
        if (!strcmp(compositorShaders[i], "color.frag"))
            add_gen(strf("%s/src/filter/filter_type.h", base), out, g);
    }
    // embed the SPIR-V into a header the renderer #includes (regen on EITHER
    // shader change — one step keyed on the vert, one on the frag)
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char *src = strf("%s/src/shaders/%s/%s", base, dirs[i], names[i]);
        char *hdr = strf("%s/shader/quad_spv.h", g_out);
        Cmd g = {0};
        strl_push(&g, "python3");
        strl_push(&g, strf("%s/tools/spv_header.py", g_root));
        strl_push(&g, strf("%s/shader/quad.vert.spv", g_out));
        strl_push(&g, strf("%s/shader/quad.frag.spv", g_out));
        strl_push(&g, hdr);
        add_gen(src, hdr, g);
    }
}

// ── darling tests (tests/darling — the battle suite lives in tests/, never in
//    an ecosystem repo) ──────────────────────────────────────────────────────
static void setup_darling_tests(TargetList *tl) {
    char *tdir = strf("%s/tests/darling", g_root);
    StrList ts = {0};
    glob_rec(tdir, "_test.c", &ts);
    strl_sort(&ts);
    for (int i = 0; i < ts.count; i++) {
        const char *bn = strrchr(ts.items[i], '/');
        bn = bn ? bn + 1 : ts.items[i];
        char *name = xstrdup(bn);
        name[strlen(name) - 2] = 0;
        Target *t = target_new(tl, name, T_EXE);
        (*t).is_test = true;
        strl_push(&(*t).srcs, ts.items[i]);
        strl_push(&(*t).includes, abspath("ecosystem/interface/darling-framework/src"));
        strl_push(&(*t).includes, abspath("tests"));
        strl_push(&(*t).defs, "UNDEBUG");
        strl_push(&(*t).deps, "darling");
        add_exe_libs(t);
        strl_push(&(*t).syslibs, "-L/opt/homebrew/lib");
        strl_push(&(*t).syslibs, "-lvulkan");
        strl_push(&(*t).syslibs, "-Wl,-rpath,/opt/homebrew/lib");
    }
}

static void setup_targets(TargetList *tl) {
    if (g_only) {
        // a scoped build: only the named subsystem's lib + its tests. Used by
        // `b coverage` so a broken sibling repo cannot block the proof.
        if (!strcmp(g_only, "vexspoke")) {
            setup_vexspoke(tl);
            setup_vexspoke_tests(tl);
        }
        return;
    }
    setup_vexspoke(tl);        // libs first
    setup_hotcwap(tl);
    setup_graphvex(tl);
    setup_sesh(tl);
    setup_impedance(tl);
    setup_apihaven(tl);
    setup_darling(tl);
    setup_apps(tl);            // tests/darling/*.c apps
    setup_graphvex_tests(tl);  // tests/graphvex mirrors graphvex/src
    setup_darling_tests(tl);   // tests/darling mirrors darling-framework/src
    setup_vexspoke_tests(tl);  // executables last
}

// ─────────────────────────────────────────────────────────────────────────────
// unit compile + cache
// ─────────────────────────────────────────────────────────────────────────────

typedef struct {
    Target *t;
    const char *src;
    char *obj;
    char *dep;
    char *meta;
    char *ohash_path;
    Cmd cmd;
    uint64_t cmdhash;
    bool need_build;
    Job job;
} Unit;

static char *mangle(const char *src) {
    char *s = xstrdup(src);
    for (char *p = s; *p; p++) {
        if (*p == '/') *p = '_';
    }
    char *dot = strrchr(s, '.');
    if (dot) *dot = 0;
    return s;
}

static void unit_paths(Unit *u) {
    // make obj/dep names stable & unique to the target
    Target *target = (*u).t;
    char *m = mangle((*u).src);
    (*u).obj = strf("%s/obj/%s/%s.o", g_out, (*target).name, m);
    (*u).dep = strf("%s/%s/%s.d", g_deps, (*target).name, m);
    (*u).meta = strf("%s/%s/%s.meta", g_meta, (*target).name, m);
    (*u).ohash_path = strf("%s/obj/%s/%s.ohash", g_out, (*target).name, m);
    mkdir_parent((*u).obj);
    mkdir_parent((*u).dep);
    mkdir_parent((*u).meta);
    free(m);
}

static void strl_push_unique(StrList *l, const char *s) {
    for (int i = 0; i < (*l).count; i++)
        if (!strcmp((*l).items[i], s)) return;
    strl_push(l, s);
}

// gather a dep's PUBLIC include dirs + defs, transitively (CMake's PUBLIC
// propagation). The sentinel dep "*" expands to every library that exists.
static void collect_pub(const Target *t, StrList *incs, StrList *defs, int depth) {
    if (depth > 8) return;
    const StrList *includes = &(*t).includes;
    const StrList *publicDefinitions = &(*t).pub_defs;
    const StrList *dependencies = &(*t).deps;
    for (int i = 0; i < (*includes).count; i++) strl_push_unique(incs, (*includes).items[i]);
    for (int i = 0; i < (*publicDefinitions).count; i++) strl_push_unique(defs, (*publicDefinitions).items[i]);
    for (int d = 0; d < (*dependencies).count; d++) {
        const char *dn = (*dependencies).items[d];
        if (!strcmp(dn, "*")) {
            for (int i = 0; i < g_targets_ref.count; i++)
                if (g_targets_ref.items[i].kind == T_LIB)
                    collect_pub(&g_targets_ref.items[i], incs, defs, depth + 1);
            continue;
        }
        Target *dt = find_target(&g_targets_ref, dn);
        if (dt) collect_pub(dt, incs, defs, depth + 1);
    }
}

static void unit_build_cmd(Unit *u) {
    Cmd *cmd = &(*u).cmd;
    Target *target = (*u).t;
    const StrList *definitions = &(*target).defs;
    strl_push(cmd, "cc");
    StrList base = {0};
    base_cflags(&base);
    strl_extend(cmd, &base);
    strl_extend(cmd, &(*target).cflags);
    for (int i = 0; i < (*definitions).count; i++)
        strl_pushf(cmd, "-D%s", (*definitions).items[i]);
    // transitive PUBLIC defs + includes (own + deps, "*" = all libs)
    StrList incs = {0};
    StrList pubd = {0};
    collect_pub(target, &incs, &pubd, 0);
    for (int i = 0; i < pubd.count; i++) strl_pushf(cmd, "-D%s", pubd.items[i]);
    for (int i = 0; i < incs.count; i++) {
        strl_push(cmd, "-I");
        strl_push(cmd, incs.items[i]);
    }
    size_t sl = strlen((*u).src);
    if (sl > 2 && !strcmp((*u).src + sl - 2, ".m")) strl_push(cmd, "-fobjc-arc");
    strl_push(cmd, "-MMD");
    strl_push(cmd, "-MF");
    strl_push(cmd, (*u).dep);
    strl_push(cmd, "-c");
    strl_push(cmd, (*u).src);
    strl_push(cmd, "-o");
    strl_push(cmd, (*u).obj);

    uint64_t h = hash_str(HASH_SEED, cc_version());
    char *j = cmd_join(cmd);
    h = hash_str(h, j);
    free(j);
    (*u).cmdhash = h;
}

// ─────────────────────────────────────────────────────────────────────────────
// depfile parsing (compiler-emitted truth)
// ─────────────────────────────────────────────────────────────────────────────

static void parse_depfile(const char *path, StrList *headers) {
    size_t len = 0;
    char *buf = read_file(path, &len);
    if (!buf) return;
    // collapse continuations and newlines to spaces (our paths have no spaces)
    for (size_t i = 0; i < len; i++)
        if (buf[i] == '\\' || buf[i] == '\n' || buf[i] == '\r') buf[i] = ' ';
    char *colon = strchr(buf, ':');
    if (!colon) {
        free(buf);
        return;
    }
    char *p = colon + 1;
    bool first = true;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        char *tok = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
        if (first) {
            first = false;  // the object target itself
        } else if (*tok) {
            strl_push(headers, tok);
        }
    }
    free(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
// up-to-date decision + content cache
// ─────────────────────────────────────────────────────────────────────────────

// meta file layout:
//   cmd <hex>
//   in <sec> <nsec> <size> <path>     (source first, then each header)
//   obj <hex>                         (object content hash)
static bool unit_up_to_date(Unit *u) {
    uint64_t stored = 0;
    size_t len = 0;
    char *meta = read_file((*u).meta, &len);
    if (!meta) return false;
    if (!path_exists((*u).obj)) {
        free(meta);
        return false;
    }
    bool ok = true;
    char *save = NULL;
    for (char *line = strtok_r(meta, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(line, "cmd ", 4)) {
            stored = strtoull(line + 4, NULL, 16);
        } else if (!strncmp(line, "in ", 3)) {
            long long sec, nsec, size;
            char path[4096];
            if (sscanf(line + 3, "%lld %lld %lld %4095[^\n]", &sec, &nsec, &size, path) == 4) {
                char *full = path[0] == '/' ? xstrdup(path) : strf("%s/%s", g_root, path);
                Stamp st;
                if (!stamp_of(full, &st)) {
                    ok = false;
                } else if (st.sec != sec || st.nsec != nsec || st.size != size) {
                    ok = false;
                }
                free(full);
            }
        }
        if (!ok) break;
    }
    free(meta);
    if (!ok || stored != (*u).cmdhash) return false;
    return true;
}

static void write_meta(Unit *u, uint64_t obj_hash) {
    StrList hdrs = {0};
    parse_depfile((*u).dep, &hdrs);
    StrList lines = {0};
    strl_pushf(&lines, "cmd %llx", (unsigned long long) (*u).cmdhash);
    Stamp st;
    if (stamp_of((*u).src, &st))
        strl_pushf(&lines, "in %lld %lld %lld %s", st.sec, st.nsec, st.size, (*u).src);
    for (int i = 0; i < hdrs.count; i++) {
        if (stamp_of(hdrs.items[i], &st))
            strl_pushf(&lines, "in %lld %lld %lld %s", st.sec, st.nsec, st.size, hdrs.items[i]);
    }
    strl_pushf(&lines, "obj %llx", (unsigned long long)obj_hash);

    size_t n = 1;
    for (int i = 0; i < lines.count; i++) n += strlen(lines.items[i]) + 1;
    char *buf = xmalloc(n);
    buf[0] = 0;
    for (int i = 0; i < lines.count; i++) {
        strcat(buf, lines.items[i]);
        strcat(buf, "\n");
    }
    write_file((*u).meta, buf);
    free(buf);
}

// content hash over the exact inputs the compiler will see
static uint64_t unit_content_hash(Unit *u) {
    uint64_t h = (*u).cmdhash;
    hash_file(&h, (*u).src);
    StrList hdrs = {0};
    parse_depfile((*u).dep, &hdrs);
    for (int i = 0; i < hdrs.count; i++) {
        h = hash_str(h, hdrs.items[i]);
        hash_file(&h, hdrs.items[i]);
    }
    return h;
}

static char *cache_path(uint64_t h) {
    return strf("%s/%016llx.o", g_cache, (unsigned long long)h);
}

// ─────────────────────────────────────────────────────────────────────────────
// build driver
// ─────────────────────────────────────────────────────────────────────────────

static Unit *g_units = NULL;
static int g_unit_count = 0;

static void compile_all(void) {
    // build the unit list for every target
    int total = 0;
    for (int i = 0; i < g_targets_ref.count; i++)
        total += g_targets_ref.items[i].srcs.count;
    g_units = xmalloc((size_t)total * sizeof(Unit));
    g_unit_count = 0;

    Job *jobs = xmalloc((size_t)total * sizeof(Job));
    int job_count = 0;
    int fresh = 0;

    for (int i = 0; i < g_targets_ref.count; i++) {
        Target *t = &g_targets_ref.items[i];
        const StrList *sources = &(*t).srcs;
        for (int s = 0; s < (*sources).count; s++) {
            Unit *u = &g_units[g_unit_count++];
            memset(u, 0, sizeof *u);
            (*u).t = t;
            (*u).src = (*sources).items[s];
            unit_paths(u);
            unit_build_cmd(u);
            (*u).need_build = !unit_up_to_date(u);
            if ((*u).need_build) {
                Job j = {0};
                j.cmd = (*u).cmd;
                char *m = mangle((*sources).items[s]);
                j.log_path = strf("%s/logs/%s_%s.log", g_state, (*t).name, m);
                free(m);
                j.label = (*sources).items[s];
                jobs[job_count++] = j;
            } else {
                fresh++;
            }
        }
    }

    if (job_count == 0) {
        printf("b: %d unit(s) up to date\n", fresh);
        free(jobs);
        return;
    }
    printf("b: compiling %d unit(s) [%d up to date] with -j%d\n", job_count, fresh,
           g_max_jobs);
    int failed = run_jobs(jobs, job_count, g_max_jobs, g_verbose);
    if (failed) die("%d compilation(s) failed", failed);

    // post-process each compiled unit: meta + content cache
    for (int i = 0; i < g_unit_count; i++) {
        Unit *u = &g_units[i];
        if (!(*u).need_build) continue;
        uint64_t h = unit_content_hash(u);
        char *cp = cache_path(h);
        if (!path_exists(cp)) {
            Cmd c = {0};
            strl_push(&c, "cp");
            strl_push(&c, (*u).obj);
            strl_push(&c, cp);
            run_sync(&c);
        }
        write_meta(u, h);
        free(cp);
    }
    free(jobs);
}

static bool target_objs_changed(const Target *t) {
    for (int i = 0; i < g_unit_count; i++) {
        if (g_units[i].t == t && g_units[i].need_build) return true;
    }
    return false;
}

// hash of the target's object list, so adding/removing a source forces a relink
static uint64_t target_obj_hash(const Target *t) {
    uint64_t h = HASH_SEED;
    for (int i = 0; i < g_unit_count; i++)
        if (g_units[i].t == t) h = hash_str(h, g_units[i].obj);
    return h;
}

// assemble a double-clickable macOS .app bundle around a built app binary
static void make_app_bundle(Target *t) {
#ifdef __APPLE__
    char *app = strf("%s/apps/%s.app", g_out, (*t).name);
    char *macos = strf("%s/Contents/MacOS", app);
    mkdir_p(macos);
    mkdir_p(strf("%s/Contents/Resources", app));
    Cmd cp = {0};
    strl_push(&cp, "cp");
    strl_push(&cp, (*t).out_path);
    strl_push(&cp, strf("%s/%s", macos, (*t).name));
    run_sync(&cp);
    char *plist = strf(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n"
        "  <key>CFBundleName</key><string>%s</string>\n"
        "  <key>CFBundleDisplayName</key><string>%s</string>\n"
        "  <key>CFBundleIdentifier</key><string>dev.vexgraph.%s</string>\n"
        "  <key>CFBundleExecutable</key><string>%s</string>\n"
        "  <key>CFBundlePackageType</key><string>APPL</string>\n"
        "  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>\n"
        "  <key>CFBundleShortVersionString</key><string>0.1.0</string>\n"
        "  <key>CFBundleVersion</key><string>1</string>\n"
        "  <key>LSMinimumSystemVersion</key><string>14.0</string>\n"
        "  <key>NSHighResolutionCapable</key><true/>\n"
        "  <key>NSPrincipalClass</key><string>NSApplication</string>\n"
        "</dict>\n</plist>\n",
        (*t).name, (*t).name, (*t).name, (*t).name);
    write_file(strf("%s/Contents/Info.plist", app), plist);
    Cmd cs = {0};
    strl_push(&cs, "codesign");
    strl_push(&cs, "--force");
    strl_push(&cs, "--sign");
    strl_push(&cs, "-");
    strl_push(&cs, app);
    run_sync(&cs);
    printf("b: bundled %s\n", app);
#else
    (void)t;
#endif
}

// transitive link libraries (a dep's dep must follow it on the link line).
// The sentinel dep "*" links every library that exists (dependents first, so
// the dependency-free base lands last on the line).
static void collect_link_libs(const Target *t, StrList *out, int depth) {
    if (depth > 8) return;
    const StrList *dependencies = &(*t).deps;
    for (int d = 0; d < (*dependencies).count; d++) {
        const char *dn = (*dependencies).items[d];
        if (!strcmp(dn, "*")) {
            for (int i = g_targets_ref.count - 1; i >= 0; i--) {
                Target *dt = &g_targets_ref.items[i];
                if (dt == t || (*dt).kind != T_LIB) continue;
                strl_push_unique(out, (*dt).out_path);
                collect_link_libs(dt, out, depth + 1);
            }
            continue;
        }
        Target *dt = find_target(&g_targets_ref, dn);
        if (!dt) continue;
        strl_push_unique(out, (*dt).out_path);
        collect_link_libs(dt, out, depth + 1);
    }
}

// signature of a target's transitive libraries — a rebuilt dep must force a relink
static uint64_t target_dep_sig(const Target *t) {
    uint64_t h = HASH_SEED;
    StrList libs = {0};
    collect_link_libs(t, &libs, 0);
    strl_sort(&libs);
    for (int i = 0; i < libs.count; i++) {
        Stamp st;
        if (stamp_of(libs.items[i], &st)) {
            h = hash_str(h, libs.items[i]);
            h = fnv1a(h, &st, sizeof st);
        }
    }
    return h;
}

static void link_target(Target *t) {
    uint64_t listh = target_obj_hash(t);
    uint64_t depsig = target_dep_sig(t);
    bool need = target_objs_changed(t);
    char *lmeta = strf("%s/%s.link", g_meta, (*t).name);
    if (!need) {
        size_t len = 0;
        char *m = read_file(lmeta, &len);
        if (!m || !path_exists((*t).out_path)) {
            need = true;
        } else {
            uint64_t sl = 0, sd = 0;
            char *p = strstr(m, "list ");
            if (p) sl = strtoull(p + 5, NULL, 16);
            p = strstr(m, "dep ");
            if (p) sd = strtoull(p + 4, NULL, 16);
            if (sl != listh || sd != depsig) need = true;
        }
        free(m);
    }
    if (!need) return;

    Cmd c = {0};
    if ((*t).kind == T_LIB) {
        strl_push(&c, "ar");
        strl_push(&c, "rcs");
        strl_push(&c, (*t).out_path);
        for (int i = 0; i < g_unit_count; i++)
            if (g_units[i].t == t) strl_push(&c, g_units[i].obj);
    } else if ((*t).kind == T_MOD) {
        strl_push(&c, "cc");
        for (int i = 0; i < g_unit_count; i++)
            if (g_units[i].t == t) strl_push(&c, g_units[i].obj);
        // a loadable bundle: unresolved symbols bind at dlopen time
        strl_push(&c, "-bundle");
        strl_push(&c, "-Wl,-undefined,dynamic_lookup");
        if (g_coverage) strl_push(&c, "-fprofile-instr-generate");
        strl_push(&c, "-o");
        strl_push(&c, (*t).out_path);
    } else {
        strl_push(&c, "cc");
        StrList base = {0};
        base_lflags(&base);
        strl_extend(&c, &base);
        if (g_coverage) strl_push(&c, "-fprofile-instr-generate");
        for (int i = 0; i < g_unit_count; i++)
            if (g_units[i].t == t) strl_push(&c, g_units[i].obj);
        StrList libs = {0};
        collect_link_libs(t, &libs, 0);
        strl_extend(&c, &libs);
        strl_extend(&c, &(*t).syslibs);
        strl_push(&c, "-o");
        strl_push(&c, (*t).out_path);
    }
    if (g_verbose) printf("  link %s\n", (*t).name);
    mkdir_parent((*t).out_path);
    if ((*t).kind == T_LIB) unlink((*t).out_path);  // ar is incremental: rebuild clean
    run_sync(&c);
    if ((*t).kind == T_APP) make_app_bundle(t);
    write_file(lmeta, strf("list %016llx dep %016llx\n",
                           (unsigned long long)listh, (unsigned long long)depsig));
    free(lmeta);
}

// ── generated sources (shaders -> SPIR-V, etc.) ─────────────────────────────
typedef struct GenStep {
    char *src;
    char *out;
    Cmd cmd;
} GenStep;

static GenStep *g_gens = NULL;
static int g_genCount = 0;
static int g_genCap = 0;

static void add_gen(const char *src, const char *out, Cmd cmd) {
    if (g_genCount == g_genCap) {
        g_genCap = g_genCap ? g_genCap * 2 : 16;
        g_gens = xrealloc(g_gens, (size_t)g_genCap * sizeof *g_gens);
    }
    g_gens[g_genCount].src = xstrdup(src);
    g_gens[g_genCount].out = xstrdup(out);
    g_gens[g_genCount].cmd = cmd;
    g_genCount++;
}

static void run_gens(void) {
    for (int i = 0; i < g_genCount; i++) {
        Stamp s, o;
        if (path_exists(g_gens[i].out) && stamp_of(g_gens[i].src, &s) &&
            stamp_of(g_gens[i].out, &o) && s.sec <= o.sec && s.nsec <= o.nsec)
            continue;
        mkdir_parent(g_gens[i].out);
        if (g_verbose) printf("  gen  %s\n", g_gens[i].out);
        run_sync(&g_gens[i].cmd);
    }
}

static void build_all(void) {
    run_gens();
    compile_all();
    for (int i = 0; i < g_targets_ref.count; i++) link_target(&g_targets_ref.items[i]);
}

// ─────────────────────────────────────────────────────────────────────────────
// the coverage ratchet — the Per-File Battle Test Law + the Public Surface
// Proof Law, enforced in the C23 toolchain (not a script). Three shrink-only
// baselines under tests/vexspoke/ record the known gaps:
//   coverage_baseline.txt   units with no owner test
//   surface_baseline.txt    public functions the owner never names
//   function_baseline.txt   public functions the owner never executes
// mirror_exceptions.txt lists deliberately non-mirrored owner tests.
//   b check      static: owner + public-surface gates
//   b coverage   dynamic: instrument, run, lcov, function-execution gate
// ─────────────────────────────────────────────────────────────────────────────

#define VEX_SRC_REL   "ecosystem/vexspoke"
#define VEX_TESTS_REL "tests/vexspoke"

static bool ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static bool sl_has(const StrList *l, const char *s) {
    for (int i = 0; i < (*l).count; i++)
        if (!strcmp((*l).items[i], s)) return true;
    return false;
}

// one non-comment, trimmed line per entry (baselines)
static void load_lines(const char *path, StrList *out) {
    char *t = read_file(path, NULL);
    if (!t) return;
    char *save = NULL;
    for (char *line = strtok_r(t, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        while (*line == ' ' || *line == '\t' || *line == '\r') line++;
        size_t n = strlen(line);
        while (n && (line[n-1] == ' ' || line[n-1] == '\t' || line[n-1] == '\r')) line[--n] = 0;
        if (*line && *line != '#') strl_push(out, xstrdup(line));
    }
    free(t);
}

// "key value" per entry (mirror exceptions)
static void load_pairs(const char *path, StrList *keys, StrList *vals) {
    char *t = read_file(path, NULL);
    if (!t) return;
    char *save = NULL;
    for (char *line = strtok_r(t, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        while (*line == ' ' || *line == '\t' || *line == '\r') line++;
        if (!*line || *line == '#') continue;
        char *sp = line;
        while (*sp && *sp != ' ' && *sp != '\t' && *sp != '\r') sp++;
        if (*sp) {
            *sp++ = 0;
            while (*sp == ' ' || *sp == '\t' || *sp == '\r') sp++;
        }
        size_t n = strlen(sp);
        while (n && (sp[n-1] == ' ' || sp[n-1] == '\t' || sp[n-1] == '\r')) sp[--n] = 0;
        if (*sp) {
            strl_push(keys, xstrdup(line));
            strl_push(vals, xstrdup(sp));
        }
    }
    free(t);
}

static const char *TYPE_WORDS[] = {
    "void", "int", "char", "short", "long", "float", "double", "signed",
    "unsigned", "bool", "_Bool", "size_t", "ssize_t", "ptrdiff_t", "wchar_t",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
    "uint32_t", "uint64_t", "intptr_t", "uintptr_t", "va_list",
    "if", "for", "while", "switch", "return", "sizeof", "typeof", "defined",
    "alignof", "_Alignof", "assert", "static_assert", "_Static_assert",
};

static bool is_type_word(const char *s) {
    for (size_t i = 0; i < sizeof TYPE_WORDS / sizeof TYPE_WORDS[0]; i++)
        if (!strcmp(s, TYPE_WORDS[i])) return true;
    return false;
}

// strip comments, ;;ANNOTATION lines, preprocessor lines, and brace bodies, so
// only top-level declarations remain. A function-pointer typedef such as
// `void (*Fn)(void)` then yields no bogus name (the type word is filtered).
static char *scrub_header(const char *raw) {
    size_t n = strlen(raw), o = 0;
    char *tmp = xmalloc(n + 1);
    for (size_t i = 0; i < n;) {
        if (raw[i] == '/' && i + 1 < n && raw[i+1] == '*') {
            i += 2;
            while (i + 1 < n && !(raw[i] == '*' && raw[i+1] == '/')) i++;
            i = (i + 1 < n) ? i + 2 : n;
            tmp[o++] = ' ';
        } else if (raw[i] == '/' && i + 1 < n && raw[i+1] == '/') {
            while (i < n && raw[i] != '\n') i++;
        } else if (raw[i] == ';' && i + 1 < n && raw[i+1] == ';') {
            // `;;ANNOTATION` — may span lines when it opens a parenthesis
            // outside a string literal; consume through the matching close.
            i += 2;
            int depth = 0;
            bool in_str = false;
            for (; i < n; i++) {
                char ch = raw[i];
                if (in_str) {
                    if (ch == '\\' && i + 1 < n) i++;
                    else if (ch == '"') in_str = false;
                    continue;
                }
                if (ch == '"') { in_str = true; continue; }
                if (ch == '\n') { if (depth <= 0) break; }
                else if (ch == '(') depth++;
                else if (ch == ')') { depth--; if (depth <= 0) { i++; break; } }
            }
            tmp[o++] = ' ';
        } else if (raw[i] == '{') {
            int depth = 1;
            i++;
            while (i < n && depth) {
                if (raw[i] == '{') depth++;
                else if (raw[i] == '}') depth--;
                i++;
            }
            tmp[o++] = ' ';
        } else {
            tmp[o++] = raw[i++];
        }
    }
    tmp[o] = 0;

    char *clean = xmalloc(o + 1);
    size_t c = 0;
    bool line_start = true, skip = false;
    for (size_t i = 0; i < o; i++) {
        char ch = tmp[i];
        if (line_start) {
            size_t j = i;
            while (j < o && (tmp[j] == ' ' || tmp[j] == '\t')) j++;
            if (j < o && tmp[j] == '#') skip = true;
            line_start = false;
        }
        if (ch == '\n') {
            line_start = true;
            skip = false;
            clean[c++] = ch;
        } else if (!skip) {
            clean[c++] = ch;
        }
    }
    clean[c] = 0;
    free(tmp);
    return clean;
}

// public function names declared by a header, in declaration order
static void header_functions(const char *header_path, StrList *out) {
    char *raw = read_file(header_path, NULL);
    if (!raw) return;
    char *text = scrub_header(raw);
    free(raw);
    char *stmt = text;
    for (char *p = text;; p++) {
        if (*p == ';' || *p == 0) {
            bool end = (*p == 0);
            *p = 0;
            const char *par = strchr(stmt, '(');
            if (par) {
                const char *q = par;
                while (q > stmt && (q[-1] == ' ' || q[-1] == '\t' ||
                                    q[-1] == '\n' || q[-1] == '\r')) q--;
                const char *e = q;
                while (q > stmt && ident_char(q[-1])) q--;
                size_t len = (size_t)(e - q);
                if (len > 0 && len < 128) {
                    char name[128];
                    memcpy(name, q, len);
                    name[len] = 0;
                    if (!is_type_word(name) && !sl_has(out, name))
                        strl_push(out, xstrdup(name));
                }
            }
            if (end) break;
            stmt = p + 1;
        }
    }
    free(text);
}

static bool word_in(const char *name, const char *text) {
    size_t nl = strlen(name);
    for (const char *p = text; (p = strstr(p, name)); p += nl) {
        char before = (p == text) ? 0 : p[-1];
        char after = p[nl];
        if (!ident_char(before) && !ident_char(after)) return true;
    }
    return false;
}

// strip the workspace prefix and the repo prefix: /root/eco/vexspoke/src/x -> src/x
static const char *unit_rel(const char *unit) {
    const char *p = unit;
    size_t rl = strlen(g_root);
    if (!strncmp(p, g_root, rl)) {
        p += rl;
        while (*p == '/') p++;
    }
    const char *pre = VEX_SRC_REL "/";
    if (!strncmp(p, pre, strlen(pre))) p += strlen(pre);
    return p;
}

static char *own_header(const char *unit) {
    size_t n = strlen(unit);
    if (n < 3) return NULL;
    if (strcmp(unit + n - 2, ".c") && strcmp(unit + n - 2, ".m")) return NULL;
    char *stem = xstrdup(unit);
    stem[n-2] = 0;
    char *hdr = strf("%s.h", stem);
    free(stem);
    return path_exists(hdr) ? hdr : NULL;
}

static char *owner_test_for(const char *unit, const StrList *eu, const StrList *eo) {
    const char *rel = unit_rel(unit);
    for (int i = 0; i < (*eu).count; i++)
        if (!strcmp((*eu).items[i], rel)) return strf("%s/tests/%s", g_root, (*eo).items[i]);
    if (strncmp(rel, "src/", 4)) return NULL;
    char *tmp = xstrdup(rel + 4);
    char *slash = strrchr(tmp, '/');
    char *dir, *base;
    if (slash) {
        *slash = 0;
        dir = tmp;
        base = xstrdup(slash + 1);
    } else {
        dir = xstrdup("");
        base = tmp;
    }
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;
    char *out = NULL;
    char *tc = strf("%s/%s/%s%s%s_test.c", g_root, VEX_TESTS_REL, dir,
                    dir[0] ? "/" : "", base);
    if (path_exists(tc)) {
        out = tc;
    } else {
        char *tm = strf("%s/%s/%s%s%s_test.mm", g_root, VEX_TESTS_REL, dir,
                        dir[0] ? "/" : "", base);
        if (path_exists(tm)) out = tm;
    }
    free(dir);
    free(base);
    return out;
}

static bool src_matches(const char *src, const char *unit) {
    if (!strcmp(src, unit)) return true;
    const char *rel = unit_rel(unit);
    size_t sl = strlen(src), rl = strlen(rel);
    return sl >= rl && !strcmp(src + sl - rl, rel);
}

typedef struct {
    char *test;
    char *src;
    char *fn;
} CovHit;

static CovHit *g_cov;
static int g_cov_n, g_cov_cap;

static void cov_add(const char *test, const char *src, const char *fn) {
    if (g_cov_n == g_cov_cap) {
        g_cov_cap = g_cov_cap ? g_cov_cap * 2 : 256;
        g_cov = xrealloc(g_cov, (size_t)g_cov_cap * sizeof *g_cov);
    }
    g_cov[g_cov_n].test = xstrdup(test);
    g_cov[g_cov_n].src = xstrdup(src);
    g_cov[g_cov_n].fn = xstrdup(fn);
    g_cov_n++;
}

// executed by `test`? Functions defined in a header are compiled into each
// including unit and recorded against the header's record, so accept evidence
// from the unit's own source or its header.
static bool cov_has(const char *test, const char *unit, const char *hdr, const char *fn) {
    for (int i = 0; i < g_cov_n; i++) {
        if (strcmp(g_cov[i].fn, fn) || strcmp(g_cov[i].test, test)) continue;
        if (src_matches(g_cov[i].src, unit)) return true;
        if (hdr && src_matches(g_cov[i].src, hdr)) return true;
    }
    return false;
}

// read every *.lcov in dir: record executed functions (count > 0) keyed by test
static void load_lcov(const char *dir, StrList *stems) {
    StrList files = {0};
    glob_rec(dir, ".lcov", &files);
    strl_sort(&files);
    for (int i = 0; i < files.count; i++) {
        const char *path = files.items[i];
        const char *b = strrchr(path, '/');
        b = b ? b + 1 : path;
        char *stem = xstrdup(b);
        size_t sl = strlen(stem);
        if (sl > 5) stem[sl-5] = 0;                 // drop ".lcov"
        strl_push_unique(stems, stem);
        char *text = read_file(path, NULL);
        if (!text) continue;
        const char *cur_src = NULL;
        char *save = NULL;
        for (char *line = strtok_r(text, "\n", &save); line;
             line = strtok_r(NULL, "\n", &save)) {
            if (!strncmp(line, "SF:", 3)) {
                cur_src = line + 3;
            } else if (!strncmp(line, "FNDA:", 5) && cur_src) {
                char *comma = strchr(line + 5, ',');
                if (comma && atoi(line + 5) > 0) {
                    // a header-defined function is recorded as "file.c:name"
                    const char *fname = comma + 1;
                    const char *colon = strrchr(fname, ':');
                    if (colon) fname = colon + 1;
                    cov_add(stem, cur_src, fname);
                }
            }
        }
        free(text);
    }
}

static char *baseline_path(const char *name) {
    return strf("%s/%s/%s", g_root, VEX_TESTS_REL, name);
}

static int ratchet(const char *covdir, bool strict, bool list,
                   bool emit_units, bool emit_surface, bool emit_functions) {
    StrList units = {0};
    glob_rec(strf("%s/%s/src", g_root, VEX_SRC_REL), ".c", &units);
#ifdef __APPLE__
    glob_rec(strf("%s/%s/src", g_root, VEX_SRC_REL), ".m", &units);
#endif
    strl_sort(&units);

    StrList unit_known = {0}, surface_known = {0}, func_known = {0};
    load_lines(baseline_path("coverage_baseline.txt"), &unit_known);
    load_lines(baseline_path("surface_baseline.txt"), &surface_known);
    load_lines(baseline_path("function_baseline.txt"), &func_known);
    StrList exc_u = {0}, exc_o = {0};
    load_pairs(baseline_path("mirror_exceptions.txt"), &exc_u, &exc_o);

    StrList uncovered = {0}, surface_gaps = {0}, func_gaps = {0};
    int owned = 0, total_public = 0, checked = 0;

    for (int i = 0; i < units.count; i++) {
        const char *u = units.items[i];
        char *owner = owner_test_for(u, &exc_u, &exc_o);
        if (!owner) {
            strl_push(&uncovered, xstrdup(unit_rel(u)));
            continue;
        }
        owned++;
        char *hdr = own_header(u);
        if (!hdr) continue;
        StrList fns = {0};
        header_functions(hdr, &fns);
        char *txt = read_file(owner, NULL);
        if (txt) {
            for (int k = 0; k < fns.count; k++) {
                total_public++;
                if (!word_in(fns.items[k], txt))
                    strl_push(&surface_gaps, strf("%s %s", unit_rel(u), fns.items[k]));
            }
        }
        free(txt);
        free(hdr);
        free(owner);
    }

    if (covdir) {
        StrList stems = {0};
        load_lcov(covdir, &stems);
        for (int i = 0; i < units.count; i++) {
            const char *u = units.items[i];
            char *owner = owner_test_for(u, &exc_u, &exc_o);
            if (!owner) continue;
            char *hdr = own_header(u);
            if (!hdr) continue;
            const char *b = strrchr(owner, '/');
            b = b ? b + 1 : owner;
            char *stem = xstrdup(b);
            char *dot = strrchr(stem, '.');
            if (dot) *dot = 0;
            if (sl_has(&stems, stem)) {
                StrList fns = {0};
                header_functions(hdr, &fns);
                for (int k = 0; k < fns.count; k++) {
                    checked++;
                    if (!cov_has(stem, u, hdr, fns.items[k]))
                        strl_push(&func_gaps, strf("%s %s", unit_rel(u), fns.items[k]));
                }
            }
            free(stem);
            free(hdr);
            free(owner);
        }
    }

    strl_sort(&uncovered);
    strl_sort(&surface_gaps);
    strl_sort(&func_gaps);

    if (emit_units) {
        for (int i = 0; i < uncovered.count; i++) printf("%s\n", uncovered.items[i]);
        return 0;
    }
    if (emit_surface) {
        for (int i = 0; i < surface_gaps.count; i++) printf("%s\n", surface_gaps.items[i]);
        return 0;
    }
    if (emit_functions) {
        if (!covdir) {
            fprintf(stderr, "b check: --emit-functions needs `b coverage`\n");
            return 2;
        }
        for (int i = 0; i < func_gaps.count; i++) printf("%s\n", func_gaps.items[i]);
        return 0;
    }

    printf("vexspoke coverage ratchet\n");
    printf("  compiled units   : %d\n", units.count);
    printf("  owned            : %d\n", owned);
    printf("  uncovered        : %d (baseline %d)\n", uncovered.count, unit_known.count);
    printf("  public functions : %d\n", total_public);
    printf("  uninvoked        : %d (baseline %d)\n", surface_gaps.count, surface_known.count);
    if (covdir) {
        printf("  executed (dyn)   : %d / %d\n", checked - func_gaps.count, checked);
        printf("  unexecuted (dyn) : %d (baseline %d)\n", func_gaps.count, func_known.count);
    }

    if (list) {
        for (int i = 0; i < surface_gaps.count; i++)
            printf("  SURFACE %s\n", surface_gaps.items[i]);
        for (int i = 0; i < func_gaps.count; i++)
            printf("  EXEC    %s\n", func_gaps.items[i]);
    }

    int rc = 0;
    for (int i = 0; i < uncovered.count; i++)
        if (!sl_has(&unit_known, uncovered.items[i])) {
            if (!rc) printf("\nREGRESSION — unit(s) shipped without an owner test:\n");
            printf("  MISSING %s\n", uncovered.items[i]);
            rc = 1;
        }
    for (int i = 0; i < surface_gaps.count; i++)
        if (!sl_has(&surface_known, surface_gaps.items[i])) {
            if (rc != 1) printf("\nREGRESSION — public function(s) never invoked by the owner test:\n");
            printf("  UNINVOKED %s\n", surface_gaps.items[i]);
            rc = 1;
        }
    for (int i = 0; i < func_gaps.count; i++)
        if (!sl_has(&func_known, func_gaps.items[i])) {
            printf("\nREGRESSION — public function(s) never executed under `b coverage`:\n");
            printf("  UNEXECUTED %s\n", func_gaps.items[i]);
            rc = 1;
        }

    if (strict && (uncovered.count || surface_gaps.count || func_gaps.count)) rc = 1;
    if (rc == 0) printf("\nOK — no new coverage gaps\n");
    return rc;
}

// `b coverage [substr]`: build with clang source coverage, run each selected
// test exactly once, export lcov, then gate per-function execution natively.
// A test that does not pass contributes no coverage; it is reported and skipped.
static int run_coverage(StrList *rest) {
    const char *sub = NULL;
    bool strict = false, list = false, emit_functions = false;
    for (int i = 0; i < (*rest).count; i++) {
        const char *a = (*rest).items[i];
        if (!strcmp(a, "--strict")) strict = true;
        else if (!strcmp(a, "--list")) list = true;
        else if (!strcmp(a, "--emit-functions")) emit_functions = true;
        else if (a[0] != '-') sub = a;
    }
    build_all();
    char *covdir = strf("%s/cov", g_state);
    Cmd rm = {0};
    strl_push(&rm, "rm");
    strl_push(&rm, "-rf");
    strl_push(&rm, covdir);
    run_sync(&rm);
    mkdir_p(covdir);

    int ran = 0, failed = 0, skipped = 0;
    for (int i = 0; i < g_targets_ref.count; i++) {
        Target *t = &g_targets_ref.items[i];
        if (!(*t).is_test) continue;
        if (sub && !strstr((*t).name, sub)) continue;

        char *profraw = strf("%s/%s.profraw", covdir, (*t).name);
        g_profraw = profraw;
        Cmd c = {0};
        strl_push(&c, (*t).out_path);
        int st = run_test((*t).name, &c, 60);
        g_profraw = NULL;
        if (st == 77) {
            printf("  SKIP    %s (contract unproved) — coverage not counted\n", (*t).name);
            skipped++;
            continue;
        }
        if (st != 0) {
            printf("  FAIL    %s (exit %d) — coverage skipped\n", (*t).name, st);
            failed++;
            continue;
        }
        char *profdata = strf("%s/%s.profdata", covdir, (*t).name);
        Cmd mg = {0};
        strl_push(&mg, "xcrun");
        strl_push(&mg, "llvm-profdata");
        strl_push(&mg, "merge");
        strl_push(&mg, "-sparse");
        strl_push(&mg, profraw);
        strl_push(&mg, "-o");
        strl_push(&mg, profdata);
        char *junk = NULL;
        if (run_capture(&mg, &junk) != 0) {
            free(junk);
            fprintf(stderr, "b: llvm-profdata merge failed for %s\n", (*t).name);
            failed++;
            continue;
        }
        free(junk);

        Cmd ex = {0};
        strl_push(&ex, "xcrun");
        strl_push(&ex, "llvm-cov");
        strl_push(&ex, "export");
        strl_push(&ex, (*t).out_path);
        strl_push(&ex, "-instr-profile");
        strl_push(&ex, profdata);
        strl_push(&ex, "-format");
        strl_push(&ex, "lcov");
        char *lcov = NULL;
        if (run_capture(&ex, &lcov) != 0) {
            free(lcov);
            fprintf(stderr, "b: llvm-cov export failed for %s\n", (*t).name);
            failed++;
            continue;
        }
        write_file(strf("%s/%s.lcov", covdir, (*t).name), lcov);
        free(lcov);
        ran++;
    }
    printf("b: coverage from %d test(s), %d not passing, %d skipped\n",
           ran, failed, skipped);
    return ratchet(covdir, strict, list, false, false, emit_functions);
}

// ─────────────────────────────────────────────────────────────────────────────
// watch: rebuild on change (the hotload loop)
// ─────────────────────────────────────────────────────────────────────────────

static uint64_t watch_snapshot(void) {
    uint64_t h = HASH_SEED;
    StrList dirs = {0};
    for (int i = 0; i < g_targets_ref.count; i++) {
        Target *t = &g_targets_ref.items[i];
        const StrList *includes = &(*t).includes;
        const StrList *sources = &(*t).srcs;
        for (int d = 0; d < (*includes).count; d++) {
            bool seen = false;
            for (int k = 0; k < dirs.count; k++)
                if (!strcmp(dirs.items[k], (*includes).items[d])) { seen = true; break; }
            if (seen) continue;
            strl_push(&dirs, (*includes).items[d]);
            StrList all = {0};
            glob_rec((*includes).items[d], "", &all);
            strl_sort(&all);
            for (int k = 0; k < all.count; k++) {
                Stamp st;
                if (stamp_of(all.items[k], &st)) {
                    h = hash_str(h, all.items[k]);
                    h = fnv1a(h, &st, sizeof st);
                }
            }
        }
        for (int s = 0; s < (*sources).count; s++) {
            Stamp st;
            if (stamp_of((*sources).items[s], &st)) {
                h = hash_str(h, (*sources).items[s]);
                h = fnv1a(h, &st, sizeof st);
            }
        }
    }
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// b.json (clangd database)
// ─────────────────────────────────────────────────────────────────────────────

static void gen_compile_commands(void) {
    StrList entries = {0};
    for (int i = 0; i < g_unit_count; i++) {
        Unit *u = &g_units[i];
        // the db command needs neither the object (-o) nor the depfile (-MF);
        // dropping them keeps b.json free of machine-specific paths.
        Cmd dbc = {0};
        const Cmd *arguments = &(*u).cmd;
        for (int k = 0; k < (*arguments).count; k++) {
            const char *a = (*arguments).items[k];
            if (!strcmp(a, "-o") || !strcmp(a, "-MF")) { k++; continue; }
            strl_push(&dbc, a);
        }
        char *cmd = cmd_join(&dbc);
        char *r1 = relativize(cmd, g_root);
        char *rcmd = relativize(r1, g_state);   // strip the state dir too
        char *rsrc = relativize((*u).src, g_root);
        free(r1);
        char *ecmd = json_escape(rcmd);
        char *esrc = json_escape(rsrc);
        // portable: paths are relative to the db's own directory (the root)
        strl_pushf(&entries, "  {\n    \"directory\": \".\",\n    \"file\": \"%s\",\n    \"command\": \"%s\"\n  }",
                   esrc, ecmd);
        free(cmd);
        free(rcmd);
        free(rsrc);
        free(ecmd);
        free(esrc);
    }
    size_t n = 32;
    for (int i = 0; i < entries.count; i++) n += strlen(entries.items[i]) + 2;
    char *buf = xmalloc(n);
    strcpy(buf, "[\n");
    for (int i = 0; i < entries.count; i++) {
        strcat(buf, entries.items[i]);
        strcat(buf, i + 1 < entries.count ? ",\n" : "\n");
    }
    strcat(buf, "]\n");
    write_file(strf("%s/b.json", g_root), buf);
    printf("b: wrote b.json (%d entries)\n", entries.count);
    free(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
// self-rebuild + bootstrap
// ─────────────────────────────────────────────────────────────────────────────

// Machine-readable IDE seam. CMake consumes this graph instead of inventing
// a second set of include roots, public definitions, or archive link ordering.
static void ide_strings(const StrList *items) {
    printf("[");
    for (int i = 0; i < (*items).count; i++) {
        char *escaped = json_escape((*items).items[i]);
        printf("%s\"%s\"", i ? "," : "", escaped);
        free(escaped);
    }
    printf("]");
}

static void export_ide_graph(void) {
    printf("{\"byproducts\":[");
    int count = 0;
    for (int i = 0; i < g_targets_ref.count; i++) {
        Target *t = &g_targets_ref.items[i];
        if ((*t).kind != T_LIB && (*t).kind != T_MOD) continue;
        char *path = json_escape((*t).out_path);
        printf("%s\"%s\"", count++ ? "," : "", path);
        free(path);
    }
    printf("],\"tests\":[");
    count = 0;
    for (int i = 0; i < g_targets_ref.count; i++) {
        Target *t = &g_targets_ref.items[i];
        if (!(*t).is_test) continue;
        StrList includes = {0}, definitions = {0}, options = {0}, libraries = {0};
        collect_pub(t, &includes, &definitions, 0);
        strl_extend(&definitions, &(*t).defs);
        base_cflags(&options);
        strl_extend(&options, &(*t).cflags);
        collect_link_libs(t, &libraries, 0);
        // CMake treats each framework pair as one link item, not -lFoundation.
        const StrList *systemLibraries = &(*t).syslibs;
        for (int k = 0; k < (*systemLibraries).count; k++) {
            if (!strcmp((*systemLibraries).items[k], "-framework") && k + 1 < (*systemLibraries).count) {
                strl_pushf(&libraries, "-framework %s", (*systemLibraries).items[++k]);
            } else {
                strl_push(&libraries, (*systemLibraries).items[k]);
            }
        }
        char *name = json_escape((*t).name);
        printf("%s{\"name\":\"%s\",\"sources\":", count++ ? "," : "", name);
        free(name);
        ide_strings(&(*t).srcs);
        printf(",\"includes\":"); ide_strings(&includes);
        printf(",\"definitions\":"); ide_strings(&definitions);
        printf(",\"options\":"); ide_strings(&options);
        printf(",\"libraries\":"); ide_strings(&libraries);
        printf("}");
    }
    printf("]}\n");
}

static void rebuild_self(char **argv) {
    const char *src = "b/workspace.c";
    if (!path_exists(src)) return;

    char *bin = strf("%s/b", g_state);
    char *hashf = strf("%s/b.hash", g_state);

    uint64_t h = HASH_SEED;
    hash_file(&h, src);
    char hex[32];
    snprintf(hex, sizeof hex, "%016llx", (unsigned long long)h);

    size_t len = 0;
    char *stored = read_file(hashf, &len);
    bool same = stored && strncmp(stored, hex, 16) == 0;
    free(stored);

    if (same && path_exists(bin)) return;

    printf("b: rebuilding itself…\n");
    mkdir_p(g_state);
    Cmd c = {0};
    strl_push(&c, "cc");
    strl_push(&c, "-std=gnu23");
    strl_push(&c, "-O2");
    strl_push(&c, "-Wall");
    strl_push(&c, "-Wextra");
    strl_push(&c, src);
    strl_push(&c, "-Werror");
    strl_push(&c, "-arch");
    strl_push(&c, "arm64");
    strl_push(&c, "-mcpu=apple-m1");
    strl_push(&c, "-mmacosx-version-min=14.0");
    strl_push(&c, "-o");
    char *tmp = strf("%s/b.new", g_state);
    strl_push(&c, tmp);
    run_sync(&c);
    rename(tmp, bin);
    write_file(hashf, hex);

    // re-exec with the same args
    execv(bin, argv);
    die("re-exec failed: %s", strerror(errno));
}

// ─────────────────────────────────────────────────────────────────────────────
// runnable discovery — `b run` with no target lists what you can run, grouped
// by the directory the target lives in (apps from tests/darling/, tests from tests/…).
// This is the answer to "what do I run?" without spelunking the tree.
// ─────────────────────────────────────────────────────────────────────────────

static bool target_runnable(const Target *t) {
    return (*t).kind == T_APP || (*t).kind == T_EXE;
}

static const char *target_label(const Target *t) {
    if ((*t).is_test) return "test";
    if ((*t).kind == T_APP) return "app";
    return "tool";
}

// the directory (relative to the repo root) a target's main source lives in
static char *target_group(const Target *t) {
    const StrList *sources = &(*t).srcs;
    if ((*sources).count == 0) return xstrdup(".");
    const char *src = (*sources).items[0];
    size_t rl = strlen(g_root);
    const char *rel = (strncmp(src, g_root, rl) == 0 && src[rl] == '/') ? src + rl + 1 : src;
    const char *slash = strrchr(rel, '/');
    if (!slash) return xstrdup(".");
    size_t n = (size_t)(slash - rel);
    char *g = xmalloc(n + 1);
    memcpy(g, rel, n);
    g[n] = 0;
    return g;
}

typedef struct {
    const char *group;
    const char *name;
    const char *label;
} Runnable;

static void list_runnables(TargetList *tl) {
    Runnable *rs = xmalloc((size_t)((*tl).count ? (*tl).count : 1) * sizeof *rs);
    int n = 0;
    for (int i = 0; i < (*tl).count; i++) {
        Target *target = &(*tl).items[i];
        if (target_runnable(target))
            rs[n++] = (Runnable){target_group(target), (*target).name,
                                 target_label(target)};
    }

    // sort by group, then name (insertion sort — n is small)
    for (int i = 1; i < n; i++) {
        Runnable key = rs[i];
        int j = i - 1;
        while (j >= 0 && (strcmp(rs[j].group, key.group) > 0 ||
                          (!strcmp(rs[j].group, key.group) && strcmp(rs[j].name, key.name) > 0))) {
            rs[j + 1] = rs[j];
            j--;
        }
        rs[j + 1] = key;
    }

    printf("b: %d runnable target(s) — run one with `b run <name>`", n);
    if (n) printf(", or `b test <name>` for a test");
    printf("\n");
    const char *cur = NULL;
    for (int i = 0; i < n; i++) {
        if (!cur || strcmp(cur, rs[i].group)) {
            cur = rs[i].group;
            printf("\n  %s/\n", cur);
        }
        printf("    %-30s %s\n", rs[i].name, rs[i].label);
    }
    if (n) printf("\n");
    free(rs);
}

// names of runnable targets containing q
static void match_targets(TargetList *tl, const char *q, StrList *out) {
    for (int i = 0; i < (*tl).count; i++) {
        Target *t = &(*tl).items[i];
        if (target_runnable(t) && strstr((*t).name, q)) strl_push(out, (char*) (*t).name);
    }
}

// resolve a possibly-partial name to exactly one runnable target (silent on
// miss/ambiguity — the caller may fall through to an on-demand source)
static Target *resolve_runnable(TargetList *tl, const char *q) {
    Target *exact = NULL, *match = NULL;
    int hits = 0;
    for (int i = 0; i < (*tl).count; i++) {
        Target *t = &(*tl).items[i];
        if (!target_runnable(t)) continue;
        if (!strcmp((*t).name, q)) exact = t;
        if (strstr((*t).name, q)) { match = t; hits++; }
    }
    if (exact) return exact;
    if (hits == 1) return match;
    return NULL;
}

// ── compile-on-demand: run any source by its stem ───────────────────────────
// `b run <stem>` (or a path, with or without ".c") finds a .c with a main()
// under tests/ and, if it isn't already a target, builds a
// throwaway one on the spot — so loose files run without wiring a target.

static void match_sources(const char *root, const char *basename, StrList *out) {
    StrList all = {0};
    glob_rec(root, ".c", &all);
    for (int i = 0; i < all.count; i++) {
        const char *b = strrchr(all.items[i], '/');
        b = b ? b + 1 : all.items[i];
        if (!strcmp(b, basename)) strl_push(out, all.items[i]);
    }
}

static char *find_source_for(const char *name) {
    size_t nl = strlen(name);
    bool ends_c = nl > 2 && !strcmp(name + nl - 2, ".c");

    // an explicit path, or a bare name that exists as a file
    char *direct = ends_c ? xstrdup(name) : strf("%s.c", name);
    char *full = direct[0] == '/' ? xstrdup(direct) : strf("%s/%s", g_root, direct);
    if (path_exists(full)) return full;

    // a bare stem: search the runnable roots, tests/ first
    const char *base = strrchr(direct, '/');
    base = base ? base + 1 : direct;
    const char *roots[] = {"tests", "projects", "ecosystem"};
    for (size_t i = 0; i < sizeof roots / sizeof roots[0]; i++) {
        StrList hits = {0};
        match_sources(abspath(roots[i]), base, &hits);
        if (hits.count == 1) return hits.items[0];
        if (hits.count > 1) {
            strl_sort(&hits);
            fflush(stdout);
            fprintf(stderr, "b: '%s' matches %d sources — be more specific:\n", name, hits.count);
            for (int k = 0; k < hits.count; k++)
                fprintf(stderr, "  %s\n", relativize(hits.items[k], g_root));
            return NULL;
        }
    }
    return NULL;
}

// wrap a lone .c into a throwaway exe target that links every library
static Target *synth_target(TargetList *tl, const char *src) {
    const char *b = strrchr(src, '/');
    b = b ? b + 1 : src;
    size_t bl = strlen(b);
    char *name = xstrdup(b);
    if (bl > 2 && !strcmp(name + bl - 2, ".c")) name[bl - 2] = 0;

    Target *t = target_new(tl, name, T_EXE);
    strl_push(&(*t).srcs, src);
    char *dir = xstrdup(src);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = 0; else { free(dir); dir = xstrdup(g_root); }
    strl_push(&(*t).includes, dir);                                  // local headers
    strl_push(&(*t).cflags, "-I/opt/homebrew/include");              // Vulkan headers
    strl_push(&(*t).deps, "*");                                      // link + include everything
    add_exe_libs(t);
    strl_push(&(*t).syslibs, "-L/opt/homebrew/lib");
    strl_push(&(*t).syslibs, "-lvulkan");
    strl_push(&(*t).syslibs, "-Wl,-rpath,/opt/homebrew/lib");
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

static void usage(void) {
    printf("b — our build system\n"
           "usage: b [--release] [-j N] [-v] <command> [args]\n"
           "commands:\n"
           "  build [target...]   compile (all, or the named target + deps)\n"
           "  run [target] ...    run an app/tool/test; no target lists what's runnable\n"
           "  test [substr]       run the test suite (or just tests matching substr)\n"
           "  check               per-file owner + public-surface coverage gates\n"
           "  coverage [substr]   run tests instrumented; gate per-function execution\n"
           "  list                list every runnable target (alias: ls)\n"
           "  targets             list every target, including libraries\n"
           "  ide                 export test build metadata as JSON for CLion/CMake\n"
           "  watch | cc | clean | doctor\n"
           "  b run <name>        name may be partial (e.g. `b run gallery`)\n");
}

int main(int argc, char **argv) {
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd)) die("getcwd failed");
    g_root = xstrdup(cwd);
    const char *nh = getenv("B_HOME");
    if (nh && *nh) {
        g_state = xstrdup(nh);
    } else {
        const char *home = getenv("HOME");
        g_state = strf("%s/Library/Application Support/vexgraph/b", home ? home : ".");
    }

    // parse globals until the first non-flag
    int i = 1;
    const char *command = NULL;
    StrList rest = {0};
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1]) {
            if (!strcmp(a, "--release")) g_release = true;
            else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) g_verbose = true;
            else if (!strcmp(a, "-j")) {
                if (++i >= argc) die("-j needs a number");
                g_max_jobs = atoi(argv[i]);
            } else if (a[1] == 'j') {
                g_max_jobs = atoi(a + 2);
            } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
                usage();
                return 0;
            } else {
                die("unknown flag: %s", a);
            }
        } else {
            command = a;
            i++;
            break;
        }
    }
    for (; i < argc; i++) strl_push(&rest, argv[i]);
    if (g_max_jobs <= 0) g_max_jobs = 4;
    if (command && !strcmp(command, "coverage")) {
        g_coverage = true;
        g_only = "vexspoke";       // the ratchet is vexspoke-scoped
    }

    rebuild_self(argv);

    setup_paths();
    setup_targets(&g_targets_ref);

    if (!command) {
        usage();
        printf("\n");
        list_runnables(&g_targets_ref);
        return 0;
    }

    if (!strcmp(command, "targets")) {
        for (int k = 0; k < g_targets_ref.count; k++) {
            const char *kindstr = g_targets_ref.items[k].kind == T_LIB ? "lib"
                                : g_targets_ref.items[k].kind == T_MOD ? "mod"
                                : g_targets_ref.items[k].kind == T_APP ? "app" : "exe";
            printf("%-7s %s\n", kindstr, g_targets_ref.items[k].name);
        }
        return 0;
    }
    if (!strcmp(command, "ide")) {
        export_ide_graph();
        return 0;
    }
    if (!strcmp(command, "list") || !strcmp(command, "ls") || !strcmp(command, "apps")) {
        list_runnables(&g_targets_ref);
        return 0;
    }
    if (!strcmp(command, "doctor")) {
        printf("root:     %s\nconfig:   %s\ncc:       %s\njobs:     %d\n",
               g_root, g_release ? "release" : "debug", cc_version(), g_max_jobs);
        StrList f = {0};
        base_cflags(&f);
        char *s = cmd_join(&f);
        printf("cflags:   %s\n", s);
        free(s);
        for (int k = 0; k < g_targets_ref.count && k < 3; k++) {
            Target *t = &g_targets_ref.items[k];
            const StrList *sources = &(*t).srcs;
            printf("target %-12s %d src(s) -> %s\n", (*t).name, (*sources).count, (*t).out_path);
        }
        return 0;
    }
    if (!strcmp(command, "clean")) {
        Cmd c = {0};
        strl_push(&c, "rm");
        strl_push(&c, "-rf");
        strl_push(&c, strf("%s/out", g_state));
        run_sync(&c);
        printf("b: cleaned %s/out\n", g_state);
        return 0;
    }
    if (!strcmp(command, "cc")) {
        compile_all();
        gen_compile_commands();
        return 0;
    }
    if (!strcmp(command, "coverage")) return run_coverage(&rest);
    if (!strcmp(command, "check")) {
        bool strict = false, list = false, eu = false, es = false;
        for (int k = 0; k < rest.count; k++) {
            const char *a = rest.items[k];
            if (!strcmp(a, "--strict")) strict = true;
            else if (!strcmp(a, "--list")) list = true;
            else if (!strcmp(a, "--emit-units")) eu = true;
            else if (!strcmp(a, "--emit-surface")) es = true;
        }
        return ratchet(NULL, strict, list, eu, es, false);
    }
    if (!strcmp(command, "watch")) {
        build_all();
        uint64_t prev = watch_snapshot();
        printf("b: watching for changes… (Ctrl-C to stop)\n");
        fflush(stdout);
        for (;;) {
            usleep(300000);
            uint64_t cur = watch_snapshot();
            if (cur == prev) continue;
            printf("\n=== change detected — rebuilding ===\n");
            build_all();
            prev = watch_snapshot();
            fflush(stdout);
        }
    }
    if (!strcmp(command, "run") && rest.count == 0) {
        list_runnables(&g_targets_ref);
        return 0;
    }

    Target *run_target = NULL;
    if (!strcmp(command, "run")) {
        const char *q = rest.items[0];
        run_target = resolve_runnable(&g_targets_ref, q);
        if (!run_target) {
            StrList tm = {0};
            match_targets(&g_targets_ref, q, &tm);
            if (tm.count > 1) {
                fflush(stdout);
                fprintf(stderr, "b: '%s' matches %d targets — be more specific:\n", q, tm.count);
                for (int k = 0; k < tm.count; k++) fprintf(stderr, "  %s\n", tm.items[k]);
                return 1;
            }
            // no target: try it as a loose .c (compile on demand)
            char *src = find_source_for(q);
            if (!src) {
                fflush(stdout);
                fprintf(stderr, "b: nothing runnable matches '%s'  (try `b run` to list)\n", q);
                return 1;
            }
            const char *bs = strrchr(src, '/');
            bs = bs ? bs + 1 : src;
            char *stem = xstrdup(bs);
            size_t sl = strlen(stem);
            if (sl > 2 && !strcmp(stem + sl - 2, ".c")) stem[sl - 2] = 0;
            Target *decl = find_target(&g_targets_ref, stem);
            if (decl && target_runnable(decl)) {
                run_target = decl;
            } else {
                printf("b: compiling %s on demand\n", relativize(src, g_root));
                run_target = synth_target(&g_targets_ref, src);
            }
        }
    }

    if (!strcmp(command, "build") || !strcmp(command, "run") || !strcmp(command, "test")) {
        if (!strcmp(command, "test")) {
            // the static proof gate runs before the suite: no test run passes
            // while a unit lacks an owner or a public function goes uninvoked.
            int rc = ratchet(NULL, false, false, false, false, false);
            if (rc) return rc;
        }
        build_all();
        if (!strcmp(command, "build")) {
            gen_compile_commands();
            return 0;
        }
        if (!strcmp(command, "run")) {
            Target *t = run_target;
            if ((*t).kind == T_APP) {
                char *app = strf("%s/apps/%s.app", g_out, (*t).name);
                Cmd o = {0};
                strl_push(&o, "open");
                strl_push(&o, app);
                printf("b: launching %s\n", app);
                fflush(stdout);
                run_sync(&o);
                return 0;
            }
            char **a = xmalloc(((size_t)rest.count + 1) * sizeof(char *));
            a[0] = (*t).out_path;
            int n = 1;
            for (int k = 1; k < rest.count; k++) a[n++] = rest.items[k];
            a[n] = NULL;
            printf("b: launching %s\n", (*t).out_path);
            fflush(stdout);
            execv((*t).out_path, a);
            die("exec %s failed: %s", (*t).out_path, strerror(errno));
        }
        // test
        StrList tests = {0};
        for (int k = 0; k < g_targets_ref.count; k++) {
            Target *t = &g_targets_ref.items[k];
            if (!(*t).is_test) continue;
            if (rest.count && !strstr((*t).name, rest.items[0])) continue;
            strl_push(&tests, (*t).out_path);
        }
        printf("b: running %d test(s)\n", tests.count);
        int failed = 0, timedout = 0, skipped = 0;
        for (int k = 0; k < tests.count; k++) {
            const char *nm = strrchr(tests.items[k], '/');
            nm = nm ? nm + 1 : tests.items[k];
            Cmd c = {0};
            strl_push(&c, tests.items[k]);
            int st = run_test(nm, &c, 30);
            if (st == 0) {
                printf("  PASS    %s\n", nm);
            } else if (st == 77) {
                // B_TEST_SKIP: the contract was not exercised on this host.
                // Neither a pass nor a failure — but never silently green.
                printf("  SKIP    %s (contract unproved on this host)\n", nm);
                skipped++;
            } else if (st == -1) {
                printf("  TIMEOUT %s (>30s)\n", nm);
                timedout++;
            } else {
                printf("  FAIL    %s (exit %d)\n", nm, st);
                failed++;
                char *lp = strf("%s/logs/test_%s.log", g_state, nm);
                size_t len = 0;
                char *txt = read_file(lp, &len);
                if (txt && len) {
                    char *p = txt + len;
                    for (int lines = 0; p > txt && lines < 12;) {
                        p--;
                        if (*p == '\n') lines++;
                    }
                    fputs(p, stdout);
                }
                free(txt);
            }
        }
        printf("b: %d passed, %d failed, %d timed out, %d skipped\n",
               tests.count - failed - timedout - skipped, failed, timedout, skipped);
        if (skipped > 0 && g_verbose) {
            printf("b: %d test(s) skipped a contract — see the SKIP lines above; "
                   "a skip proves nothing\n", skipped);
        }
        return (failed || timedout) ? 1 : 0;
    }

    usage();
    return 1;
}
