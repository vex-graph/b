<p align="center">
  <img src="https://raw.githubusercontent.com/vex-graph/vex-graph/main/resources/b.png" alt="build, breeze, box!" width="800">
</p>

# b

b is a general-purpose, language-agnostic build system written in C23: an
orchestrator on top of existing compilers, runtimes and project build tools.
C is its implementation language, not a restriction on what it builds.

It exists because a build file you have to edit is a build file you have to
maintain. In b the directory is the build file: a unit owns the sources sitting
in it, so adding a file changes no list anywhere and no generator has to be
re-run. CMake asks you to name every source; b asks you to put it somewhere.

b is not a new compiler, package manager or language, and not a replacement for
Tsoding's [nob](https://github.com/tsoding/nob.h). It does not compile or
interpret anything itself. It decides what to build, finds the installed tool
that builds it, runs that tool, and reports what came back. **Build, breeze,
box.**

## Status

The contract below is the target, and the gap is stated instead of implied.

Working today: `b generate` and `.b/workspace.json`, `b assembly`, per-file and
per-directory adapter builds, `b box` exporting a declared payload as a portable
directory, and `b adapter list`.

In progress: downward linking between units, the `exec` / `instance`
argument under `run`, the default manifest derived from a unit, and tier refusal.
C library units now archive to `lib<unit>.a`; every other adapter still reports a
library unit as skipped.

Not implemented: platform bundles (`.app`, `.msi`, `.pkg`, `.dmg`, `.iso`), ZIP
and TAR export, icons, signing, and cross-compilation. None of them are claimed.

The tree is mid-move, so treat this paragraph as the honest line: the CLI does
not build itself at this commit until the include paths follow `adapters/lang/`.

## The command set

```text
b generate [<directory>]            claim a territory; write .b/workspace.json
b build [<unit>...] [--explain]     build units; no argument builds descendants
b run exec <file> [arguments...]    build the artifact, then execute it
b run instance <file> [arguments...]  hand the source to its runtime
b breeze exec|instance <file>       the same two, second spelling
b assembly [<unit>]                 author <unit>/b.assembly.json once
b box <main> [--target <target>]    package a unit's assembly
b adapter list                      names, selectors, requirements
```

`run` and `breeze` are one behaviour with two spellings. A file is the only
argument that matters; a registered project name is not a concept b has.

## workspace

`.b/` is a territory marker, not a cache. Its presence means every file beneath
it is runnable through b. `b generate` is the only act that claims ground;
nothing else creates a territory as a side effect, and nothing walks a home
directory unless someone typed `generate` there.

Inside a territory there is exactly one generated fact file:

```text
.b/workspace.json     the facts: the root, then each directory and its files
```

It is disposable. Regenerating replaces it; no consumer reads it as intent.
There is no second graph file, no committed target list, and no generated file
that a human edits.

A unit is a directory, and owns the sources sitting directly in it.
Subdirectories are separate units, never merged upward.

```text
src/lang/vec3/        one unit: vec3.c, vec3d.c
src/lang/             not a unit; a container
```

Nested territories are refused, so a file has exactly one owner and one answer
to "what builds this".

## build

Each unit classifies itself from what it contains. An adapter answers the
question; the core never learns a language.

| finding | kind | `b build` produces | `b box` |
| --- | --- | --- | --- |
| an entry point | `app` | an executable | the packaged executable |
| sources, no entry | `library` | an archive the tiers above consume | the archive |
| nothing linkable | `inert` | nothing | refuses |

`b build` with no argument builds every descendant unit under the current
directory and prints one summary line; `--explain` names the inert units instead
of counting them. A build failure is reported and the remaining units still run;
the process exits nonzero if any failed.

Edges point downward only. Include and link edges are derived, not declared: for
each `#include`, walk up to the first ancestor where the path exists, and that
ancestor is an include root. When a unit includes a header owned by another unit,
the dependency is that unit's artifact. The include is the declaration.

