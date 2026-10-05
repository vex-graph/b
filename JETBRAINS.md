# Using b in a JetBrains IDE

An **external tool** is simply a command the IDE launches for you. b does not
need an IDE plugin or a generated CMake project for this workflow. The same
setup works in CLion, IntelliJ IDEA, Rider, PyCharm, and similar JetBrains IDEs
that provide External Tools. Menu wording may vary slightly by version.

## Before opening settings

Clone b as explained in [README.md](README.md). In a terminal, run
`/absolute/path/to/b/b --help`. Also check the compiler or runtime for the file
you want to run. b does not install these tools for you.

Use the **launcher named `b` inside the checkout**, not `b.c` and not an old
cached CLI binary. An absolute path is easiest: it avoids depending on the IDE's
`PATH`. Keep the checkout somewhere stable if you move projects often.

## Add “Run current file”

1. Open **Settings** (macOS: **Preferences/Settings**), then **Tools → External Tools**.
2. Click **+** to add a tool. Name it **Run current file**, and optionally put it
   in a group named **b**.
3. Enter these fields:

   | Field | Value |
   | --- | --- |
   | Program | `/absolute/path/to/b/b` |
   | Arguments | `run exec "$FilePath$"` |
   | Working directory | `$FileDir$` |

   The Program field is the path to the executable itself, not a shell command.
   Use the file chooser if the checkout path contains spaces.
4. Enable the tool's console/output window option so you can see results and
   errors. Click **Apply**, then **OK**.
5. Open a supported entry file in the editor and **save it**. Choose
   **Tools → b → Run current file** (or its entry under **Tools → External Tools**).

`$FilePath$` means the full path of the currently selected editor file.
`$FileDir$` means that file's folder. JetBrains expands these macros before
calling b. Quotes around `"$FilePath$"` keep a filename with spaces together.
You can insert macros using the IDE's macro selector rather than typing them.

This command uses `run exec`: C/Rust/C# compile first, Java compiles classes,
and Python/R run their interpreters. The selected file must be an entry point,
not an arbitrary helper file. For multi-source programs, use a directory build
tool instead. Nothing here automatically attaches the IDE debugger.

## Optional tools and shortcuts

To run a source file through its runtime, create **Run current instance** with
Arguments `run instance "$FilePath$"` and the same Program/Working directory.
C and Rust reject this mode; Java, Python, C# and R use their runtime adapters.

To pass program arguments, append them after `--`, for example:
`run exec "$FilePath$" -- "hello world"`. They go to your program, not to b.

For a directory build, create a separate tool with Arguments such as
`build rust "$ProjectFileDir$"` and Working directory `$ProjectFileDir$`.
Choose the adapter explicitly and make sure the directory matches its contract
in README.md. `$ProjectFileDir$` is a JetBrains external-tool macro for the
project's directory; do not paste an unrelated user's home path.

Under **Settings → Keymap**, search for your external tool's name and assign a
keyboard shortcut. To include it in an existing run configuration, open
**Run → Edit Configurations**, find **Before launch**, and add **Run External Tool**
if that configuration type supports it. This runs b *before* the configuration's
own command; it does not replace that command or turn b into a debugger. Use
the Tools menu/shortcut for a simple one-command run without a duplicate launch.

## Upload the selected Arduino sketch

You can edit `.ino` files in JetBrains and upload without opening Arduino IDE.
Syntax highlighting or language-specific completion may need an Arduino plugin
or a C++ file-type association; the external tool itself does not depend on one.

1. Connect the board and find its port and FQBN using Arduino CLI's `board list`
   and `board listall`. An Uno uses `arduino:avr:uno`; clones may report an unknown
   model, so check the actual board rather than guessing from the USB adapter.
2. Add a new external tool named **Upload Arduino sketch**:

   | Field | Value |
   | --- | --- |
   | Program | `/absolute/path/to/b/b` |
   | Arguments | `upload arduino "$FilePath$" --fqbn arduino:avr:uno --port /dev/cu.YOUR_BOARD` |
   | Working directory | `$FileDir$` |

   Replace the example board/port with yours. Windows serial ports use names
   such as COM3, but b's native Windows process adapter is not implemented yet.
3. Save and select the `.ino` file. Close any Serial Monitor using that port,
   then invoke the tool from Tools or your assigned shortcut.

b compiles before uploading, so a failed build won't flash an old result.
**Uploading replaces the sketch on the connected board.** A standalone `.ino`
is staged without modifying its source. For multiple tabs or sibling headers,
keep the normal Arduino folder layout: `Sketch/Sketch.ino`. An FTDI adapter
without working automatic reset may need RESET pressed when uploading starts.

If Arduino CLI is only inside the macOS IDE app, b finds the default bundle
location automatically. For another installation, expose `arduino-cli` on PATH
or set `ARDUINO_CLI` to its full executable path in the IDE's environment.
Installed board cores and libraries are still managed through Arduino IDE/CLI.

## If it fails

- **Cannot start b:** check the Program path and launcher executable permission.
- **Cannot launch rustc/dotnet/Rscript/etc.:** the IDE's environment differs from
  your terminal's. Make the toolchain's executable directory visible in the
  environment used to launch the IDE, then restart it. A shell-only PATH export
  may not be inherited by an IDE opened from the Dock. On Homebrew macOS installs,
  `/opt/homebrew/bin` is commonly needed; include `/usr/local/bin` if R lives there.
- **Old source runs:** save the file before invoking the tool.
- **Wrong file runs:** select the intended entry file in the editor first.
- **C#/Java project fails:** read the adapter limits in README.md; project-system
  discovery is not implemented just because the IDE supports that language.

This is generic b suite integration. A workspace-specific forwarding script may
have a different command grammar; do not substitute it for the launcher above.
