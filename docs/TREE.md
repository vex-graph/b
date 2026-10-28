# b — command tree and examples

[Back to README](../README.md) · [JetBrains setup](JETBRAINS.md)

See [ADAPTERS.md](ADAPTERS.md) for build-only `glsl`/`metal` shader contracts
and recursive workspace safety. GLSL file selectors also have `glsl-vert`,
`glsl-frag`, `glsl-comp`, `glsl-geom`, `glsl-tesc` and `glsl-tese` aliases.
`b build workspace <directory> --plan` assesses before any tools run;
`--confirm` admits reviewed broad scopes. No sudo or automatic tool downloads.

b orchestrates installed tools; it does not replace their project configuration.
Examples assume `b` is on your PATH. From the b checkout, use `b` instead.
Install the required toolchain before using its adapter.

## Standalone command tree

```text
b
├── help | --help                         (no command also prints help)
├── project <name> <project-path>          (register a project under B_HOME)
├── project <name>                         (print its registered root)
├── project remove <name>                  (remove B's saved registration/index)
├── refresh <project-name>                 (refresh its adapter file index)
├── breeze
│   ├── exec <file> [--in <project>] [-- arguments...]
│   └── instance <file> [--in <project>] [-- arguments...]
├── run <exec|instance> ...              (alias for breeze)
├── build <adapter> [directory]
│   ├── native: c · cpp · objc · swift · java · rust · csharp · go · zig
│   ├── syntax/parse checks: python · r · javascript · typescript · php · shell · lua
│   ├── existing projects: cmake · npm · cargo · zig (build.zig)
│   ├── firmware compilation: arduino
│   ├── shader compilation: glsl · metal (build-only)
│   └── workspace <directory> [--plan|--confirm] (recursive assessment/build)
├── create
│   └── manifest <path>                 (write starter JSON; never overwrite)
└── box <manifest.json> <path-to-export>
    └── validate schema 1, then export the declared payload
        (only the `portable` format; every other format rejects)
```

The everyday cycle is **project → breeze or build → box**. `b run` is an alias
for `b breeze` with the same syntax. Adapter discovery, tool diagnostics and
Arduino upload are optional utilities described in [ADAPTERS.md](ADAPTERS.md);
they are not steps in normal project use.

The separate `./tools/b` ecosystem target engine is described below; it is not
a standalone `b workspace` command. `b build workspace` is the recursive builder.

HTML and SQL have run adapters but **no standalone build pipeline**; their build
commands reject. Arduino uses build/upload, not either source run mode. CMake
builds a project but has no source run adapter.

### Adapter names

| Primary name | Other accepted names | Files |
| --- | --- | --- |
| `c` | — | `.c` |
| `cpp` | `cxx`, `c++` | `.cpp`, `.cc`, `.cxx` |
| `objc` | — | `.m` (macOS Foundation/ARC) |
| `swift` | — | `.swift` |
| `java` | — | `.java` |
| `rust` | — | `.rs` |
| `csharp` | — | `.cs` |
| `python` | — | `.py` |
| `r` | `R` | `.R`, `.r` |
| `javascript` | `node`, `js` | `.js`, `.mjs`, `.cjs` |
| `typescript` | `ts`, `node-ts` | `.ts`, `.mts`, `.cts` |
| `php` | — | `.php` |
| `shell` | — | `.sh` (POSIX sh) |
| `html` | `web` | `.html`, `.htm` |
| `sql` | — | `.sql` (PostgreSQL) |
| `npm` | — | `package.json` |
| `arduino` | — | `.ino` (build/upload only) |
| `cmake` | — | Existing `../CMakeLists.txt` project (build only) |
| `go` | — | `.go` |
| `lua` | — | `.lua` |
| `zig` | — | `.zig` (directory builds may delegate build.zig) |
| `cargo` | — | Exact `Cargo.toml` basename |

Every single-file run uses `b breeze exec` or `b breeze instance`. The file suffix or
exact manifest basename selects its adapter; there is no adapter-name run
shorthand. `b run` is an alias for both forms.

