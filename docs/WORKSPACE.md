# b — the workspace territory

[Back to README](../README.md) · [Command tree](TREE.md) · [Adapters](ADAPTERS.md)

`b` is a path-agnostic build tool. Its only project state is one marker on disk
and one descriptor per unit. This file is the specification; the shipping binary
predates it.

## The command tree

```text
b
├── generate [<directory>]
│     claim a territory; write .b/workspace.json
│
├── assembly [<unit>] [--version <version>] [--target <target>] [--force]
│     write <unit>/b.assembly.json once; --version is required on creation
│
├── build [<unit>...] [--explain]
│     no argument builds every descendant unit; --explain names inert ones
│
├── run <file> [arguments...]
│     build the unit, box it, execute the box
│
├── breeze <file> [arguments...]
│     identical to run
│
├── box <main> [--target <target>] [--assembly <file>]
│     package an assembly; target defaults to this host
│
├── box <main> --assembly --name <name> --version <version> [...]
│     package using an assembly declared on the command line
│
├── box <main> --assembly default
│     package using an assembly synthesized from discovery
│
├── adapter list
│     names, selectors, requirements
│
├── adapter <name> [arguments...]
│     adapter-specific operations
│
└── help | --help                (no command also prints help)
```

That is the entire surface. There is no other verb, no mode word, and no named
registration of any kind.

