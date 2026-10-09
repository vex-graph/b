<p align="center">
  <img src="https://raw.githubusercontent.com/vex-graph/vex-graph/main/resources/b.png" alt="build, breeze, box!" width="800">
</p>

# b — adapters

[Back to README](README.md) · [Command tree](TREE.md) · [JetBrains setup](JETBRAINS.md)

This reference describes native-tool delegation, file selectors and adapter
limits. The README's language list is alphabetical; the detailed walkthroughs
below group related workflows. GLSL and Metal now have standalone build-only
adapters; the existing Vexgraph shader generator remains project-owned.

## Shared command behavior

See the **[command tree and examples](TREE.md)** for the complete command map,
adapter names, run/build behavior, and separate workspace commands.

```text
b run <exec|instance> <filename> [-- program arguments...]
b adapters
b languages   # compatibility alias for adapters
b doctor [adapter]
b build <adapter> [directory]
b build workspace <directory> [--plan|--confirm]
b upload arduino <sketch> --port <port> [--fqbn <matching-board>]
b <adapter> <filename> [-- program arguments...]
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

Arguments are preserved; runtime wrappers may affect exit codes as noted below.
Paths with spaces work when
quoted. An omitted build directory means the current directory. Source discovery
is top-level, not recursive. Builds print their result path; compiler diagnostics
go to stderr. Failed compilation never launches an older artifact.

## Adapter inventory

`Adapter` is the integration contract, not a claim that every entry is a
programming language: CMake/npm/Cargo are project backends, HTML launches a
document, and Arduino targets firmware deployment. Implementations live in
`adapters`. `b adapters` lists file selectors, capabilities and required tools;
`b languages` is its compatibility alias. `b doctor [adapter]` checks executable
presence on PATH or adapter overrides without running tools. It does **not**
verify versions, SDKs, board cores or successful builds. Missing optional tools
do not fail the unfiltered report; a selected missing/unknown adapter does.

This list is a snapshot, not a ceiling. Install only the tools you need; b never
downloads a toolchain automatically.

| Adapter / CLI name | Required tool | What works today |
| --- | --- | --- |
| Arduino / `arduino` | Arduino CLI and installed board core | Sketch compilation and explicit compile-before-upload |
| C / `c` | `cc` or `CC` | Single-file exec; directory build into one executable with `main()` |
| C# / `csharp` | .NET SDK 10+ (`dotnet`) | File-based `.cs` instance/exec; directory build with exactly one `.cs` entry |
| C++ / `cpp` (`cxx`, `c++`) | `c++` or `CXX` | C++23 exec and directory builds for `.cpp`/`.cc`/`.cxx` |
| Cargo / `cargo` | `cargo` or `CARGO`, `rustc` | Existing Cargo.toml builds/runs offline; external target directory, no guessed executable |
| CMake / `cmake` | `cmake` | Existing project configure/build; run the chosen artifact explicitly |
| GLSL / SPIR-V shaders / `glsl` | shaderc `glslc` (default), explicit glslang | Stage files and staged `.glsl` compile to `.spv`; no host execution |
| Go / `go` | `go` or `GO` | Package-directory build; single-file `go run` instance or compiled exec |
| HTML / `html` (`web`) | Default host browser opener or `B_BROWSER` | `.html`/`.htm` file launch; no guessed HTML build |
| Java / `java` | JDK (`java`, `javac`) | Source instance; compiled exec; top-level directory build |
| JavaScript / `javascript` (`node`, `js`) | `node` | `.js`/`.mjs`/`.cjs` runs and directory syntax checks |
| Lua / `lua` | `lua`/`luac` or `LUA`/`LUAC` | Script runtime in both modes; parse-only directory check with `luac -p` |
| Metal shaders / `metal` | Apple `xcrun metal`, `metallib` | Separate library per `.metal`; build-only, macOS |
| npm / `npm` | `npm`, `node` | Declared build/start scripts; no automatic install |
| Objective-C / `objc` | Clang + Foundation (macOS) | `.m` exec/directory build with ARC; no source instance |
| PHP / `php` | PHP CLI | Script runs; top-level `php -l` checks; no automatic web service |
| POSIX shell / `shell` | `/bin/sh` | `.sh` runs; top-level `sh -n` syntax checks |
| Python / `python` | `python3` | Interpreter runs; directory bytecode syntax check |
| R / `r` (also `R`) | `Rscript` | `.R`/`.r` interpreter runs; parse-only directory check |
| Rust / `rust` | `rustc` | Single-file exec; directory build with `main.rs` or one `.rs` crate root |
| SQL / `sql` | PostgreSQL `psql` | Explicit-database `.sql` execution; no fake standalone compilation |
| Swift / `swift` | `swift`, `swiftc` | Script instance; native exec; top-level directory build |
| TypeScript / `typescript` (`ts`, `node-ts`) | Recent Node with native type stripping | `.ts`/`.mts`/`.cts` runs and directory parse/strip checks, **not type-checking** |
| Zig / `zig` | `zig` or `ZIG` | Both source modes compile; directory uses build.zig, main.zig or exactly one source root |

Project backends are adapters too: `b build cmake` delegates to CMake, and
`b build npm` delegates to the project's npm `build` script. They do not replace
the native project metadata or package managers.

Go retains native module/toolchain policy; b does not create modules or invoke
`go get`. Set `GOTOOLCHAIN=local GOPROXY=off` for offline work. A non-main package
may build an archive, not an executable. Cargo uses `--offline`, may write its
lockfile and execute native build scripts; uncached dependencies reject.
Multiple binaries require native `default-run` configuration. Zig project
steps/dependency fetching remain build.zig policy. Lua is CLI Lua, not Luau or
an engine-specific embedded dialect. New adapters have macOS-only test scope.

Go instance preserves `go run`'s wrapper status (typically 1 for a failed
program); Go exec preserves the native program's exit code.

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
inputs. Cargo projects use the explicit `cargo` adapter; `.csproj` discovery is
not implemented. C# builds a managed
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

### GLSL / SPIR-V shaders

`b build glsl <directory>` compiles top-level `.vert`, `.frag`, `.comp`, `.geom`,
`.tesc`, `.tese` and `.glsl` inputs. shaderc's `glslc` is the default;
`GLSLC` selects one executable. Set `GLSL_BACKEND=glslang` to explicitly use
`glslangValidator` (`GLSLANG_VALIDATOR` override). Unknown backend values reject.
There is no silent compiler fallback or install. Vulkan is the target API,
not the compiler. Both backends target Vulkan 1.0 SPIR-V.

```sh
b build glsl ./shaders
GLSL_BACKEND=glslang b build glsl ./shaders
```

GLSL is the source language; `.vert` (vertex), `.frag` (fragment) and `.comp`
(compute) identify stages, while `.glsl` can be a generic source/include suffix.
SPIR-V (`.spv`) is bytecode, not a host executable. Generic `.glsl` must declare
its stage under the native compiler's contract (shaderc accepts
`#pragma shader_stage(compute)`). Unsupported/unstaged inputs reject through the
compiler. Include-only files should use a different suffix or stay outside the
build directory: b cannot infer that a selected `.glsl` is only a header.
Aliases `glsl-vert`, `glsl-frag`, `glsl-comp`, `glsl-geom`, `glsl-tesc`, `glsl-tese`
provide file selectors; their directory builds use the same complete stage set.
Both run modes reject. Each build uses a fresh external output generation;
failure cleans that generation without replacing older successful outputs.
No shared shader cache or automatic pruning of successful generations exists.

