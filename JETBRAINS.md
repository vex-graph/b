# Using b in a JetBrains IDE

An **external tool** is simply a command the IDE launches for you. b does not
need an IDE plugin or a generated CMake project for this workflow. The same
setup works in CLion, IntelliJ IDEA, Rider, PyCharm, and similar JetBrains IDEs
that provide External Tools. Menu wording may vary slightly by version.

## Build, breeze, box — not a language plugin

Coding a different language in a JetBrains IDE can be awkward: syntax awareness,
completion, refactoring, project indexing and debugger support may be missing or
incomplete. b does not add those IDE features. It gives you a way to invoke the
real build/run tools from the editor you chose. That distinction is the point.

**Build, breeze, box** is the workflow direction: build through native tools,
run without switching editors, and package when a real export adapter exists.
Current builds/runs and Arduino upload are implemented; export is still planned.
A Run button calling b is not proof of language completion, debugging, browser
rendering or production packaging. Assess those capabilities independently.

## Before opening settings

Clone b as explained in [README.md](README.md). In a terminal, run
`/absolute/path/to/b/b --help`. Also check the compiler or runtime for the file
you want to run. b does not install these tools for you.

Use the **launcher named `b` inside the checkout**, not `b.c` and not an old
cached CLI binary. An absolute path is easiest: it avoids depending on the IDE's
`PATH`. Keep the checkout somewhere stable if you move projects often.

## Method 1: manual UI setup

### Add “Run current file”

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

This command uses `run exec`: C/C++/Rust/Swift/Objective-C/C# compile first,
Java compiles classes, and script adapters use their runtimes. HTML opens a local
browser file; `package.json` delegates npm build then start. SQL additionally
requires an explicitly selected database in the tool's environment.
The selected file must be an entry point,
not an arbitrary helper file. For multi-source programs, use a directory build
tool instead. Nothing here automatically attaches the IDE debugger.

### Optional tools and shortcuts

To run a source file through its runtime, create **Run current instance** with
Arguments `run instance "$FilePath$"` and the same Program/Working directory.
C, C++ and Objective-C reject this mode; Swift and scripting/managed adapters
use their runtime paths. Arduino uses the separate upload tool below.

To pass program arguments, append them after `--`, for example:
`run exec "$FilePath$" -- "hello world"`. They go to your program, not to b.

For a directory build, create a separate tool with Arguments such as
`build rust "$ProjectFileDir$"` and Working directory `$ProjectFileDir$`.
Choose the adapter explicitly and make sure the directory matches its contract
in README.md. `$ProjectFileDir$` is a JetBrains external-tool macro for the
project's directory; do not paste an unrelated user's home path.

For an existing CMake project, use Arguments `build cmake "$ProjectFileDir$"`.
b delegates configuration and building to CMake and prints its external build
directory. CMake's own targets, dependencies and flags remain authoritative;
this tool does not guess an executable to launch afterward. This is a separate
workflow from running one source file or uploading Arduino firmware.

For npm projects, use `build npm "$ProjectFileDir$"` to run the declared build
script, or select `package.json` and use the run tool to build then start it.
For PostgreSQL scripts, configure `B_SQL_DATABASE` and libpq connection settings
in the IDE/tool environment before invoking b. Use a disposable database for
testing; do not rely on a default connection or put database passwords in XML.

Under **Settings → Keymap**, search for your external tool's name and assign a
keyboard shortcut. To include it in an existing run configuration, open
**Run → Edit Configurations**, find **Before launch**, and add **Run External Tool**
if that configuration type supports it. This runs b *before* the configuration's
own command; it does not replace that command or turn b into a debugger. Use
the Tools menu/shortcut for a simple one-command run without a duplicate launch.

### Upload the selected Arduino sketch

You can edit `.ino` files in JetBrains and upload without opening Arduino IDE.
Syntax highlighting or language-specific completion may need an Arduino plugin
or a C++ file-type association; the external tool itself does not depend on one.

1. Connect the board and find its port and FQBN using Arduino CLI's `board list`
   and `board listall`. An Uno uses `arduino:avr:uno`; clones may report an unknown
   model, so check the actual board rather than guessing from the USB adapter.
2. Put the board in the very first line of your primary `.ino`:

   ```cpp
   // b_build("arduino:avr:uno")
   ```

   Replace the quoted FQBN for another board. No blank line or comment should
   precede this header. This makes the board choice travel with the sketch.
3. Add a new external tool named **Upload Arduino sketch**:

   | Field | Value |
   | --- | --- |
   | Program | `/absolute/path/to/b/b` |
   | Arguments | `upload arduino "$FilePath$" --port /dev/cu.YOUR_BOARD` |
   | Working directory | `$FileDir$` |

   Replace the example port with yours; the board comes from the header.
   You can append `--fqbn arduino:avr:uno` as an explicit cross-check, but it
   must match the header. Windows serial ports use names
   such as COM3, but b's native Windows process adapter is not implemented yet.
