# b

A general-purpose, language-agnostic build system written in C23.

One CLI to **run, build and export** projects across languages and platforms.
The build graph describes targets, inputs, dependencies and outputs; adapters
connect that graph to language toolchains, execution environments and packaging
formats. C is the implementation language, not a restriction on what b builds.

The current implementation starts with C and Java. Other adapters and the shared
project-manifest contract remain planned, as detailed below.

```sh
git clone https://github.com/vex-graph/b.git
cd b
./b --help
```

The `b` launcher bootstraps the C CLI with your system compiler. Put this checkout
on `PATH` to use `b` from any directory. It preserves your working directory:
relative filenames refer to your project, not this repository. No global install
or toolchain download is performed automatically.

## Command contract

```text
b run <exec|instance> <filename> [-- program arguments...]
b build <language> [directory]
b export <manifestmainfile> <destination> <exe|app|msi|iso|zip>
```

An omitted build directory means the current directory. Paths with spaces are
ordinary arguments; quote them in your shell. External tools are invoked with
argument arrays, not shell command strings.

### Available now

```sh
b run exec ./hello.c -- one two  # compile C23, then execute; return its exit code
b run exec ./hello.rs           # compile Rust (rustc), then execute (needs rustc)
b run instance ./Hello.java -- world # normal JDK source run, no packaged app
b java ./Hello.java -- world    # shorthand for b run instance ./Hello.java
b run exec ./Hello.java -- world # compile classes first, then launch the main class
b run exec ./program -- arg     # execute an existing native executable
b build c                      # compile this directory's top-level .c files
b build c ./native             # produce one executable, requiring a main()
b build java ./java-src         # javac this directory's top-level .java files
b build rust ./crate            # compile this directory's top-level .rs files
```

Rust support requires a `rustc` on `PATH` (install via `rustup`); b does not
download it. Rust is compiled, so it has no `run instance` source runtime and
rejects instance mode like C. C# and other languages remain planned.

`run instance` runs a file as-is through its runtime, or runs an existing native
executable. It does not build an app, package, supervise or manage instances.
`run exec` builds a runnable source artifact first, then launches it. For C this
is a native executable; for Java it is compiled classes plus the JDK runtime.
Existing executables are already built and launch directly in either mode.
Creating a macOS `.app` bundle for the generic CLI remains an export-adapter gap.

Java instance execution uses the JDK's source launcher (`java File.java`);
Java exec/project builds use `javac`. Single-file Java exec currently assumes
a default-package main class matching the filename. Package/dependency discovery, recursive source
discovery and build-graph configuration are not implemented yet. C compilation
uses `-std=gnu23 -Wall -Wextra -Werror`; macOS uses the arm64 M1/macOS 14 floor.
`CC` may name one compiler executable, not an embedded shell command.
C source has no direct runtime in this version: `run instance file.c` rejects
without compiling and directs the caller to `run exec`. More runtime adapters
will extend instance execution without introducing app packaging.

Builds print the output path. Outputs and bootstrap binaries live outside the
source tree under `~/Library/Application Support/b` on macOS, or
`$XDG_CACHE_HOME/b` (`~/.cache/b` by default) elsewhere. `B_HOME` overrides that
location. Project outputs are separated by canonical project-path hashes.
Builds currently recompile every time; caching and dependency tracking remain
with the workspace engine below, not the new language front end.

### Workspace compatibility

`workspace.c` preserves the existing workspace build graph, cache, shaders,
tests and app launcher during migration. The worktree's `tools/b` is a
compatibility launcher:

```sh
./tools/b test [filter]
./tools/b run <target>
./b/b workspace /path/to/workspace build
```

This legacy graph is a compatibility adapter, not b's general project model.
Moving its target declarations into the shared project manifest is the next
migration step. Generic `b build c` does not build that entire workspace;
project sources belong to their own checkouts, not to the build system.

## Planned, explicitly rejected for now

`export` will read one main manifest and package its declared entry point,
resources and dependencies. Export formats are adapters, not renamed binaries:

| Format | Intended output | Required implementation |
| --- | --- | --- |
| `exe` | Native executable | Target toolchain, dependency policy; Windows PE on Windows |
| `app` | macOS application bundle | Info.plist, executable, resources, signing policy |
| `msi` | Windows installer | Installer identity, upgrade/uninstall contract and MSI toolchain |
| `iso` | ISO image | Image toolchain; bootability is an additional explicit contract |
| `zip` | Portable archive | Declared file layout and archive toolchain |

No export format or manifest schema is implemented yet. A recognized export
request returns a clear nonzero error and creates no destination. Cross-compiling
and signing require explicitly available toolchains/credentials; C itself does
not make them automatic. Unknown languages/formats also reject nonzero.

## Architecture and next steps

The core is independent of any one language. Language adapters resolve a
project's toolchain and translate its targets into build actions; execution
adapters launch native programs, managed runtimes or browsers; export adapters
package the declared outputs. Existing package/dependency metadata belongs to
its toolchain and is consumed rather than replaced by a new package registry.

| Adapter (planned unless noted) | Native tools |
| --- | --- |
| C (initial file/directory support) / C++ | Clang, GCC, MSVC |
| Rust (initial exec/build via `rustc`) | rustc; Cargo for full projects |
| C# | dotnet |
| Java (initial file/directory support) | JDK source launcher/compiler and project build tooling |
| JavaScript / TypeScript | Node, Bun, npm/package scripts and project bundlers |
| HTML / web | Browser launch, loopback development server when needed |

For HTML, the intended `b run exec index.html` launches a browser, while a web
project uses its declared development-server command. `b build web <directory>`
would produce the project's static/deployable output. Browser launch, HTTP
serving and web builds are **not implemented yet**. A local-file browser launch
is not equivalent to testing a web app with a server, modules, routing and assets.
Servers must have explicit port/bind/lifetime policies and stop with their owner.
Mixed-language projects will declare separate targets and dependencies, not a
single guessed language for the entire directory.

1. CLI parser: validate the command before launching tools or creating outputs.
2. Language adapters: C and Java first; add other languages without forking the CLI.
3. Project manifest/build graph: own targets, inputs, flags and dependencies.
4. Execution adapters: direct file/runtime runs and built-artifact runs, preserving
   arguments and exit codes; neither mode implies background supervision.
5. Export adapters: validate the manifest, stage outputs, publish only on success.
6. Move the compatibility workspace graph behind the same project contract; retain incremental
   caching rather than replacing it with a shell-script chain.

Current runtime evidence is macOS only. Windows support needs a native process
and filesystem adapter; the initial implementation uses POSIX APIs. Source
arguments and manifests are executable build instructions, not a sandbox.

## Verification

```sh
python3 ../tests/b/cli_test.py
```

The suite uses temporary projects and an isolated `B_HOME`, exercising bootstrap,
C builds/runs, argument forwarding, exit codes, rejection without export side
effects, direct instance runs versus compiled exec runs, and the optional Java
toolchain. Missing Java is an explicit test skip.
Tests live in the independent workspace `tests/b/` checkout, not this repository.
Workspace compatibility checks are separate from language-adapter checks.
