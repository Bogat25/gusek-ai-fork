# Building and testing GUSEK AI

## Prerequisites and first build

Use Windows x64, Visual Studio C++ tools with a Windows SDK, and Python 3 for
tests. The compiler environment is discovered through vswhere across supported
installed editions, with standard-path fallbacks. `-VcVars` accepts a full
vcvarsall.bat path when automatic discovery is unsuitable.

Choose an absolute writable BuildRoot outside the checkout with no spaces.
Source checkout and installed application paths may contain spaces. The wrapper
mirrors source into BuildRoot before invoking the native makefiles.

```powershell
.\gusek.cmd doctor -BuildRoot C:\gusek-build
.\gusek.cmd fetch -NoModel -BuildRoot C:\gusek-build
.\gusek.cmd full -BuildRoot C:\gusek-build
```

`fetch -NoModel` downloads the pinned CPU runtime and per-user Inno Setup compiler
with integrity verification. Without `-NoModel`, it also prepares the base model
and projector. Pins are in [gusek.ps1](../gusek.ps1) and the
[default configuration](../ai/defaults/GusekAI.ini).

The full build compiles Scintilla and SciTE from this repository. No untracked
sibling source directory or checked-in generated Scintilla DLL is required.
The release uses the freshly built static editor executable; the inherited
root `gusek.exe` is a legacy payload, not the AI build output.

## Edit/build loop

`gusek.cmd quick` rebuilds incrementally after a full build. `dev` rebuilds and
runs the application; `run` starts it using a separate build-folder development
profile. Builds explicitly select the release runtime, regardless of an inherited
`DEBUG` variable. Keep the same BuildRoot and compiler override across commands.

## Tests

```powershell
.\gusek.cmd test -BuildRoot C:\gusek-build
.\gusek.cmd test -Real -BuildRoot C:\gusek-build
.\gusek.cmd test -Installer -BuildRoot C:\gusek-build
```

| Suite | What it exercises |
| --- | --- |
| Native | JSON/SSE, configuration, context bounds, image encoding/history, Unicode and real RichEdit control behavior |
| Build/release helpers | Compiler discovery, version/tag conversion, root/path validation, wrapper forwarding and errors |
| GUI (default test command) | Actual staged pane, attachments, simulated downloads/inference, rendering, keyboard behavior and shutdown |
| `-Real` | Actual CPU generation, GLPK validation of generated MathProg, picture inference and follow-up context |
| `-Installer` | Isolated per-user install, GUI, upgrade, uninstall, launchers and portable relocation |

Requested suites fail when prerequisites are missing. Do not interpret a skipped,
blocked, or fake-backend run as real inference success. The current clipboard
limitation is recorded in [VALIDATION.md](../VALIDATION.md).

Tests use synthetic content, separate profiles, and separate installer identities.
They manipulate real windows and the clipboard; use a disposable desktop and
leave test windows alone. Do not capture or publish the user's real clipboard.
Evidence is retained in `BuildRoot/tests/run-*` and build logs under `logs`.

## Packaging

```powershell
.\gusek.cmd package -Version 0.1.0 -BuildRoot C:\gusek-build
.\gusek.cmd installer -Version 0.1.0 -BuildRoot C:\gusek-build
```

These produce `dist/gusek-<version>-portable.zip` and
`dist/gusek-ai-<version>-setup.exe`, with adjacent `.sha256` files. The payload
includes the built editor, solver assets, CPU runtime, defaults, documentation,
and licenses. Model weights, partial downloads, work profiles, and test logs
are excluded. Build current source before packaging.

The installer uses `PrivilegesRequired=lowest`. Its identity is distinct from
the other forks. Portable use requires `Start-Gusek.cmd`; installed shortcuts
start the per-user application directly. See [AI-README.md](../AI-README.md)
for the different data locations and upgrade/uninstall preservation rules.