`test` is deliberately absent: a test is a unit with an entry point, and
aggregating them is the caller's job, not `b`'s. See
[Tests are units, not a verb](#tests-are-units-not-a-verb).

## Territory

`.b/` is a **territory marker**, not a cache that happens to exist. Its presence
means: every file beneath this directory is runnable through `b`.

`b generate` is the only act that claims ground. Nothing else creates a
territory as a side effect.

Resolution walks up from the current directory:

- **`.b/` found**: that directory is the workspace root, and every unit beneath
  it resolves inside it.
- **No `.b/` found**: `b` has no territory and refuses. There is no implicit
  scan, no home-directory sweep, no fallback root.

```
b: no .b/ territory above /Users/vexgraph/src/thing
   run `b generate` to claim one
```

Consequences:

- Root inference disappears. The marker **is** the root; there is no `.git`
  sniffing and no cwd guesswork.
- `~/` is safe by construction. Nothing walks a home directory unless the user
  typed `b generate` there.
- Broad-root refusal disappears. Its only job was preventing scans nobody asked
  for; a scan now happens only when someone types one.

### Nested territories are refused

Claiming a directory that already sits inside a territory is rejected. One file
has exactly one owner, one object cache and one answer to "what builds this".

```
b: /Users/vexgraph/src/thing is already inside /Users/vexgraph's territory
   regenerate there, or claim outside it
```

### Autogen inside a territory

Inside a claimed territory, a unit the workspace has not recorded is added on
first use. This is scoped to the unit being asked for, never to the tree.

`b generate` remains the explicit full refresh, recursive from the directory it
is invoked in.

## Units

**A unit is a directory**, and owns the sources sitting directly in it.
Subdirectories are separate units, never merged upward.

```text
src/lang/vec3/     one unit: vec3.c, vec3d.c, vec3_int_float.c
src/lang/          not a unit; a container
```

`b build` in a container directory builds every descendant unit. `b build` with
an argument names one.

## Kinds

Each unit classifies itself from what it contains. An adapter answers the
question; the workspace engine never learns a language.

| finding | kind | `b build` | `b box` |
| :--- | :--- | :--- | :--- |
| an entry point | `app` | link to an executable | package the executable |
| sources, no entry | `library` | produce the chosen artifact | package it |
| nothing linkable | `inert` | nothing | refuse |

A directory without an entry cannot be boxed as an application; it is a library,
and `b box` says so rather than inventing an executable at its door.

An `inert` unit produces no artifact at all: markdown, data, or sources whose
adapter has no build step. It is not a failure and not a library. Runnability
is a separate question answered per file by its adapter, so a unit full of
Python can be inert for building and still run.

`b build` reports inert units as a count in its summary line, not as one message
per directory; `--explain` names them.

The entry point is adapter-owned: C looks for `main(`, Rust for `fn main(`,
while Python, TypeScript, Lua, Shell and PHP have top-level code and no entry at
all.

### Library kinds are semantic, never platform-spelled

A descriptor naming `dylib` cannot build on Linux or Windows. The kind names the
concept; the spelling follows the target.

| kind | darwin | linux | windows |
| :--- | :--- | :--- | :--- |
| `static` | `lib<name>.a` | `lib<name>.a` | `<name>.lib` |
| `shared` | `lib<name>.dylib` | `lib<name>.so` | `<name>.dll` |
| `objects` | `<source>.o` | `<source>.o` | `<source>.obj` |
| `executable` | `<name>` | `<name>` | `<name>.exe` |

`objects` stops after compiling. The unit's artifact is its object set under
`.b/obj/<unit>/`, one object per source, and nothing is archived.

The kind is derived by default and may be overridden in the assembly. An
explicit platform extension is an escape hatch, not the vocabulary.

## Assembly

`b assembly` writes one file per unit: the unit's identity and packaging intent.
It is authored once and never regenerated afterwards.

```json
{
  "application": {
    "id": "org.vexgraph.determinant",
    "name": "determinant",
    "displayName": "Determinant",
    "version": "0.4.1",
    "type": "gui",
    "authors": ["Vexgraph"],
    "publisher": "Vexgraph",
    "description": "R3 terminal toolkit",
    "license": "MIT",
    "homepage": "https://vex-graph.github.io",
    "icon": "assets/icon.png"
  },
  "build": {
    "entry": "main.c",
    "sources": [],
    "includes": [],
    "defines": [],
    "options": [],
    "link": "executable"
  },
  "payload": { "files": ["README.md", "LICENSE"] },
  "targets": {
    "darwin-arm64": {},
    "linux-x86_64": {},
    "windows-x86_64": {}
  },
  "formats": ["portable"],
  "statePolicy": "clean"
}
```

Every key is present in the file `b assembly` writes. Values it can derive are
filled in; values it cannot derive are written as their default rather than
omitted, so the file is a complete statement of intent and not a stub.

### The shape is final; there is no version field

`box.c` already parses the assembly's keys: `application {id, name, version,
authors, publisher, description, license, homepage, icon}`, `payload {entry,
files}`, `targets`, `formats`, `statePolicy`. The assembly adds `build {entry,
sources, includes, defines, options, link}` and nothing else. Targets are an
object keyed by canonical target name, so per-target settings are added as keys
rather than as parallel arrays.

The file carries no `schemaVersion`. The reader is strict: an unknown key is an
error, so a file cannot be silently misread. Extending the format means adding a
key inside its section and a test for it, a change in one place.

`b`'s own assembly is the first file written against it, and the acceptance test
for the schema: when `b assembly` lands, running it on `b` must reproduce that
file.

Three writers, and they never share a file:

| verb | writes | when |
| :--- | :--- | :--- |
| `b generate` | `.b/workspace.json` | always; facts are regenerated freely |
| `b assembly` | `<unit>/b.assembly.json` | **only when absent**; `--force` replaces |
| `b build`, `b run`, `b box` | `.b/obj/`, `.b/box/` | outputs only, never a source of truth |

`b assembly` is the `dotnet new` of the suite: it authors intent once and then
never touches it again.

### One unit, several assemblies

A unit may carry more than one assembly, one per target, because the product
genuinely differs per target. These filenames are the convention, and they are
what `b box` finds when no `--assembly` is given:

```text
<unit>/b.assembly.json                  the portable declaration
<unit>/b.assembly.darwin-arm64.json     exactly that target
<unit>/b.assembly.windows-x86_64.json   exactly that target
```

The target token is the canonical name, spelled once and never abbreviated. A
*discovered* file called `assembly-win-x86-64.json` would be a second vocabulary
for the same six targets, and the whole point of the floor is that there is one.
Passing such a file explicitly is fine — see below — because then you named it,
and b is not the one guessing.

Resolution when `b box` is called with no `--assembly` flag, for target `T`
(default: this host):

1. `b.assembly.T.json` — exact, wins.
2. `b.assembly.json` — used when its `targets` list contains `T`, or when it
   lists none, which means every target.
3. otherwise refuse, naming `T` and which file would have covered it.

**Authoring is not boxing.** `b assembly --target windows-x86_64` writes the
Windows file from any host, because a declaration is data. Only `b box` needs a
toolchain, and only for the target it is producing. That split is what lets a
macOS laptop and a Windows CI runner share one unit: each declares its own
assembly, each boxes its own.

### Where the assembly comes from

`b box` never invents an assembly quietly. The invocation says which of four
places to read, and the first matching form wins:

```
b box main.c                                    the unit's conventional assembly
b box main.c --assembly path/to/any.json        exactly that file, whatever it is called
b box main.c --assembly --name b --version 0.1.0 --type cli
                                                the command line is the assembly
