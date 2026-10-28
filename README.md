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

## Current State

b is working build infrastructure, not merely an experiment. Its standalone
CLI implements native-tool build/run delegation, adapter discovery, tool-presence
diagnostics, recursive workspace assessment/build, GLSL/Metal shader delegation,
explicit Arduino upload, and a first packaging surface: `b create manifest`
writes a starter form and `b box` validates schema-1 manifests and exports the
declared payload as a portable directory. Other formats (`.app`, `.msi`, `.pkg`,
`.dmg`, `.iso`, ZIP, TAR) and target-specific exports are not implemented yet.
Named projects can be registered by name, refreshed into a language-neutral
adapter inventory, and used as context for file runs.
Vexgraph also maintains a separate project-owned build graph. Workspace builds
are proved cross-platform (macOS, Linux, Windows) after the `b` toolport
amendment; glslang, shaderc, Apple Metal, NSIS, and Windows PE toolchains all
have real compiler proof, and the CLI seam has Apple `leaks` proof.

**Platform parity:** `b` now builds and runs on macOS, Linux, and Windows with a
single portable source tree. The build script uses portable flags (`-std=gnu23
-O2 -Wall -Wextra -Werror`) with no macOS-specific arch/cpu pins. Process
spawning uses `_WIN32` guards: `CreateProcess` on Windows, `posix_spawnp` on
POSIX. Directory creation uses `_mkdir`/`mkdir` with `_stat`/`stat` probes
platform-aware. Windows is now in `proven` alongside macOS; Linux is the latest
addition (see `../../tools/platforms.json`).

Current automated proof spans macOS, Linux, and Windows; see the shared
`../tests/b` owner suites and workspace `../../metadata/gists/test-checklist.md`
for content-specific evidence. These scoped owner runs do not establish every
toolchain, platform or GPU execution claim.

## Build, Breeze, Box

```text
b project <name> <project-path>
b project remove <name>
b refresh <name>
b breeze exec <file> [--in <name>] [-- arguments...]
b breeze instance <file> [--in <name>] [-- arguments...]
b run <exec|instance> <file> [--in <name>] [-- arguments...]  # alias for breeze
b build <adapter> [directory]
b build workspace <directory> [--plan|--confirm]
b create manifest <path>
b box <manifest.json> <path-to-export>
```

Register the project once, refresh its index after the tree changes, then
run files through the appropriate adapter. A single file can also run without
a registered project by omitting `--in`.

`exec` builds a runnable source artifact first for compiled-language adapters;
`instance` uses a source runtime where available. Script adapters use their
runtimes in both modes. C, C++ and Objective-C reject source instance mode;
Zig compiles in both modes. HTML opens the original document, not a built app.
Build failures do not launch stale artifacts. Arguments are forwarded literally;
native tool wrappers can affect the final exit status (notably `go run`).

```sh
b create manifest ./box.json      # write the starter form; never overwrites
b box ./box.json ./release        # validate, then export the declared payload
b run exec ./hello.c -- one two   # run is an alias for breeze
```

```sh
git clone https://github.com/vex-graph/b.git
cd b
./b --help
./b breeze exec ./hello.c -- one two
./b breeze instance ./app.py
./b breeze instance ./Hello.java -- world
./b run exec ./hello.c -- one two
```

Use an absolute launcher path or add this checkout to `PATH` to use b from other
projects. It preserves the caller's working directory. `B_HOME` selects external
build state; the macOS default is `~/Library/Application Support/b`.

## Download a build