Real glslang tests verify vertex/fragment/compute SPIR-V magic and rejection.
Real shaderc is unproved on this host because glslc is missing; an offline tool
fixture proves argv/failure handling, not compilation. No GPU execution is proved.
Vexgraph's existing `tools/workspace.c::setup_graphvex` still invokes
`glslangValidator -V` for registered quad/compositor sources, hashes generators
and filter IDs, and embeds quad output; it has not migrated to this adapter.

### Metal shaders

`b build metal <directory>` compiles each top-level `.metal` using
`xcrun -sdk macosx metal`, then links its AIR into one `.metallib` per source.
The macOS 14 deployment floor is explicit. `XCRUN` selects one executable.
There is no cross-file library-link inference or host shader execution. Fresh
generations preserve old output on compile/link failure; AIR intermediates are
removed. Non-Apple hosts reject. Missing Apple compiler components are not
downloaded. This host has only two-stage fixture proof: real Metal compilation
is skipped because the installed tool cannot run. Rendering remains unproved.

### Recursive workspace assessment and build

`b build workspace <directory> [--plan|--confirm]` inventories first, then builds.
It references Vexgraph's `tools/workspace.c` approach, not its specific target
graph. The iterative alphabetical directory walk grows without a fixed total
ceiling. Cargo.toml, package.json, CMakeLists.txt and build.zig own their subtrees;
competing manifests reject. Loose sources group by directory/build callback.
Mixed C-family sources need a manifest to specify linkage. Other language and
shader units coexist independently in the same directory.
There is no guessed single-file run, no firmware upload and no SQL execution.
HTML/SQL/headers/unknown loose files are counted as unassigned.