## Discover adapters and inspect tools

```sh
b adapters
b languages          # compatibility alias, identical output
b doctor
b doctor lua
```

Doctor searches PATH or named executable overrides without executing tools.
It does not prove versions, SDKs or board cores. The full report permits absent
optional tools; selecting a missing or unknown adapter returns an error.

## Go, Lua, Zig and Cargo

```sh
b breeze exec ./main.go -- argument
b breeze exec ./main.go
b build go ./go-project
b breeze instance ./script.lua -- argument
b build lua ./scripts
b breeze exec ./main.zig
b build zig ./zig-project
b build cargo ./rust-project
b breeze exec ./rust-project/Cargo.toml -- argument
b breeze exec ./rust-project/Cargo.toml
```

Go directory builds use native modules; single-file runs compile only that file.
Instance returns go run's wrapper status; exec returns the native program status.
`GOTOOLCHAIN=local GOPROXY=off` forbids toolchain/dependency downloads; b does not
create go.mod or invoke go get. Non-main packages may produce archives.
Lua build parses each source without running it; both run modes use Lua.
Zig both run modes compile; a directory delegates build.zig or selects main.zig
or exactly one root. Project build returns an external install prefix, not a
guessed executable; build.zig owns its steps and dependency policy.
Cargo uses `--offline` and an external target directory; native default-run
selects binaries, ambiguity rejects. Exec builds before cargo run; instance
delegates cargo run (which may compile). Cargo may write a lockfile and execute
trusted build scripts. No automatic dependency installation is offered.

## Breeze

`exec` compiles first where supported, then runs only if compilation succeeds.
`instance` runs through the source runtime. Both can launch an existing native
executable. C, C++ and Objective-C reject source instance mode. Script adapters
use their runtimes in both modes; this does not produce standalone binaries.

Named projects keep a per-user name-to-root record under `B_HOME/projects/`.
For example, on macOS the default location is
`~/Library/Application Support/b/projects/`. `--in <name>` resolves relative
file paths from that root and makes it the run working directory. Without
`--in`, file paths retain their normal caller-relative behavior.
`b refresh <name>` rescans the registered root and replaces
`projects/<name>/index.tsv` with files recognized by the adapter registry. It
skips common dependency/cache directories and does not build or modify sources.

```sh
b breeze exec ./hello.c -- "Hello World"
b run exec ./hello.c -- "Hello World" # alias for breeze
b project demo ~/work/demo
b breeze exec src/main.c --in demo -- "Hello World"
b breeze exec ./hello.cpp
b breeze exec ./hello.m
b breeze exec ./hello.swift
b breeze exec ./Hello.java
b breeze exec ./main.rs
b breeze exec ./hello.cs
b breeze instance ./already-built-program -- input.txt

b breeze instance ./app.py -- input.csv
b breeze instance ./app.mjs -- "argument with spaces"
b breeze instance ./app.ts
b breeze instance ./hello.php
b breeze instance ./analysis.R
b breeze instance ./script.sh
b breeze instance ./hello.swift
```

Arguments after `--` are passed literally, without shell interpolation by b.
Quote paths and arguments containing spaces. Program exit codes propagate.
Native Node TypeScript supports erasable types, **not type-checking** or arbitrary
TypeScript transforms. C# instance delegates to .NET's file runner, which itself
compiles internally.

## Build

An omitted directory means the current directory. Individual adapter build
discovery is top-level; workspace builds and `b refresh` use recursive scans.
Successful builds print their result path; tool diagnostics go to stderr. Native
artifacts belong to b's external state directory; syntax checks return the
checked directory instead of inventing an executable.

```sh
b build c
b build cpp ./native-app
b build swift ./swift-app
b build java ./java-app
b build rust ./rust-app
b build csharp ./csharp-app

# Check without evaluating the programs:
b build javascript ./scripts
b build typescript ./scripts
b build php ./scripts
b build shell ./scripts

# Capture a native artifact and launch it only after a successful build:
program="$(b build c ./native-app)" && b breeze exec "$program"
```