No GitHub Release is needed to download a CI build. Open the [B CI workflow
runs](https://github.com/vex-graph/b/actions/workflows/ci.yml), choose a
successful run, then download `b-release-assets` from its **Artifacts** section.
That bundle contains the archive and SHA-256 file for each supported target:
macOS arm64 and x86_64, Linux arm64 and x86_64, and Windows arm64 and x86_64.
The complete bundle appears only after all six target jobs pass. A passing
target job also publishes its own `b-<target>` artifact, which you can download
when you only need that platform.

You can also download the bundle with GitHub CLI after signing in and selecting
a successful run ID:

```sh
gh run list --repo vex-graph/b --workflow ci.yml --branch main
gh run download <run-id> --repo vex-graph/b --name b-release-assets
```

The Actions download is a ZIP; unzip it, then choose the `.tar.gz` matching your
operating system and CPU. Verify that archive with its matching `.sha256` file
before extracting it. For example:

```sh
# macOS
shasum -a 256 -c b-darwin-arm64.tar.gz.sha256

# Linux
sha256sum -c b-linux-x86_64.tar.gz.sha256
```

On Windows, compare the SHA-256 shown by
`Get-FileHash .\b-windows-x86_64.tar.gz -Algorithm SHA256` with the first value
in the matching `.sha256` file. Extract the selected `.tar.gz`, then run the
launcher beside its `b-core` file:

```sh
./b --help
```

On Windows PowerShell, run:

```powershell
.\b.cmd --help
```

Keep the extracted files together; add that directory to `PATH` to call `b`
from other directories.

Workflow artifacts are temporary downloads and expire according to the
repository's retention settings (90 days by default). The included
`install.sh` and `install.ps1` scripts currently download from GitHub Releases,
so they become useful after a release is published; they do not install an
Actions artifact. See [GitHub's artifact download guide](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/download-workflow-artifacts).

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
For an external CMake project, use `b build cmake <directory>`.

## Tree

See the [command tree and examples](docs/TREE.md) for the full CLI and the separate
Vexgraph workspace commands.

```text
b/
├── b                 bootstrap launcher; builds outside the checkout
├── project/          named registry, file resolution and adapter inventory
├── util/             CLI, shared contracts, packaging, inspection and process helpers
├── adapters/         native-tool adapter file pairs and registry
└── docs/             adapter behavior, IDE setup and command examples
```

## Named projects

Register a directory once, then address it by name. The registry is stored in
the per-user `B_HOME/projects/<name>/` directory (by default
`~/Library/Application Support/b/projects/<name>/` on macOS). Project files
stay in their existing directory; registration stores the canonical root. The
name `remove` is reserved for the project removal command.

```sh
b project notes ~/work/notes
b project notes
b refresh notes
b breeze exec src/main.c --in notes -- argument
b project remove notes
```

With `--in`, a relative file path is resolved from the registered project root,
the process starts at that root, and build/run adapters receive that project
context. Absolute file paths are accepted only when they remain inside the
registered root. Without `--in`, `b breeze` keeps its standalone file behavior.
`b refresh <name>` rebuilds the adapter-recognized file inventory at
`B_HOME/projects/<name>/index.tsv`. It skips common dependency/cache directories,
does not invoke build tools, and never edits the project source tree. Removing a
project with `b project remove <name>` deletes only B's saved root/index; it
leaves project files in place.

## Box: manifest and portable export

`b create manifest <path>` writes a schema-1 JSON form without replacing an
existing file. Fill in the application identity, name, version, authors,
publisher, description, license, the payload entry and files, the requested
formats and the state policy; then `b box <manifest.json> <path-to-export>`
validates the manifest and exports the declared payload.

```json
{
  "schemaVersion": 1,
  "application": { "id": "com.example.app", "name": "Example App", "version": "2.0.0" },
  "payload": {
    "entry": "dist/app",
    "files": [{ "source": "assets/logo.txt", "destination": "resources/logo.txt" }]
  },
  "targets": [],
  "formats": ["portable"],
  "statePolicy": "clean"
}
```

`box` copies the entry under its own name plus each declared file at its
declared destination (the file name by default), preserving permission bits,
then publishes the destination with a single rename. It refuses symlinks and
non-regular payload entries, refuses a destination that already exists, and
removes its private staging directory when anything fails, so a failed export
leaves any previous export untouched. The `statePolicy` is `clean`: only
declared payload is copied, never machine or user state.

## JetBrains IDEs

[JETBRAINS.md](docs/JETBRAINS.md) explains External Tools in CLion, IntelliJ IDEA,
Rider, PyCharm and similar IDEs. Build/run the current file without switching
editors. b does not supply completion, refactoring, language plugins or a
debugger; IDE appearance remains user-verified.

## Adapters

[ADAPTERS.md](docs/ADAPTERS.md) documents tools, selectors, build/run behavior and
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

## Using B with the Vexgraph workspace

The Vexgraph workspace has its own C23 build graph and umbrella launcher. That
integration is separate from this standalone B checkout; the workspace launcher
still contains a path for the previous B layout and needs its own migration.
This B checkout remains usable directly and can register/index any accessible
project root without importing the workspace's target graph.
The outer workspace owns its target graph and project-specific generators. A
successful build does not prove GPU execution or finished R5 applications.

## Future and b's own build

`b` bootstraps the CLI with an installed C23 compiler, strict warnings and
external cached state; changed launcher inputs trigger recompilation. On macOS
the build targets Apple Silicon M1 and macOS 14+. It does not need a separate
build-system generator to build itself.

Future work includes the remaining export formats (`.app`, `.msi`, `.pkg`,
`.dmg`, `.iso`, ZIP, TAR), signing and notarization, installer lifecycle,
per-target exports, project-graph adoption of the shader adapters, richer
cross-directory dependency inference, broader platform support and deeper
integration with future projects and R5 apps.
Project-specific graphs should remain project-owned, borrowing b's command
surface rather than embedding Vexgraph policy into the standalone suite.

## Scope and Limitations

`b create manifest <path>` writes a starter schema-1 JSON template without
replacing an existing file. `b box <manifest.json> <path-to-export>` parses and
validates that manifest and exports the declared payload as a **portable
directory**; the requested `formats` array may only name `portable` today, so
`.exe`, `.app`, `.msi`, `.pkg`, `.dmg`, `.iso`, ZIP and TAR exports reject
loudly. `targets` must be empty: per-target exports, signing, notarization,
installer lifecycle, upgrades and uninstall behavior are not implemented.
Loading rejects unknown or duplicate keys, unsupported `\u` escapes, missing
required fields, absolute paths and `.`/`..` path components; `box` refuses a
destination that already exists and never copies machine or user state. Export
is not a sandbox: a stable payload tree is required while it runs. b does not
replace Cargo, npm, CMake, Maven or Gradle.
Individual adapter builds discover source files at the top level;
`build workspace` recursively assigns directory/project build units, and
`refresh <name>` recursively indexes files recognized by the adapter registry.
Refresh skips common VCS, dependency, build and cache directories. Native
project backends own dependency graphs and incremental behavior; indexing does
not infer app entry points, Python environments or arbitrary multi-directory C
linkage.

Toolchain scripts retain native authority: this is not a sandbox. SQL requires
`B_SQL_DATABASE` with no default database. Firmware upload changes the board.
There is no implicit web server, cross-compilation framework or shared universal
cache. Workspace discovery excludes named output/cache/VCS directories and never
traverses directory symlinks; recognized source/manifest symlinks reject before
tools run. Callers must keep the tree stable. Native scripts and compiler includes
can read/write outside the root: this is not filesystem confinement or protection
against hostile build scripts. The process/filesystem implementation uses POSIX APIs; Windows remains a
gap. Shader compilation and a browser launch do not certify rendered output.

The shared B test fixtures still expect the previous `personal/b/b` checkout
path. Running `b breeze exec tests/b/cli_test.py --in ecosystem` reached Python,
but its setup stopped before tests ran because that old path no longer exists.
Update the fixtures before treating their result as evidence for this checkout.

Tests are segregated in the workspace's independent `../tests/b` repository:
the fixture root must first be updated from the previous `personal/b/b` layout
to this checkout.

```sh
python3 ../tests/b/cli_test.py
python3 ../tests/b/readme_test.py
PYTHONPATH=../tests/b python3 -m unittest discover -s ../tests/b/adapters -p '*_test.py' -v
```

Missing optional tools are explicit skips. Architecture follows the canonical
[preferences.md](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a),
the compressed workspace law list `../../metadata/gists/preferences/gist.md`.