b box main.c --assembly default                 b synthesizes one
```

- **No flag, no file**: refuse. Nothing is written, nothing is guessed, no
  `0.0.0` placeholder ever reaches an artifact.
- **`--assembly <path>`**: that file, read as written. The name carries no
  meaning — `assembly-win-x86-64.json`, `packaging.json`, anything. Only the
  `--target` flag decides which target the box is for, and the file's own
  `targets` list must allow it.
- **`--assembly` with fields**: the command line *is* the assembly. No file is
  consulted, so there is never a merge of two sources.
- **`--assembly default`**: every field discovery can see is derived, and the
  derivation is printed. Guessing is allowed only because it was asked for by
  name — and `--version` is still required, because the counter is not a guess.

A path is needed only when a unit carries a name b would not guess. The
conventional filenames from the previous section exist so the common case needs
no flag at all; they are never the only way to reach an assembly.

None of these write a file. A shell-declared assembly is for one artifact; if it
should persist, `b assembly` writes it and the next invocation drops the flags.

### What `--assembly default` derives

```text
name         the unit directory, kebab-cased
displayName  name, Title-Cased per word
id           <reverse-dns of publisher>.<name>      e.g. local.vex.determinant
version      not derivable: --version is required, or refuse
type         app with an entry becomes cli; library becomes library; inert refuses
publisher    OS full name, else login name, else "Unknown"
authors      [publisher]
description  first prose line of README.md facing the unit, else absent
license      LICENSE's first line when recognisable, else "UNLICENSED"
homepage     the git remote URL, when there is one
icon         assets/icon.png when it exists, else absent
build        entry, sources, includes from discovery
payload      README.md, LICENSE, and files/ when they exist
targets      all six canonical targets
formats      portable
statePolicy  clean
```

Every derived value is printed, so a synthesized assembly is not a black box:

```text
b: assembly default
   name      hello        (directory)
   version   0.3.0        (--version, as asked)
   type      cli          (main() in main.c)
   publisher vex          ($USER)