### Existing CMake and npm projects

```sh
b build cmake ./native-project
b build npm ./web-project

b npm ./web-project/package.json
# npm run start; no build first

b breeze exec ./web-project/package.json -- app-argument
# npm run build, then npm run start only if build succeeded
```

CMake returns its build directory, not a guessed executable. npm returns its
project directory; its scripts control outputs and can start servers. b performs
no automatic npm install or missing-script fallback.

## HTML and PostgreSQL

```sh
b breeze instance ./index.html

PGHOST=/path/to/socket PGUSER=your_user \
  B_SQL_DATABASE=disposable_database b breeze instance ./example.sql
```

HTML opens the original local document through the host opener or the executable
named by `B_BROWSER`. No HTTP server or bundling is implicit; launch acceptance
is not rendering proof.

SQL requires `B_SQL_DATABASE`: **no default database** is chosen. Connection
settings belong to libpq's environment. psql disables startup files/password
prompts, stops on errors and wraps ordinary statements in one transaction.
SQL can mutate data; explicit transaction control and psql meta-commands retain
their native semantics. This is **not a sandbox**. Use a disposable database
for testing; b neither creates one nor silently chooses production data.

## Arduino

The sketch's first line supplies the board:

```cpp
// b_build("arduino:avr:uno")
```

```sh
b build arduino ./Blink
b upload arduino ./Blink/Blink.ino --port /dev/cu.YOUR_BOARD
```

Upload compiles first and requires an explicit port. Optional `--fqbn` must match
the source header; it is not a way to override it.

## Box: manifest and portable export

```sh
b create manifest ./box.json
b box ./box.json ./release
```

`create manifest` writes a starter schema-1 JSON form without overwriting an
existing path. The user fills in application metadata (identity, name, version,
authors, publisher, description, license, homepage, icon), the payload entry and
files, the requested formats and the state policy. `box` then parses and
validates that form: unknown or duplicate keys, unsupported `\u` escapes,
missing required fields, absolute paths, `.`/`..` components, a non-`clean` state
policy, a non-empty `targets` array, duplicate export destinations and unknown
format names all reject before anything is copied.

The export root receives the entry under its own name and each declared file at
its declared destination (the file name by default), with permission bits
preserved; the destination is published by a single rename. Symlinks and
non-regular payload entries are refused, an existing destination is never
replaced, and a failure removes the private staging directory, leaving any
previous export untouched.

Only `portable` is implemented. `exe`, `app`, `msi`, `pkg`, `dmg`, `iso`, `zip`
and `tar` are recognised names that reject until their backends exist, and
per-target exports, signing, notarization, installer lifecycle and exported
machine/user state are not implemented. **Build, breeze, box** describes the
direction, not a claim that packaging or IDE language intelligence is complete.

## Ecosystem workspace command tree

Vexgraph owns this separate target-based graph in `../../tools/workspace.c`, not the
standalone b checkout. Its project launcher uses generic `b breeze exec`; a
standalone clone contains no ecosystem graph. Global options precede the command:

```text
./tools/b [--release] [-j N] [-v] <command>
├── build [targets...]       all targets, or named targets and dependencies
├── run [target] [args...]   run a target; omitted target lists runnable choices
├── test [substring]        all registered tests, or matching names
├── check                   owner/public-surface coverage gates
├── coverage [substring]    instrumented test execution and function gates
├── list | ls               runnable targets
├── targets                 all targets, including libraries
├── ide                     JSON build metadata for IDE adapters
├── watch
├── cc
├── clean
└── doctor
```

The outer Vexgraph workspace has a separate target engine and launcher. Its
launcher still refers to the previous B checkout layout and needs a separate
path migration; it is not a command in this standalone CLI.

Angle-bracket values are placeholders, not literal shell arguments. See the
[README](../README.md) for adapter limitations and the
[JetBrains tutorial](JETBRAINS.md) for editor integration.
