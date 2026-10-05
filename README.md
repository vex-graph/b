# b

b is a general-purpose, language-agnostic build system written in C23: a small
command suite for running files and handing builds to their native toolchains.
C is the implementation language, not a restriction on what b can run.

b isn't trying to be the next big build system or a replacement for Tsoding's
[nob](https://github.com/tsoding/nob.h). It isn't a new compiler, package manager,
or language. The aim is simpler: use the same small command vocabulary while
letting each language's existing tools do the work.

## What it can do

```text
b run <exec|instance> <filename> [-- program arguments...]
b build <language> [directory]
b upload arduino <sketch> --fqbn <board> --port <port>
b <language> <filename> [-- program arguments...]
b export <manifestmainfile> <destination> <exe|app|msi|iso|zip>
```

`run instance` runs a file as-is through its runtime, or launches an existing
native executable. It does not package an app. `run exec` builds a runnable
source artifact first, then launches it for compiled languages. Python and R
use their interpreters in both modes; there is no standalone binary build here.
C# instance uses .NET's file runner, which itself compiles internally.
C and Rust have no source runtime here and reject instance mode.

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
b upload arduino ./Blink/Blink.ino --fqbn arduino:avr:uno --port /dev/cu.YOUR_BOARD
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

### Arduino sketches

`upload` compiles first and flashes only after compilation succeeds. Supply both
the fully qualified board name (FQBN) and the actual port explicitly; b never
guesses a connected board. Find them using `arduino-cli board list` and
`arduino-cli board listall`. For an Uno, the FQBN is `arduino:avr:uno`.
Uploading replaces the program on the board.

For compile-only work, set `ARDUINO_FQBN` and use `b build arduino ./Blink`:

```sh
ARDUINO_FQBN=arduino:avr:uno b build arduino ./Blink
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
executable, not a shell command with flags. Outputs and bootstrap binaries stay
under `~/Library/Application Support/b` on macOS, or `$XDG_CACHE_HOME/b`
(`~/.cache/b` by default) elsewhere. `B_HOME` overrides this location. Toolchain
own caches may be separate; .NET file-based intermediates use its temporary cache.

For optional tools, follow the official [Rust](https://www.rust-lang.org/tools/install),
[.NET SDK](https://dotnet.microsoft.com/download), or [R](https://cran.r-project.org/)
installation instructions. Check `rustc --version`, `dotnet --list-sdks`, or
`Rscript --version` in the environment where b will run.

### JetBrains IDEs

See [JETBRAINS.md](JETBRAINS.md) for a plain-English guide to adding b as an
external tool in CLion, IntelliJ IDEA, Rider, PyCharm, and other JetBrains IDEs.
It uses the current editor file, not a hardcoded project or language list.

### Layout and limits

`b.c` is the suite: argument validation, registry dispatch and existing-executable
fallback. Shared helpers live in `b.h`/`util.c`. Adding a language is one file pair
under `languages/` and a registry entry in `languages/language.c`; adapter
selection comes from the filename extension. Language shorthand should match
that extension. Builds currently recompile rather than providing a shared cache.

`workspace.c` is a compatibility adapter, not b's general project model. It
preserves an existing workspace graph, shaders, tests and target launcher:
`b workspace /path/to/workspace build`. It is separate from `b build c`.

No export format or manifest schema is implemented yet. `export` rejects
nonzero without creating a destination. Packaging, cross-compilation, shared
dependency graphs, C++, JavaScript/TypeScript and HTML/web adapters are future
work, not advertised runtime support. b does not replace Maven, Gradle or Cargo.

Tests live in the independent shared `tests/b/` checkout, not in production
source. In the workspace, run:

```sh
python3 ../tests/b/cli_test.py
python3 ../tests/b/readme_test.py
```

They use temporary projects and an isolated `B_HOME`. Missing optional runtimes
are explicit skips; a pass on macOS is not proof on another platform.

Architecture follows the canonical
[preferences.md](https://github.com/vexgraph-ecosystem/vexspoke/blob/main/preferences.md)
(workspace path `../ecosystem/vexspoke/preferences.md`).