Assessment lists unit paths, adapters and executable requirements; it does not
validate installed tools or native project configuration. `--plan` executes no
tool or output creation. Home/ancestor roots, Downloads/Documents/Desktop and
inventories of at least 1,000 files or 100 units require `--confirm` for building.
These are review thresholds, not size ceilings. Never sudo or automatic installs.
Each completed unit prints percent; failures continue and aggregate nonzero.
An empty inventory reports no buildable units, not a fabricated compilation.

Excluded names: `.git`, `.hg`, `.svn`, `node_modules`, `target`, `build`, `dist`,
`out`, `.cache`, `__pycache__`, `.venv`, `venv`, and the configured B_HOME directory.
Directory symlinks are not followed. Known source/manifest symlinks reject; control
characters in names reject. Keep the filesystem stable during the operation.
Native scripts/includes may escape the tree: this is not a sandbox, a TOCTOU
defence or proof against hostile downloaded projects. Generic dependency/schema
inference, native Windows support and live project-graph migration remain gaps.

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
fallback. Shared helpers live in `b.h`/`util.c`. Adding an adapter is one file pair
under `adapters` and a registry entry in `adapters/adapter.c`; adapter
selection uses a file suffix or exact manifest basename; build-only backends have no source
extension. Each C source/header begins with `;;DEFINITION` and `;;OVERVIEW`
blueprints documenting capabilities, fields and public/private function registries.
The markers compile to zero-runtime assertions in standalone `annotation.h`.
Adapter shorthand should match that file selector. Builds currently delegate
incremental decisions to project backends rather than providing a shared cache.

Project-specific graphs belong to their project, not this checkout. Vexgraph's
`tools/workspace.c` holds its targets, shaders and tests; its `tools/b` launcher
uses generic `b run exec` to compile and execute that graph. There is no special
`b workspace` command or ecosystem dependency in this standalone launcher.

No export format or manifest schema is implemented yet. `export` rejects
nonzero without creating a destination. Packaging, cross-compilation, shared
dependency graphs, additional database dialects and supervised web-serving remain
future work. b does not replace Maven, Gradle, Cargo, npm or CMake.

Tests live in the independent shared `../../tests/b` checkout, not in production
source. In the workspace, run:

```sh
python3 ../../tests/b/cli_test.py
python3 ../../tests/b/readme_test.py
python3 -m unittest discover -s ../../tests/b -p '*_test.py' -v
```

They use temporary projects and an isolated `B_HOME`. Missing optional runtimes
are explicit skips; a pass on macOS is not proof on another platform.
New language tests use real installed tools; browser launching is tested with a
headless opener fixture, not GUI acceptance. SQL uses a temporary socket-only
PostgreSQL cluster, with bounded cleanup, never an existing database.

Architecture follows the canonical
[preferences.md](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a)
(one real, Git-ignored workspace-root file at `../../preferences.md`).