```

Every row above is observation except `version`, which is the author's own
counter. So `--assembly default` without `--version` refuses instead of filling
the hole, and names the missing flag rather than the missing file.

### Icon is semantic, the files are derived

`"icon": "assets/icon.png"` names one source image. `b box` emits the platform's
form: `.icns` inside a macOS bundle, `.ico` embedded in a Windows executable, a
sized PNG set on Linux. Naming the files directly would lock the assembly to one
platform for the same reason `dylib` would.

### Type is the product

`application.type` answers one question: what does boxing this assembly produce?
It is not a mode and not a preference — it is a product, declared per assembly,
so a Windows assembly and a macOS assembly of the same unit may differ.

| type | darwin | linux | windows | needs `build.link` |
| :--- | :--- | :--- | :--- | :--- |
| `cli` | bare executable | bare executable | `.exe` | `executable` |
| `gui` | `.app` bundle, `Info.plist`, `.icns` | executable, `.desktop`, PNG set | `.exe` with icon resource | `executable` |
| `service` | executable, launchd plist | executable, systemd unit | `.exe`, service manifest | `executable` |
| `plugin` | `.dylib` | `.so` | `.dll` | `shared` |
| `library` | `.a` / `.dylib` | `.a` / `.so` | `.lib` / `.dll` | `static` or `shared` |

A type whose required link disagrees with `build.link` is a cold validation
error naming both fields. There is no coercion and no guessed default.

### A product this host cannot make is refused

`b` does not cross-compile. A `gui` box for darwin means writing a `.app`: an
`Info.plist`, a `.icns`, and a Mach-O executable. A Windows host has none of
those. What it could produce is a directory that *looks* like a bundle and is
not one — worse than no artifact, because it would pass a file listing and fail
on the machine that opened it.

So `b box` refuses, names the files it would have written, and offers the real
ways out:

```text
b: cannot box determinant for darwin-arm64 from this windows-x86_64 host
   type "gui" on darwin produces
     determinant.app/Contents/Info.plist
     determinant.app/Contents/Resources/icon.icns
     determinant.app/Contents/MacOS/determinant
   a windows host can produce none of them: no Mach-O toolchain, no .icns.

   any of these works
     run the same command on a darwin-arm64 host
     let the CI leg do it          (.github/workflows/ci.yml, darwin-arm64)
     box for this host instead     (--target windows-x86_64)

   nothing was written.
```

Three escapes: another machine, CI, or a different target. What the message
never offers is a "close enough" artifact, because a box that lies about its
platform is the same defect class as a version that says `0.0.0`.

This is not a hypothetical constraint in this ecosystem: there is no Windows
machine, so the Windows artifact is proven by the CI runner or not at all.

### The box carries a baked copy

```
<unit>/b.assembly[.<target>].json   yours, permanent, never rewritten
       stamp at box time
<box>/manifest.json                b's, disposable, inside the artifact
```

The relationship is `assembly.cs` to compiled metadata: the declaration is
source, the baked copy is what the artifact carries. Because it is stamped in, a
shipped archive can say its own name and version.

## Version

`version` is the author's counter, declared once by them, in one vocabulary, and
projected outward per target. The same rule as `icon` and `dylib`: semantic in,
platform spelling out — but unlike those two, b never gets to pick it.

### A version is declared, never derived

`b` does not compute versions. There is no git tag read, no `VERSION` file, no
`0.1.0` default — the counter belongs to whoever owns the application, and a
build tool that guesses it is asserting authorship of something it does not own.

So the rule is one sentence: **`box` requires a version, and refuses without
one.** `run` and `breeze` only execute a local build, so they need no version:
they use the default assembly, whose version stays empty until the author sets
it. An empty version is never written into an artifact.

```text
b: hello declares no version
   a version is the project's own counter, so b does not invent one.
   set application.version in hello/b.assembly.json, or pass --version.