4. Save and select the `.ino` file. Close any Serial Monitor using that port,
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

## Method 2: inspectable XML and `.idea` run configuration

Use this path when you want to review, compare or share the setup as files.
It represents the same upload tool as Method 1. **Close the IDE before editing
its settings files**, back up existing files, and merge instead of replacing
another tool or project configuration.

There are two separate pieces:

- External Tools are normally **IDE-level settings**, stored in the IDE's
  configuration directory under `tools/`. They are not automatically portable
  just because you put a copy in `.idea/tools/`.
- A shared project run configuration can live under **`.idea/runConfigurations/`**
  (or `.run/` in newer IDEs). It can reference the global external tool.
  Each developer/IDE still needs that tool installed under the same group/name.

### A. Define the external tool

Locate the active IDE's configuration directory using
[JetBrains' directory guide](https://www.jetbrains.com/help/idea/tuning-the-ide.html#config-directory).
On macOS it is typically `~/Library/Application Support/JetBrains/<product-version>/`;
use your actual product/version, not a hardcoded CLion directory for every IDE.
Save this as `tools/b.xml`, or merge it with an existing b group:

```xml
<toolSet name="b">
  <tool name="Upload Arduino sketch" description="Compile then upload the selected sketch"
        showInMainMenu="true" showInEditor="true" showInProject="true"
        showInSearchPopup="true" disabled="false" useConsole="true"
        showConsoleOnStdOut="true" showConsoleOnStdErr="true" synchronizeAfterRun="true">
    <exec>
      <option name="COMMAND" value="/absolute/path/to/b/b" />
      <option name="PARAMETERS" value="upload arduino &quot;$FilePath$&quot; --port /dev/cu.YOUR_BOARD" />
      <option name="WORKING_DIRECTORY" value="$FileDir$" />
    </exec>
  </tool>
</toolSet>
```

Replace the launcher path and port. The board stays in the `.ino`'s first-line
`// b_build("arduino:avr:uno")` header. XML uses `&quot;` for argument quotes.
For a generic run tool, use a different tool name and PARAMETERS value
`run exec &quot;$FilePath$&quot;`. Do not create duplicate group/tool names.

### B. Add a project Run button

With the **Shell Script** plugin enabled, save this as
`.idea/runConfigurations/b_upload_arduino.xml` in the project:

```xml
<component name="ProjectRunConfigurationManager">
  <configuration default="false" name="b Upload Arduino" type="ShConfigurationType">
    <option name="SCRIPT_TEXT" value=":" />
    <option name="INDEPENDENT_SCRIPT_PATH" value="true" />
    <option name="SCRIPT_PATH" value="" />
    <option name="SCRIPT_OPTIONS" value="" />
    <option name="INDEPENDENT_SCRIPT_WORKING_DIRECTORY" value="true" />
    <option name="SCRIPT_WORKING_DIRECTORY" value="$PROJECT_DIR$" />
    <option name="INDEPENDENT_INTERPRETER_PATH" value="true" />
    <option name="INTERPRETER_PATH" value="/bin/sh" />
    <option name="INTERPRETER_OPTIONS" value="" />
    <option name="EXECUTE_IN_TERMINAL" value="false" />
    <option name="EXECUTE_SCRIPT_FILE" value="false" />
    <envs />
    <method v="2">
      <option name="ToolBeforeRunTask" enabled="true" actionId="Tool_b_Upload Arduino sketch" />
    </method>
  </configuration>
</component>
```

The before-launch task runs the external tool; the inline `:` command is only
a shell no-op afterward. This avoids launching b twice. The action ID contains
the exact group (`b`) and tool name (`Upload Arduino sketch`); renaming either
requires updating the reference. This is a Run wrapper, not an Arduino debugger.

Reopen the IDE, inspect **Tools → External Tools** and **Run → Edit Configurations**,
then select **b Upload Arduino**. Save/select the `.ino` before pressing Run.
If your IDE version writes a different schema, create/save one configuration
through its UI and use that generated XML as the template. These examples have
XML/contract checks, not a guarantee of every IDE version's GUI behavior.

For sharing, commit only the intended run-configuration file if your repository
allows it; do not share the entire `.idea/workspace.xml` or private IDE settings.
Keep machine-specific serial ports and paths local. If `.idea` is ignored, the
run configuration stays local too unless you deliberately adjust that policy.

See also JetBrains' [External Tools guide](https://www.jetbrains.com/help/idea/configuring-third-party-tools.html)
and [shared run configurations](https://www.jetbrains.com/help/idea/run-debug-configuration.html#share-configurations).

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