Units carry the ecosystem's tiers (R0 lifetime through R6 superapps). A unit at
R0-R4 is a library; R5-R6 is an application; a test is an application at the top
of the stack it verifies. An edge that points at or above its own tier is
refused with both tiers named.

Library artifact spellings follow the host, never the declaration:

| kind | darwin | linux | windows |
| --- | --- | --- | --- |
| `static` | `lib<name>.a` | `lib<name>.a` | `<name>.lib` |
| `shared` | `lib<name>.dylib` | `lib<name>.so` | `<name>.dll` |
| `objects` | `<source>.o` | `<source>.o` | `<source>.obj` |

## breeze and run

`breeze` and `run` are the same verb. Both resolve the file's unit, build the
artifact, and execute it; a build failure never launches a stale binary.

`exec` compiles first and runs the artifact. `instance` hands the source to the
runtime where one exists. Compiled languages reject `instance` because there is
no source runtime; script adapters use their runtime in both modes.

Arguments after the file are forwarded literally, and the program's exit status
is preserved. b adds no shell, no environment rewriting and no wrapper process
that could change what the program sees.

## box

`box` packages what `build` produced, using the unit's assembly:

```text
<unit>/b.assembly.json                  the portable declaration
<unit>/b.assembly.darwin-arm64.json     exactly that target
<unit>/b.assembly.windows-x86_64.json   exactly that target
```

The assembly is authored once by `b assembly` and never regenerated. It states
identity, version, type, payload, targets, formats and state policy; every key is
present, so the file is a complete statement of intent rather than a stub.

The version is the author's counter and b never derives it. `box` refuses
without one; `run` and `breeze` do not need one. Prerelease and build metadata
survive in the manifest even where a platform has no numeric slot for them.

Only the `portable` format is implemented: the entry under its own name plus each
declared file at its declared destination, permissions preserved, published with
a single rename. A destination that already exists is never replaced, and a
failed export leaves any previous export untouched.

b does not cross-compile. A box the host cannot produce is refused with the files
it would have written, and nothing is created. Authoring an assembly for another
target is always allowed; boxing it there is not.

## adapter

An adapter is the edge between b and one installed tool: a language, a project
backend (Cargo, CMake, npm), a firmware flow (Arduino), or a shader compiler
(GLSL, Metal). It declares its selectors, its build, its run mode, and its entry
detection. Adding a language is a new file pair in `adapters/`; it changes no
core code and no command.

Adapters never install tools and never download anything. A missing tool is an
explicit skip or a named failure, never an automatic fetch. `b adapter list`
reports names, selectors and requirements; `b doctor` checks presence, not
version and not a successful build.

## Languages

Alphabetical by displayed name. Arduino is a firmware workflow and HTML a
document launcher; GLSL / SPIR-V and Metal are build-only shader adapters.

| Language / workflow | Selector | Behaviour |
| --- | --- | --- |
| Arduino | `arduino` | Sketch build and explicit compile-before-upload |
| C | `c` | C23 native exec and unit build |
| C# | `csharp` | .NET SDK 10+ file-based run/build; managed output |
| C++ | `cpp`, `cxx`, `c++` | C++23 native exec and unit build |
| GLSL / SPIR-V | `glsl` | `.vert`/`.frag`/`.comp`/`.geom`/`.tesc`/`.tese`/`.glsl` to `.spv`; shaderc default or explicit glslang |
| Go | `go` | Package build, source runtime or native exec |
| HTML | `html`, `web` | Local browser launch, not rendering proof |
| Java | `java` | Source runtime, compiled exec and unit build |
| JavaScript | `javascript`, `node`, `js` | Node runtime and syntax checks |
| Lua | `lua` | CLI runtime and parse-only checks |
| Metal | `metal` | Apple `xcrun metal` to AIR to `metallib`; installed tools required |
| Objective-C | `objc` | macOS Foundation/ARC native builds |
| PHP | `php` | CLI runtime and syntax checks |
| POSIX shell | `shell` | `/bin/sh` runtime and syntax checks |
| Python | `python` | Interpreter run and bytecode syntax checks |
| R | `r`, `R` | Rscript runtime and parse-only checks |
| Rust | `rust` | Native exec; `main.rs` or exactly one crate root |
| SQL | `sql` | Explicit PostgreSQL execution; requires `B_SQL_DATABASE` |
| Swift | `swift` | Script runtime or native build |
| TypeScript | `typescript`, `ts`, `node-ts` | Native Node type stripping, not type-checking |
| Zig | `zig` | Native source build or `build.zig` delegation |