```

The two accepted sources are the assembly and `--version`, and the flag wins.
That is the whole resolution order — two entries, no fallbacks, nothing that
changes meaning depending on whether a tag exists.

This also fixes the failure mode that started the question: the published `b`
says `0.0.0` because CI had no tag, and the fallback filled the hole with a
placeholder. A required value cannot be silently placeheld. `b`'s own assembly
now says `0.1.0` because that is a claim we made deliberately, and bumping it is
a one-line edit by a human or an agent, not a side effect of a build.

`b assembly` therefore asks for the version at creation time, and `--assembly
default` requires it too: `default` derives what discovery can see, and says so
when asked for something only the author knows.

`b`'s remaining interest in the string is projection — it must be parseable to
become three platform values. That is validation for a purpose, not ownership.

### Lenient, then canonical

Strict SemVer rejects `1.0` and `0.4`, which are as common as `0.4.1`. So the
accepted form is:

```text
MAJOR[.MINOR[.PATCH]][-prerelease][+build]
```

canonicalized to three numeric components on read: `1.0` becomes `1.0.0`. What
is enforced is what actually matters: numeric components, no leading zeros,
optional `-prerelease` and `+build` in SemVer's own alphabet. Validation happens
once, cold, at the seam, because `--version` is shell input.

### One version, four platform values

A version is not one value. Each target wants a different one, and two of them
cannot express what the other two can.

| | darwin | windows | linux |
| :--- | :--- | :--- | :--- |
| user-visible | `CFBundleShortVersionString` (three integers) | `FileVersion` string | filename / manifest |
| machine | `CFBundleVersion` (digits and dots, must increase) | `FILEVERSION` (four 16-bit integers) | none |
| prerelease | no legal slot | string only, never numeric | `~` in deb ordering |
| build metadata | ignored | ignored | ignored |

So `1.2.3-beta.1` is valid and unrepresentable on both macOS and Windows. The
projection is:

| target | short | machine | where the full string survives |
| :--- | :--- | :--- | :--- |
| darwin | `1.2.3` | `1.2.3` | archive name, manifest |
| windows | `1.2.3` | `1,2,3,0` | archive name, manifest |
| linux | `1.2.3` | — | archive name, manifest |

The manifest always carries the declared string, so identity is never lost to a
platform that cannot spell it.

**Refusal beats truncation.** A component over 65535 refuses and names the field,
because Windows writes it as a 16-bit integer. A prerelease on a `gui` box warns
once and drops from the numeric fields only, since both platforms have no slot
for it.

### Version is part of the box signature

Without it, bumping `0.4.1` to `0.4.2` leaves a signature match and `b run`
reuses the old box, shipping the wrong version from a cache hit. With it, a bump
invalidates the box by itself.

## Include roots and dependencies

No include roots are configured. They are derived.

For each `#include "<path>"`, walk up from the source file to the first ancestor
where `<path>` exists. The union of those ancestors is the unit's include roots.
Nothing is guessed:

```c
// graphvex/src/compositor/compositor.c
#include "compositor/compositor.h"      // root: graphvex/src/
// vexspoke/src/lang/vec3/vec3.c
#include "lang/vec3/vec3.h"             // root: vexspoke/src/
// b/adapters/graph.c
#include "adapters/graph.h"             // root: b/
```

Cross-unit dependencies fall out of the same reading: when unit A includes a
header owned by unit B, A depends on B's artifact. The include **is** the
declaration, and no descriptor states it twice. An app linking a library it
includes is automatic, with an override for the cases inference gets wrong.

## Artifacts

```text
.b/workspace.json        the territory: units[] with dir, kind, sources, entry, deps
<unit>/b.assembly.json   identity and packaging intent, authored once
.b/obj/<unit>/           objects, content-addressed
.b/box/<unit>/           packaged artifacts
```

One file answers "what is this territory". The only file a human keeps is the
assembly, and it is also the only file `b` never rewrites.

## Running is building, boxing, then executing

`b run` follows: build the unit, package it, execute the package. Running and
boxing share one code path, so every run exercises what would ship.

Two requirements follow:

- **The box must be memoized by unit signature.** Re-packaging on every
  invocation makes the second run as slow as the first. A signature match reuses
  `.b/box/<unit>`.
- **Interpreted units short-circuit.** A script has nothing worth packaging; its
  adapter hands the file to the runtime. Boxing is a step the kind decides, not
  a universal stage.

### Tests are units, not a verb

There is no `b test`. A test is a program with an entry point in a directory,
which makes it a unit like any other: `b run tests/lang/vec3/vec3_test.c` builds
it, boxes it and runs it.

What `b` will not grow is an aggregator. "Run every matching test, collect exit
codes, report" is orchestration, and orchestration belongs to whoever is
orchestrating — a shell script, a CI job, `tools/shell/ci.sh`. Putting it in `b`
would make `b` the owner of a test policy it has no business knowing: which
directories count, whether a zero-match is a pass, how failures aggregate.

The same rule as the storage law: the tool that compiles does not also decide
what deserves to be compiled.

## What is frozen

The point of finishing this design is that it stays finished. These are the
things a future change is not allowed to touch:

