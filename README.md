<p align="center">
  <img src="https://raw.githubusercontent.com/vex-graph/vex-graph/main/resources/b.png" alt="build, breeze, box!" width="800">
</p>

# b

Language-agnostic C23 build orchestrator: **build, breeze, box.**

```bash
./b.sh --help
b generate
b build
b run exec <file>
```

## About

`b` exists because a build file you have to edit is a build file you have to maintain. In `b`, the directory is the build file: each unit owns the sources directly inside it, include/link edges are derived from `#include` paths, and adding a file requires editing no target list or re-running a generator.

`b` is not a compiler, package manager, or replacement for [nob](https://github.com/tsoding/nob.h). It finds installed compilers, runtimes, and project tools, runs them, and forwards their exact exit status.

## Commands

```text
b generate [<directory>]              claim a territory; write .b/workspace.json
b build [<unit>...] [--explain]       build units (default: all descendants)
b run exec <file> [arguments...]      build the artifact, then execute it
b run instance <file> [arguments...]  hand the source to its runtime
b breeze exec|instance <file>         alias for run
b assembly [<unit>]                   author <unit>/b.assembly.json once
b box <main> [--target <target>]      package a unit's assembly
b adapter list                        list adapters, selectors, and requirements
b doctor                              check presence of installed tools
```

## Features

- **Territories and units**: `b generate` creates `.b/workspace.json` as disposable fact metadata (nested territories are refused). Each directory is a unit classified as `app` (executable), `library` (`lib<name>.a` / `.lib`), or `inert`.
- **Tiered downward edges**: Units follow ecosystem tiers (R0–R4 libraries, R5–R6 apps, tests above). Upward or same-tier edges are rejected with both tiers named.
- **Assembly and portable boxing**: `b assembly` authors `<unit>/b.assembly.json` once; `b box` exports the declared `portable` payload atomically via rename without overwriting existing outputs.
- **21 language adapters + 3 project backends**:

| Language / workflow | Selector | Behaviour |
| :--- | :--- | :--- |
| Arduino | `arduino` | Sketch build and explicit compile-before-upload |
| C | `c` | C23 native exec and unit build |
| C# | `csharp` | .NET SDK 10+ file-based run/build |
| C++ | `cpp`, `cxx`, `c++` | C++23 native exec and unit build |
| GLSL / SPIR-V | `glsl` | Shader compilation to `.spv` (`shaderc` or `glslang`) |
| Go | `go` | Package build, source runtime, or native exec |
| HTML | `html`, `web` | Local browser launch |
| Java | `java` | Source runtime, compiled exec, and unit build |
| JavaScript | `javascript`, `node`, `js` | Node runtime and syntax checks |
| Lua | `lua` | CLI runtime and parse-only checks |
| Metal | `metal` | Apple `xcrun metal` to AIR / `metallib` |
| Objective-C | `objc` | macOS Foundation/ARC native builds |
| PHP | `php` | CLI runtime and syntax checks |
| POSIX shell | `shell` | `/bin/sh` runtime and syntax checks |
| Python | `python` | Interpreter run and bytecode syntax checks |
| R | `r`, `R` | `Rscript` runtime and parse-only checks |
| Rust | `rust` | Native exec (`main.rs` or single crate root) |
| SQL | `sql` | PostgreSQL execution (requires `B_SQL_DATABASE`) |
| Swift | `swift` | Script runtime or native build |
| TypeScript | `typescript`, `ts`, `node-ts` | Native Node type stripping |
| Zig | `zig` | Native source build or `build.zig` delegation |

Project backends: **Cargo** (`cargo`), **CMake** (`cmake`), **npm** (`npm`).

## Build and install

Install the latest release (macOS/Linux into `~/.local/bin`, Windows into `%LOCALAPPDATA%\b\bin`; set `B_VERSION` for a specific tag):

```bash
curl -fsSL https://vex-graph.github.io/b/downloads/latest/install.sh | sh
```

```powershell
irm https://vex-graph.github.io/b/downloads/latest/install.ps1 | iex
```

Build from source (`b.sh` bootstraps into `B_HOME`, defaulting to `~/Library/Application Support/b` on macOS, with `-Wall -Wextra -Werror`):

```bash
git clone https://github.com/vex-graph/b.git
cd b
./b.sh --help
./b.sh build .
```

## Documentation

- [`docs/ADAPTERS.md`](docs/ADAPTERS.md), Adapter selectors, behaviour, and limits.
- [`docs/JETBRAINS.md`](docs/JETBRAINS.md), External Tools setup for JetBrains IDEs.
- [`docs/WORKSPACE.md`](docs/WORKSPACE.md), Territory, unit, assembly, and packaging spec.

## Future

- Complete downward linking between units, `exec`/`instance` routing under `run`, default unit manifests, and tier refusal across all adapters.
- Add platform bundle exporters (`.app`, `.msi`, `.pkg`, `.dmg`, `.iso`), ZIP/TAR packaging, icons, and signing.
- Update older tests referencing pre-flatten paths.

## Scope and Limitations

- **Portable boxing only**: Platform bundles, ZIP/TAR archives, signing, and cross-compilation are not implemented.
- **C libraries only**: C library units archive to `lib<unit>.a`; other language adapters currently skip library units.
- **No `b test` verb**: Test orchestration belongs to external scripts or CI; `b` compiles and runs units.
- **Not a sandbox**: Native scripts and compiler includes run with user privileges; `b` never installs tools or requests `sudo`.