Project backends, also alphabetical: **Cargo** (`cargo`), **CMake** (`cmake`),
**npm** (`npm`). They keep their native project metadata and toolchain policy.

## Install with curl

The latest successful build for `main` is published as public files on GitHub
Pages. Before the first deploy, set the repository's **Settings > Pages > Build
and deployment > Source** to **GitHub Actions**. On macOS or Linux:

```sh
curl -fsSL https://vex-graph.github.io/b/downloads/latest/install.sh | sh
```

On Windows PowerShell:

```powershell
irm https://vex-graph.github.io/b/downloads/latest/install.ps1 | iex
```

The installer selects the matching platform archive, verifies its SHA-256
checksum, installs the launcher and CLI, and records the installation. Unix
installs go to `~/.local/bin`; Windows installs go to `%LOCALAPPDATA%\b\bin`.

The supported targets are macOS arm64 and x86_64, Linux arm64 and x86_64, and
Windows arm64 and x86_64. CI updates `downloads/latest` only after all six
platform builds pass. If `B_VERSION` is set, the installer selects that tag from
GitHub Releases instead of the latest CI build.

To work on b itself:

```sh
git clone https://github.com/vex-graph/b.git
cd b.sh
./b.sh --help
```

`B_HOME` selects external build state; the macOS default is
`~/Library/Application Support/b`.

## Building b itself

`b.sh` bootstraps the CLI into `B_HOME` and recompiles only when an input is
newer than the binary. It probes the compiler for C23 once, falling back through
`gnu2x` to `gnu17`, and builds with `-Wall -Wextra -Werror`.

There is no CMake, no nested build file and no generator to run first. b builds
itself the same way it builds anything else: from the directory.

## Scope and Limitations

b orchestrates installed tools and does not replace their project configuration.
Cargo, npm, CMake, Maven and Gradle keep their own dependency graphs and
incremental behaviour.

One process is one unit at a time; `b build` walks descendants, it does not
invent linkage between unrelated directories. Mixed C-family sources in one unit
reject rather than guess. Includes and headers are dependencies, not independently
runnable programs.

This is not a sandbox and not a security boundary. Native scripts and compiler
includes can read and write outside the territory; requiring a stable tree during
a build is not confinement against a hostile build script. b requests no sudo.

No implicit web server, no cross-compilation framework, no universal shared
cache, and no automatic tool installation. SQL requires `B_SQL_DATABASE` and has
no default database. Firmware upload changes the board. Shader compilation is not
GPU rendering evidence. HTML launch is not rendering proof.

A test is a unit with an entry point. There is no `b test` verb: running every
matching test, collecting exit codes and deciding what counts as a suite is
orchestration, and it belongs to whoever is orchestrating (a shell script, CI),
not to the tool that compiles.

## Documentation

- [docs/ADAPTERS.md](docs/ADAPTERS.md) - tools, selectors, build and run behaviour, and limitations.
- [docs/JETBRAINS.md](docs/JETBRAINS.md) - External Tools setup for CLion, IntelliJ IDEA, Rider, PyCharm and similar IDEs.
- [docs/WORKSPACE.md](docs/WORKSPACE.md) - the territory, unit, assembly and packaging specification.
- [preferences](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a) - the architecture laws b is built against.
