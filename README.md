<p align="center">
  <img src="https://raw.githubusercontent.com/vex-graph/vex-graph/main/resources/b.png" alt="build, breeze, box!" width="800">
</p>

# b

b is a general-purpose, language-agnostic build system written in C23: an
orchestrator on top of existing compilers, runtimes and project build tools.
C is its implementation language, not a restriction on what it builds.

**Build, breeze, box.** Build through native tools, run without switching editors,
and expand packaging as its contracts are implemented. b is not a new compiler,
package manager or language, nor a replacement for Tsoding's
[nob](https://github.com/tsoding/nob.h).

## Disclaimer: CMake is just IDE metadata (not irony)

This repository's `CMakeLists.txt` is **IDE metadata only**: source targets,
include paths and C23 flags for navigation, diagnostics and inlay hints. Its
targets are excluded from the default build. It downloads no dependencies and
wires no linked application runner. Missing headers remain real errors.

The `./b` launcher builds b itself. Vexgraph's `./tools/b` builds the workspace.
Neither uses this metadata adapter as its runtime build system. Separately,
`b build cmake` can delegate to an existing project's real CMake build; that
does not change the role of this repository's CMake file.

## Current State

b is working build infrastructure, not merely an experiment. Its standalone
CLI implements native-tool build/run delegation, adapter discovery, tool-presence
diagnostics, recursive workspace assessment/build, GLSL/Metal shader delegation
and explicit Arduino upload. Vexgraph uses it as the entry point to its
project-owned build graph. Export remains unimplemented. Workspace builds are
proved on macOS with scoped C/Python fixtures; glslang has real SPIR-V proof,
shaderc currently has invocation/failure-fixture proof, and Metal currently has
two-stage fixture proof because its installed compiler cannot run on this host.

Current automated proof is scoped to macOS; native Windows support and other
host platforms remain unproven here. See the shared `tests/b` owner suites and
workspace `tests/test-checklist.md` for content-specific evidence. These scoped
owner runs do not establish every toolchain, platform or GPU execution claim.

## What does it do?

```text
b adapters
b languages
b doctor [adapter]
b build <adapter> [directory]
b build workspace <directory> [--plan|--confirm]
b run <exec|instance> <filename> [-- program arguments...]
b <adapter> <filename> [-- program arguments...]
b upload arduino <sketch> --port <port> [--fqbn <matching-board>]
```

`exec` builds a runnable source artifact first for compiled-language adapters;
`instance` uses a source runtime where available. Script adapters use their
runtimes in both modes. C, C++ and Objective-C reject source instance mode;
Zig compiles in both modes. HTML opens the original document, not a built app.
Build failures do not launch stale artifacts. Arguments are forwarded literally;
native tool wrappers can affect the final exit status (notably `go run`).

```sh
git clone https://github.com/vex-graph/b.git
cd b
./b --help
./b run exec ./hello.c -- one two
./b python ./app.py
./b java ./Hello.java -- world
```

Use an absolute launcher path or add this checkout to `PATH` to use b from other
projects. It preserves the caller's working directory. `B_HOME` selects external
build state; the macOS default is `~/Library/Application Support/b`.

## List of languages

Alphabetical by displayed name. Arduino is a firmware workflow and HTML a
document launcher; GLSL / SPIR-V and Metal are build-only shader adapters. These
distinctions matter more than calling every entry a compiler.

| Language / workflow | Selector | Current implementation |
| --- | --- | --- |
| Arduino | `arduino` | Sketch build and explicit compile-before-upload |
| C | `c` | C23 native exec and top-level directory build |
| C# | `csharp` | .NET SDK 10+ file-based run/build; managed output |
| C++ | `cpp`, `cxx`, `c++` | C++23 native exec and directory build |
| GLSL / SPIR-V shaders | `glsl` | `.vert`/`.frag`/`.comp`/`.geom`/`.tesc`/`.tese`/`.glsl` to `.spv`; shaderc default or explicit glslang |
| Go | `go` | Package build, source runtime or native exec |
| HTML | `html`, `web` | Local browser launch, not rendering proof |
| Java | `java` | Source runtime, compiled exec and directory build |
| JavaScript | `javascript`, `node`, `js` | Node runtime and syntax checks |
| Lua | `lua` | CLI runtime and parse-only checks |
| Metal shaders | `metal` | Apple `xcrun metal` → AIR → `metallib`; installed tools required |
| Objective-C | `objc` | macOS Foundation/ARC native builds |
| PHP | `php` | CLI runtime and syntax checks |
| POSIX shell | `shell` | `/bin/sh` runtime and syntax checks |
| Python | `python` | Interpreter run and bytecode syntax checks |
| R | `r`, `R` | Rscript runtime and parse-only checks |
| Rust | `rust` | Native exec; `main.rs` or exactly one crate root |
| SQL | `sql` | Explicit PostgreSQL database execution |
| Swift | `swift` | Script runtime or native build |
| TypeScript | `typescript`, `ts`, `node-ts` | Native Node type stripping, not type-checking |
| Zig | `zig` | Native source build or existing build.zig delegation |

Project backends, also alphabetical: **Cargo** (`cargo`), **CMake** (`cmake`),
**npm** (`npm`). They retain their native project metadata and toolchain policy.

## Tree

See the [command tree and examples](TREE.md) for the full CLI and the separate
Vexgraph workspace commands.

```text
b/
├── b                 bootstrap launcher; builds outside the checkout
├── b.c               command validation and dispatch
├── b.h               shared contracts
├── inspect.c/.h      adapter listing and tool-presence diagnostics
├── util.c            paths, child processes and build helpers (API in b.h)
├── adapters/         native-tool adapter file pairs and registry
├── annotation.h      zero-runtime ;;DEFINITION / ;;OVERVIEW markers
├── CMakeLists.txt    IDE metadata only
├── ADAPTERS.md       adapter behavior and limits
├── JETBRAINS.md      external-tool setup
└── TREE.md           commands and examples
```

## JetBrains IDEs

[JETBRAINS.md](JETBRAINS.md) explains External Tools in CLion, IntelliJ IDEA,
Rider, PyCharm and similar IDEs. Build/run the current file without switching
editors. b does not supply completion, refactoring, language plugins or a
debugger; IDE appearance remains user-verified.

## Adapters

[ADAPTERS.md](ADAPTERS.md) documents tools, selectors, build/run behavior and
limitations, including Arduino board headers and upload, explicit SQL targets,
project backends and **GLSL / SPIR-V shader generation**.

`b adapters` lists the registered standalone adapters. `b languages` is an alias;
`b doctor [adapter]` checks executable presence, not versions or successful builds.
Shader adapters never execute their outputs. Install only the tools you need;
b does not install them. `doctor glsl` checks the default shaderc executable;
it does not check the alternate glslang backend or actual compiler availability.

Recursive builds assess the entire selected tree before invoking tools:

```sh
b build workspace ./project --plan
b build workspace ./project
b build workspace ~/Downloads --confirm
b build glsl ./shaders
GLSL_BACKEND=glslang b build glsl ./shaders
b build metal ./shaders
```

Review the plan before confirming broad scopes. Progress is completed build
units, not elapsed time. Native manifests own their subtrees; loose sources
group by directory and compiler/checker. Conflicting manifests and mixed C-family
sources reject rather than guess linkage; unrelated language units coexist.
Includes/headers are dependencies,
not independently runnable programs. Unknown files are counted as unassigned.
Small scopes build directly; home/ancestor roots, Downloads/Documents/Desktop,
1,000-file or 100-unit inventories require `--confirm`. No sudo is requested.

## Actual dogfooding across Vexgraph

b is essential to Vexgraph's current build entry, not a hypothetical integration.
`tools/b` invokes `personal/b/b run exec tools/workspace.c`, compiling and running
the project's C23 build graph. That graph owns repository targets, incremental
compile/link work, tests, coverage commands, IDE metadata and shader generators.
The standalone suite remains independent of ecosystem paths and dependencies.

```sh
# From the Vexgraph workspace root:
./tools/b targets
./tools/b build graphvex
```

The Graphvex build registers GLSL `.vert` and `.frag` sources, invokes
`glslangValidator -V` and stages `.spv` outputs in external build state; quad
shaders also become an embedded header. The new standalone shader adapters can
build compute `.comp` and staged `.glsl`, but this existing Vexgraph graph still
uses its explicit generators. The recursive builder references its assessment/
build separation without importing or replacing the ecosystem target graph.
Build success is not proof of GPU execution or finished R5 applications.

## Future and b's own build

`./b` bootstraps the CLI with an installed C23 compiler, strict warnings and
external cached state; changed launcher inputs trigger recompilation. On macOS
the build targets Apple Silicon M1 and macOS 14+. It does not need a separate
build-system generator to build itself.

Future work includes export/packaging, project-graph adoption of the shader
adapters, richer cross-directory dependency inference, broader platform support
and deeper integration with future projects and R5 apps.
Project-specific graphs should remain project-owned, borrowing b's command
surface rather than embedding Vexgraph policy into the standalone suite.

## Scope and Limitations

No export format or manifest schema is implemented yet.
`b export <manifestmainfile> <destination> <exe|app|msi|iso|zip>` rejects without
creating a destination. b does not replace Cargo, npm, CMake, Maven or Gradle.
Individual adapter source discovery is top-level; `build workspace` traverses
recursively and assigns directory/project units. Native project backends own
their dependency graphs and incremental behavior. It cannot infer arbitrary
multi-directory C linkage or turn every file into a valid executable.

Toolchain scripts retain native authority: this is not a sandbox. SQL requires
`B_SQL_DATABASE` with no default database. Firmware upload changes the board.
There is no implicit web server, cross-compilation framework or shared universal
cache. Workspace discovery excludes named output/cache/VCS directories and never
traverses directory symlinks; recognized source/manifest symlinks reject before
tools run. Callers must keep the tree stable. Native scripts and compiler includes
can read/write outside the root: this is not filesystem confinement or protection
against hostile build scripts. The process/filesystem implementation uses POSIX APIs; Windows remains a
gap. Shader compilation and a browser launch do not certify rendered output.

The existing integration owner `tests/b/workspace_test.py` still fails its
pre-migration Vexspoke IO/NIO path assertion. New recursive/shader owner passes
do not repair or confer readiness on that separate Vexgraph integration suite.

Tests are segregated in the workspace's independent `tests/b` repository:

```sh
python3 ../../tests/b/cli_test.py
python3 ../../tests/b/readme_test.py
PYTHONPATH=../../tests/b python3 -m unittest discover -s ../../tests/b/adapters -p '*_test.py' -v
```

Missing optional tools are explicit skips. Architecture follows the canonical
[preferences.md](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a),
the real workspace-root `../../preferences.md`.