- the six verbs and their arguments
- `.b/` as the only marker, and `generate` as the only act that claims ground
- a unit is a directory; three kinds; `objects` stops after compiling
- no version field: the manifest shape is final. New settings are added as keys
  inside an existing section, and the reader rejects keys it does not know, so a
  file written today still reads in ten years
- one assembly file per target, named by the canonical target
- version declared by the author; box refuses without one, run needs none
- three writers, three files, and no generated file is ever authoritative
- the six target names, spelled exactly as the Platform Support Floor spells them

Adding a language is a new file in `adapters/`. Adding a platform is a row in
the spelling and bundle tables. Neither touches the list above. That asymmetry —
a small total core, a wide additive edge — is the design; everything else here
is detail.

What is expected to change, and is therefore **not** part of the contract:
bundle layouts (`Info.plist` keys, `.desktop` fields, PE resources track OS
releases), icon encoders, and the compiler invocations adapters emit.

The invariant that matters most is the third writer rule. The old design failed
because a generated file was also authoritative: `tools/workspace.c` produced
`workspace.json`, the file became truth, and when the generator was deleted the
registry kept claiming `targets: []`. Nothing in this design can rot that way,
because the only file a human keeps is the assembly, and it is the only file `b`
never rewrites.

## Open questions

- **Icon rendering.** Deriving `.icns` / `.ico` / PNG sets needs either an image
  encoder inside `b` or delegation to a platform tool. Not decided.

## Current State

Nothing in this document is implemented. The shipping binary is a different
design: a global registry under `B_HOME/<name>/` claimed by `b project`, a
`--in` flag to reach it, an `exec` / `instance` mode pair, and a `graph` verb
that read a target graph generated by a project-owned `tools/workspace.c`.

That generator was deleted. With it gone, the shipping `b workspace` produces an
empty target list and `b build` has nothing to build, which is the state this
specification replaces.

Not yet implemented, in dependency order:

- [x] `b generate` claims a territory and writes `.b/workspace.json` with units
- [x] build/run resolve upward to the territory and refuse outside one
- [ ] unit discovery, kind classification and entry detection per adapter
- [ ] include-root derivation and inferred dependencies
- [ ] library kinds with per-target spelling
- [ ] `b build` over units, with the inert summary line
- [ ] `b assembly` writing a complete descriptor once; `--force` replaces it
- [ ] per-target assembly files and the `--target` resolution order
- [ ] host-capability refusal with the three escapes, and no artifact written
- [ ] `b box` reading the unit assembly and taking no manifest argument
- [ ] `b box --assembly` (command-line intent) and `--assembly default` (synth)
- [ ] `b box --assembly <file>` reading any named assembly file
- [ ] schema 2 in `box.c`: accept `build`, keep schema 1 parsing
- [ ] commit the release launchers so `payload.files` can name them
- [x] `vexgraph-hobby-projects/b/b.assembly.json` written (schema 2, first consumer)
- [ ] lenient version parse and canonicalization; per-target projection
- [ ] version required at the seam: no git read, no VERSION file, no default
- [ ] version in the box signature
- [ ] `b run` = build, box, exec with a signature-keyed box
- [ ] icon derivation per target, and `gui` / `service` bundle layouts
- [ ] `scripts/package_release.py` deleted; its job is `b box`
- [ ] `tests/ci_smoke_test.py` asserts `b breeze` rather than `b breeze exec`

## Scope and Limitations

- This defines the command surface, the on-disk layout and the inference rules
  for units, kinds and dependencies. It does not define adapter internals.
- It assumes the Canonical Include Paths Law holds (no parent hops), because
  dependency inference reads the include graph as written. Four parent-hop
  includes exist in `vexgraph-hobby-projects/b` itself and must be repaired
  before inference is trusted there.
- Cross-compilation is out of scope, and `b box --target` says so rather than
  approximating: the target must be this host, because a product the host
  cannot build is a bundle that lies about its platform. Authoring an assembly
  for another target is always allowed; boxing it there is not.
- `box.c` already parses the assembly schema but renders no platform bundle for
  any target, so the `gui` and `service` rows are new work, not wiring.
