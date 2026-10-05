# b — command tree and examples

[Back to README](README.md) · [JetBrains setup](JETBRAINS.md)

b orchestrates installed tools; it does not replace their project configuration.
Examples assume `b` is on your PATH. From the b checkout, use `./b` instead.
Install the required toolchain before using its adapter.

## Standalone command tree

```text
b
├── help | --help                         (no command also prints help)
├── build <language-or-backend> [directory]
│   ├── native: c · cpp · objc · swift · java · rust · csharp
│   ├── syntax/parse checks: python · r · javascript · typescript · php · shell
│   ├── existing projects: cmake · npm
│   └── firmware compilation: arduino
├── run
│   ├── exec <file> [-- arguments...]
│   └── instance <file> [-- arguments...]
├── <language> <file> [-- arguments...]     (instance shorthand)
├── upload
│   └── arduino <sketch> --port <port> [--fqbn <matching-board>]
├── export <manifestmainfile> <destination> <exe|app|msi|iso|zip>
│   └── PLANNED — not implemented; rejects without creating a destination
└── workspace <workspace-directory> <command> [arguments...]
    └── separate ecosystem target engine (see below)
```

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
| `cmake` | — | Existing `CMakeLists.txt` project (build only) |

Run dispatch uses the **file suffix**, including for language shorthand; a CLI
name does not force a different compiler onto an incompatible file.

## Run: breeze

`exec` compiles first where supported, then runs only if compilation succeeds.
`instance` runs through the source runtime. Both can launch an existing native
executable. C, C++ and Objective-C reject source instance mode. Script adapters
use their runtimes in both modes; this does not produce standalone binaries.

```sh
b run exec ./hello.c -- "Hello World"
b run exec ./hello.cpp
b run exec ./hello.m
b run exec ./hello.swift
b run exec ./Hello.java
b run exec ./main.rs
b run exec ./hello.cs
b run instance ./already-built-program -- input.txt

b python ./app.py -- input.csv
b node ./app.mjs -- "argument with spaces"
b typescript ./app.ts
b php ./hello.php
b r ./analysis.R
b shell ./script.sh
b swift ./hello.swift
```

Arguments after `--` are passed literally, without shell interpolation by b.
Quote paths and arguments containing spaces. Program exit codes propagate.
Native Node TypeScript supports erasable types, **not type-checking** or arbitrary
TypeScript transforms. C# instance delegates to .NET's file runner, which itself
compiles internally.

## Build

An omitted directory means the current directory. Source discovery is top-level,
not recursive. Successful builds print their result path; tool diagnostics go
to stderr. Native artifacts belong to b's external state directory; syntax
checks return the checked directory instead of inventing an executable.

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
program="$(b build c ./native-app)" && b run exec "$program"
```

### Existing CMake and npm projects

```sh
b build cmake ./native-project
b build npm ./web-project

b npm ./web-project/package.json
# npm run start; no build first

b run exec ./web-project/package.json -- app-argument
# npm run build, then npm run start only if build succeeded
```

CMake returns its build directory, not a guessed executable. npm returns its
project directory; its scripts control outputs and can start servers. b performs
no automatic npm install or missing-script fallback.

## HTML and PostgreSQL

```sh
b html ./index.html

PGHOST=/path/to/socket PGUSER=your_user \
  B_SQL_DATABASE=disposable_database b sql ./example.sql
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

## Export: box (planned)

```text
b export <manifestmainfile> <destination> <exe|app|msi|iso|zip>
```

The command currently rejects. No manifest schema, packaging backend or export
format is implemented. **Build, breeze, box** describes the direction, not a
claim that packaging or IDE language intelligence already exists.

## Ecosystem workspace command tree

This is a separate target-based engine, not the standalone language grammar.
It needs the configured ecosystem workspace; a standalone clone does not supply
that workspace. Global options precede the command:

```text
b workspace <workspace-directory> [--release] [-j N] [-v] <command>
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

Within vexgraph, `./tools/b` forwards to this engine:

```sh
./tools/b targets
./tools/b build <target>
./tools/b run <target>
./tools/b test <substring>
./tools/b doctor

# Equivalent explicit entry point:
b workspace /path/to/vexgraph targets
```

Angle-bracket values are placeholders, not literal shell arguments. See the
[README](README.md) for adapter limitations and the
[JetBrains tutorial](JETBRAINS.md) for editor integration.
