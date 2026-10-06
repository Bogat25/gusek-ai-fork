# GUSEK local assistant

The Windows installer installs for the current user under
`%LOCALAPPDATA%\Programs\GUSEK AI`. It requests no administrator elevation.
The tested platform is Windows 11 x64; the installer targets Windows 10 1903
and later. The installer is unsigned, so Windows may show a reputation warning.

Open **Tools > AI assistant**, or press **Ctrl+Shift+T**. Write a question and
press **Send** or **Ctrl+Enter** in the question box. Opening the pane warms an
installed model in the background. **Stop** cancels loading, an answer or a
download; **New chat** also clears the conversation.

On first use, accept the model download when offered. The default base model
and picture reader together require about 3.4 GB. Downloads resume after a
pause and must match the configured SHA-256 before use. Chat runs through a
local CPU server on `127.0.0.1:28713`, independently of RGui and RStudio. After
the runtime and models are installed, normal inference works offline.

Use **Attach...** for the active model, solver output, recent output, picture
files or clipboard pictures. PNG, JPEG, BMP, GIF and TIFF files are supported.
Click an attachment preview to open a larger viewer, and press **Esc** to close
it. Its **x** removes that attachment. Sent pictures also have transcript
thumbnails; double-click a thumbnail to open the viewer. Only the four newest
pictures are included in a request, across the current question and history.
If the picture reader is missing, GUSEK offers its separate download; declining
it still allows text chat after removing the attachments.

**Copy code** copies a fenced code block. **To editor** inserts it on request;
characters that do not fit the existing document encoding open in a new UTF-8
document when possible. Review and run the model yourself. Generated code is
never executed automatically. Conversation history stays in memory and clears
with **New chat** or when GUSEK closes.

## Settings and course notes

Per-user assistant data lives in `%LOCALAPPDATA%\GusekAI`:

- `GusekAI.ini`: overrides the shipped `ai\defaults\GusekAI.ini` settings.
- `system_prompt.txt`: your editable instructions, read for each question.
- `context`: your course notes, read for each question.
- `models`: the verified base model, reader and resumable partial downloads.
- `logs\llama-server.log`: local runtime diagnostics.

Course lookup also reads `.ai-context` next to the active document and the
configured `context_dir`. It reads supported text/model files, excludes README
scaffolding and files over 256 KiB, and bounds the total included excerpt.
It does not scan every file beside the document.

Restart GUSEK after editing `GusekAI.ini`. Set `enabled=no` to remove the pane
and menu, `hotkey=K` for Ctrl+Shift+K, or `autostart=no` to require an already
running local server. An explicit `GUSEK_AI_DATA` environment variable selects
another assistant data directory. Uninstall removes the app and the named
default model downloads; custom models, course notes, settings and prompts stay.

For a portable installation, extract the ZIP to a writable folder and run
`Start-Gusek.cmd`. Its assistant data and editor settings live under that
folder's `work` directory and can travel with it.

## Build and verify

Install MSVC C++ tools and Python 3. Choose an absolute, writable build folder
without spaces, outside the repository. The repository and installed app can
have spaces in their paths.
Builds and native tests share compiler discovery through Visual Studio's
vswhere, including Enterprise, Community and Build Tools installations.
Use -VcVars with a full vcvarsall.bat path to override build discovery.

```powershell
.\gusek.cmd doctor -BuildRoot C:\gusek-build
.\gusek.cmd fetch -NoModel -BuildRoot C:\gusek-build
.\gusek.cmd full -BuildRoot C:\gusek-build
.\gusek.cmd test -BuildRoot C:\gusek-build
.\gusek.cmd package -Version 0.1.1 -BuildRoot C:\gusek-build
.\gusek.cmd installer -Version 0.1.1 -BuildRoot C:\gusek-build
.\gusek.cmd test -Installer -BuildRoot C:\gusek-build
```

`fetch -NoModel` retrieves the pinned CPU runtime and installs the pinned Inno
Setup compiler for the current user within the build folder. To include real
CPU text and picture inference in validation, first run `fetch` without
`-NoModel`, then `test -Real -Installer`. Tests use isolated profiles and an
installer identity separate from an existing GUSEK installation. Evidence stays
in `BuildRoot\tests\run-*`. Requested checks fail when their prerequisites are
missing. The repository's `VALIDATION.md` records the measured results.
The installer suite also relocates the portable ZIP to a folder containing
spaces, starts its launcher and exercises its GUI.

`run` uses a separate build-folder development profile. `quick` builds the
editor incrementally after a full build. Builds explicitly use the release
runtime even when the caller has a `DEBUG` environment variable.

## Installer release workflow

The checked-in `.github/workflows/windows-installer.yml` runs when the owner
pushes a version tag such as `v0.1.1` or `v0.2.0-rc1`. It validates the tag,
fetches pinned tools, builds the editor, runs native and GUI regressions,
creates both packages and checks installation, upgrade and uninstall. Models
are downloaded on first use and are excluded from the release packages.

After those checks, a separate job verifies the package checksums and creates
a GitHub release with the installer, portable ZIP and their `.sha256` files.
Prerelease tags create prereleases. Manual workflow runs produce downloadable
Actions artifacts without publishing a release. The publisher creates a draft
and uploads the complete asset set before publishing it. Existing releases are
not overwritten; an interrupted draft can require owner cleanup before retry.

Creating a tag requires the workflow to be committed at that tag. Local
development and validation do not push tags or publish releases.
