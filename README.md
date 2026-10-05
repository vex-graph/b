<p align="center">
  <img src="https://raw.githubusercontent.com/vex-graph/vex-graph/main/resources/b.png" alt="build, breeze, box!" width="800">
</p>

# b

b is a general-purpose, language-agnostic build system written in C23: a small
command suite for running files and handing builds to their native toolchains.
C is the implementation language, not a restriction on what b can run.

b isn't trying to be the next big build system or a replacement for Tsoding's
[nob](https://github.com/tsoding/nob.h). It isn't a new compiler, package manager,
or language. It is an orchestrator on top of existing toolchains and build
systems, giving you another entry point rather than replacing their projects.
The aim is simpler: use the same small command vocabulary while
letting each language's existing tools do the work.

**Build, breeze, box.** Build through the native tools, run without switching
editors, and eventually package the result. b is deliberately experimental:
working capabilities and production gaps are listed separately. The "box"
part is a goal, not a claim that export already works.

## What it can do

See the **[command tree and examples](TREE.md)** for the complete command map,
adapter names, run/build behavior, and separate workspace commands.

```text
b run <exec|instance> <filename> [-- program arguments...]
b build <language> [directory]
b upload arduino <sketch> --port <port> [--fqbn <matching-board>]
b <language> <filename> [-- program arguments...]
b export <manifestmainfile> <destination> <exe|app|msi|iso|zip>
```

`run instance` runs a file as-is through its runtime, or launches an existing
native executable. It does not package an app. `run exec` builds a runnable
source artifact first, then launches it for compiled languages. Python, R,
JavaScript, native-Node TypeScript, PHP and POSIX shell use their runtimes in both
modes; there is no standalone binary build for those adapters here. HTML opens
the original document in a browser in both modes.
C# instance uses .NET's file runner, which itself compiles internally.
C, C++ and Objective-C have no source runtime here and reject instance mode.
Swift can use its script runner for instance or swiftc for exec.

Arguments and program exit codes are preserved. Paths with spaces work when
quoted. An omitted build directory means the current directory. Source discovery
is top-level, not recursive. Builds print their result path; compiler diagnostics
go to stderr. Failed compilation never launches an older artifact.

## Supported languages — for now

This list is a snapshot, not a ceiling. Install only the tools you need; b never
downloads a toolchain automatically.

| Language / CLI name | Required tool | What works today |
| --- | --- | --- |
| C / `c` | `cc` or `CC` | Single-file exec; directory build into one executable with `main()` |
| Java / `java` | JDK (`java`, `javac`) | Source instance; compiled exec; top-level directory build |
| Python / `python` | `python3` | Interpreter runs; directory bytecode syntax check |
| Rust / `rust` | `rustc` | Single-file exec; directory build with `main.rs` or one `.rs` crate root |
| C# / `csharp` | .NET SDK 10+ (`dotnet`) | File-based `.cs` instance/exec; directory build with exactly one `.cs` entry |
| R / `r` (also `R`) | `Rscript` | `.R`/`.r` interpreter runs; parse-only directory check |
| Arduino / `arduino` | Arduino CLI and installed board core | Sketch compilation and explicit compile-before-upload |
| Swift / `swift` | `swift`, `swiftc` | Script instance; native exec; top-level directory build |
| Objective-C / `objc` | Clang + Foundation (macOS) | `.m` exec/directory build with ARC; no source instance |
| C++ / `cpp` (`cxx`, `c++`) | `c++` or `CXX` | C++23 exec and directory builds for `.cpp`/`.cc`/`.cxx` |
| JavaScript / `javascript` (`node`, `js`) | `node` | `.js`/`.mjs`/`.cjs` runs and directory syntax checks |
| TypeScript / `typescript` (`ts`, `node-ts`) | Recent Node with native type stripping | `.ts`/`.mts`/`.cts` runs and directory parse/strip checks, **not type-checking** |
| HTML / `html` (`web`) | Default host browser opener or `B_BROWSER` | `.html`/`.htm` file launch; no guessed HTML build |
| PHP / `php` | PHP CLI | Script runs; top-level `php -l` checks; no automatic web service |
| POSIX shell / `shell` | `/bin/sh` | `.sh` runs; top-level `sh -n` syntax checks |
| SQL / `sql` | PostgreSQL `psql` | Explicit-database `.sql` execution; no fake standalone compilation |

Project backends are adapters too: `b build cmake` delegates to CMake, and
`b build npm` delegates to the project's npm `build` script. They do not replace
the native project metadata or package managers.

```sh
b run exec ./hello.c -- one two
b java ./Hello.java -- world
b python ./app.py
b run exec ./hello.rs
b run exec ./hello.cs -- world
b r ./hello.R -- world
b build c ./native
b build java ./java-src
b build python ./scripts
b build rust ./crate
b build csharp ./csharp-src
b build r ./r-scripts
b upload arduino ./Blink/Blink.ino --port /dev/cu.YOUR_BOARD
b swift ./hello.swift -- world
b run exec ./hello.swift
b run exec ./hello.m
b run exec ./hello.cpp
b node ./hello.js -- world
b typescript ./hello.ts
b html ./index.html
b php ./hello.php
b shell ./script.sh
b build npm ./web-project
b run exec ./web-project/package.json -- program-arguments
```

Java exec assumes a default-package main class matching the filename. Rust
modules are loaded by `mod` from the crate root, not passed as separate compiler
inputs. Cargo and `.csproj` discovery are not implemented. C# builds a managed
`program.dll` requiring `dotnet`, not a self-contained native executable.
.NET may restore dependencies declared in a file and honors surrounding SDK,
NuGet and MSBuild configuration; builds are not sandboxed.

Python `build` uses `py_compile` with `PYTHONPYCACHEPREFIX` outside the source tree.
R `build` parses without evaluating scripts and returns the checked source
directory; it does not create an artifact or install R packages. R runs use
`--vanilla`, so user profiles and saved workspaces are not loaded.

### Native, scripting and web adapters

Swift directory builds follow the compiler's `main.swift` entry conventions;
SwiftPM discovery is not implemented. Objective-C targets Foundation/ARC on
macOS 14+ and does not invent Xcode projects or additional framework links.
Use CMake for projects with their own compiler/linker configuration.

JavaScript runs through Node, respecting modules/imports and package metadata.
Its `build` is syntax-checking, not bundling. Native TypeScript execution needs
recent Node (22.18+ or a current supported release). It supports erasable types,
not arbitrary TypeScript transforms, JSX/TSX, tsconfig aliases, or type-checking.
The experimental Node `stripTypeScriptTypes` API parses build inputs without
evaluating them. Non-erasable syntax may reject. Use your npm script for `tsc`,
a bundler or a framework-specific pipeline.

For npm projects, `b npm ./package.json` or `run instance` runs `npm run start`.
`run exec` runs `npm run build` first, then starts only if building succeeded.
`b build npm <directory>` runs only `build`. Script outputs remain wherever the
project defines them; b returns the project directory rather than guessing a
`dist` layout. Arguments after `--` go to the start script. No `npm install`,
dependency download, or missing-script fallback is performed automatically.
npm scripts themselves can invoke shells, run servers or perform network/I/O.

`b html index.html` opens a local file. `B_BROWSER` may name one browser/opener
executable (not an embedded shell command). Success means launch accepted, not
page rendering, script execution or browser shutdown. There is no implicit HTTP
server. File-URL security restrictions still apply; use a declared npm start
script for pages requiring HTTP, modules, API proxies or a dev server.

PHP uses CLI configuration/extensions and does not start PHP-FPM/Apache. PHP,
JavaScript and shell directory checks do not evaluate the checked programs.
Shell execution means POSIX sh, not automatic Bash/zsh dialect detection.

### PostgreSQL scripts: choose the database explicitly

```sh
PGHOST=/path/to/socket PGPORT=5432 PGUSER=your_user \
  B_SQL_DATABASE=your_disposable_database b sql ./example.sql
```

There is **no default database**. `B_SQL_DATABASE` accepts a database name, not a
connection URI or credentials. Use libpq environment settings such as `PGHOST`,
`PGPORT`, `PGUSER`, `PGPASSFILE` and `PGCONNECT_TIMEOUT` for connection policy.
Both run modes execute the script through psql with startup files disabled,
interactive password prompts disabled, `ON_ERROR_STOP`, and a single transaction
for ordinary statements. Execution can mutate data. Scripts with transaction
control or psql meta-commands retain native semantics: this is not a sandbox or
a promise of atomicity for arbitrary scripts. Use disposable databases to learn
or test; b never creates one or guesses an existing production database.

SQL `build` rejects because database-backed semantics are not a standalone
compiler check. Current SQL proof uses PostgreSQL; other dialects are not
advertised as interchangeable.

### Arduino sketches

Every primary sketch must begin with this first-line header (Uno example):

```cpp
// b_build("arduino:avr:uno")
```

The quoted value is the fully qualified board name (FQBN), not its display name.
Use the board's real FQBN, including any required options; for example a Nano
with the old bootloader uses `arduino:avr:nano:cpu=atmega328old`. Find board names
with `arduino-cli board listall` and the current port with `arduino-cli board list`.
Do not put a blank line or another comment before the header.

`upload` reads the board from that header, compiles first, and flashes only after
compilation succeeds. Supply the actual port explicitly; b never guesses the
connected device. An optional `--fqbn` must match the header exactly or b rejects
before compiling/uploading. Missing, malformed and oversized headers reject
without launching Arduino CLI. Uploading replaces the program on the board.

For compile-only work, the same header supplies the board:

```sh
b build arduino ./Blink
```

A standard sketch folder has a primary `.ino` matching its folder name; b builds
the whole folder, including its tabs. A standalone `.ino` elsewhere is staged
as a correctly named sketch outside your source tree for upload. Only that file
is staged; sibling headers/tabs are not copied. Use a standard sketch folder
for multi-file work. `run exec`/`run instance` cannot run firmware on the host;
use `upload` for hardware.

Set `ARDUINO_CLI` to the CLI executable path if it is not on PATH. On macOS, b
also discovers the CLI bundled at `/Applications/Arduino IDE.app`. Cores and
libraries installed through Arduino IDE's managers remain owned by Arduino;
b does not install or upgrade them. Close Serial Monitor before uploading.
An FTDI-connected board may need a manual RESET as uploading starts. Hardware
upload has been exercised on an Uno; other boards/platforms remain unproved.

## Clone and use

```sh
git clone https://github.com/vex-graph/b.git
cd b
./b --help
```

The launcher compiles the CLI using your installed C23 compiler with
`-Wall -Wextra -Werror`. On macOS it targets Apple Silicon M1/macOS 14+.
Current runtime evidence is macOS only; the process/filesystem implementation
uses POSIX APIs and a native Windows adapter is still needed.

To use b elsewhere, either call `/absolute/path/to/b/b`, or add the checkout to
your shell's `PATH` (replace the example path):

```sh
export PATH="/absolute/path/to/b:$PATH"
cd /path/to/your/project
b run exec ./hello.c
```

The launcher preserves your working directory. `CC` can select one compiler
executable, not a shell command with flags; C++ uses `CXX` similarly. b-owned
compiled outputs and bootstrap binaries stay
under `~/Library/Application Support/b` on macOS, or `$XDG_CACHE_HOME/b`
(`~/.cache/b` by default) elsewhere. `B_HOME` overrides this location. Toolchain
own caches may be separate; .NET file-based intermediates use its temporary cache.
npm scripts keep their own project output layout.

For optional tools, follow the official [Rust](https://www.rust-lang.org/tools/install),
[.NET SDK](https://dotnet.microsoft.com/download), or [R](https://cran.r-project.org/)
installation instructions. Check `rustc --version`, `dotnet --list-sdks`, or
`Rscript --version` in the environment where b will run.

### JetBrains IDEs

See [JETBRAINS.md](JETBRAINS.md) for a plain-English guide to adding b as an
external tool in CLion, IntelliJ IDEA, Rider, PyCharm, and other JetBrains IDEs.
It uses the current editor file, not a hardcoded project or language list.
An IDE may have poor completion, refactoring or debugging for another language.
b supplies tool orchestration, not language intelligence: **build, breeze, box**
does not imply that every JetBrains language plugin suddenly works.

### Layout and limits

Existing CMake projects work through `b build cmake /path/to/project`. b invokes
`cmake -S <source> -B <external-build-directory>`, then `cmake --build` only after
configuration succeeds. CMake retains its targets, dependencies, flags and
incremental decisions. The result path is a build directory; run the chosen
artifact explicitly with `b run exec /path/to/build/program`. b does not guess
which target to launch or run install/tests automatically. Other build backends
can use the same delegation pattern; they are not all implemented yet.

`b.c` is the suite: argument validation, registry dispatch and existing-executable
fallback. Shared helpers live in `b.h`/`util.c`. Adding a language is one file pair
under `languages/` and a registry entry in `languages/language.c`; adapter
selection comes from the filename extension; build-only backends have no source
extension. Each C source/header begins with `;;DEFINITION` and `;;OVERVIEW`
blueprints documenting capabilities, fields and public/private function registries.
The markers compile to zero-runtime assertions in standalone `annotation.h`.
Language shorthand should match
that extension. Builds currently recompile rather than providing a shared cache.

`workspace.c` is a compatibility adapter, not b's general project model. It
preserves an existing workspace graph, shaders, tests and target launcher:
`b workspace /path/to/workspace build`. It is separate from `b build c`.

No export format or manifest schema is implemented yet. `export` rejects
nonzero without creating a destination. Packaging, cross-compilation, shared
dependency graphs, additional database dialects and supervised web-serving remain
future work. b does not replace Maven, Gradle, Cargo, npm or CMake.

Tests live in the independent shared `tests/b/` checkout, not in production
source. In the workspace, run:

```sh
python3 ../tests/b/cli_test.py
python3 ../tests/b/readme_test.py
python3 -m unittest discover -s ../tests/b -p '*_test.py' -v
```

They use temporary projects and an isolated `B_HOME`. Missing optional runtimes
are explicit skips; a pass on macOS is not proof on another platform.
New language tests use real installed tools; browser launching is tested with a
headless opener fixture, not GUI acceptance. SQL uses a temporary socket-only
PostgreSQL cluster, with bounded cleanup, never an existing database.

Architecture follows the canonical
[preferences.md](https://github.com/vexgraph-ecosystem/vexspoke/blob/main/preferences.md)
(workspace path `../ecosystem/vexspoke/preferences.md`).
