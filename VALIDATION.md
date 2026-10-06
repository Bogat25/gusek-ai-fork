# GUSEK AI release validation

Date: 2026-10-05. Reviewed baseline: `b46d8b05d11801410a41001d0031a4d02b0bee7f`, plus the changes in this commit.

The local release checks pass on an unelevated Windows 11 Pro x64 session
(OS build 10.0.26200). Version 0.1.1 installer and portable packages are built.
The GitHub release workflow is prepared locally; no GitHub workflow was
triggered and no release was published.

## Changes and evaluation

The previous worker, history, Unicode insertion and installer preservation
fixes remain covered by regression tests. This pass completes the attachment
strip, real transcript thumbnails, full-size picture viewer, separate reader
download and restart, background model warm-up, disabled-AI behavior,
configurable shortcut and keyboard navigation.

An incremental build defect was found during shutdown validation: the host
could keep an old allocation size after the pane headers changed. The makefile
now rebuilds host translation units that consume those headers. Both the debug
heap check that exposed the defect and the subsequent release GUI shutdown
checks pass. Manifest changes also trigger resource recompilation. Build
scripts explicitly select the release runtime and replace an older build
mirror before changing its compiler mode.

Configuration, model launch, image files, downloads and course lookup use
Unicode Windows APIs. Editable per-user prompts and course notes are read for
each question. Course search excludes README scaffolding, oversized files and
reparse points, bounds input reads, and accounts for excerpt headers and
omission markers within its total budget. It searches designated context
folders rather than every neighboring document.

Picture requests retain the four newest images across the question and
history; discarded images leave a marker. The attachment strip allows
individual removal and warns when more than four pictures are attached.
Five file formats, EXIF orientation, dimension limits, JPEG fallback, embedded
PNG transcript data, viewer ownership, Unicode streaming, emoji font selection,
scroll position and selection preservation are checked in actual controls.

Downloads verify complete files before installation, reject incorrect range
responses, restart when a server ignores Range, and recover from a corrupt
complete partial file. Owned runtime launches verify model hashes, respect
autostart=no and use the dedicated loopback endpoint. Runtime model lookup
uses the application's own profile or an explicit path. Build and test tools
may reuse verified, public model caches; normal application use does not
implicitly select RGui or RStudio cache folders.

The model and projector URLs now name immutable upstream revision
`e87f176479d0855a907a41277aca2f8ee7a09523`. Its public file metadata matches both
configured SHA-256 pins and byte sizes. The runtime remains pinned to
llama.cpp b11153. Inno Setup 7.1.0 is downloaded with its SHA-256 pin and
installed into the build folder for the current user. The packages include
usage instructions, GPL text and the relevant Scintilla, SciTE, llama.cpp
and LLVM OpenMP notices.

## Validation results

| Suite | Result | Evidence |
|---|---|---|
| Fresh native tests | 87/87 passed | Protocol, Unicode, configuration, bounded context, downloads, pictures and actual RichEdit behavior |
| Build and release helpers | Passed | BuildRoot forwarding, exit codes, Windows version metadata, valid/invalid tags, prereleases, manual versions and unsafe-root rejection |
| Actual staged GUI | 65 checks passed | Controls, focus, Tab/Enter, shortcut label, streaming/error recovery, history, attachments, reader-only installation, download recovery, solver attachment and shutdown |
| Actual installed GUI | 65 checks passed | Same regressions under the default per-user installation path containing spaces |
| Actual portable GUI | 65 checks passed | Same regressions after extracting the release ZIP to a new folder containing spaces |
| Installer assertions | 50 passed | Metadata, checksums, asInvoker manifests, default user destination, HKCU registration, no HKLM registration, payload, upgrade and uninstall preservation |
| Packaged launchers | 4 checks passed | Actual Start menu shortcut and portable command launcher open their own app and exit cleanly |
| Real CPU text and vision | 10 checks passed | Background warm-up, generated MathProg, GLPK solving, reader-only download/restart, picture recognition, image history and owned runtime cleanup |
| Workflow/static checks | Passed | PowerShell parsing, actionlint, release publisher Bash syntax and git diff --check |

The installer suite compiles the same packaging source with a unique AppId,
application/shortcut name and isolated assistant data directory. It installs
without a /DIR override into LocalAppData Programs, runs the app, upgrades it
and uninstalls it. The caller's administrator role was false. Setup, application
and uninstaller manifests request asInvoker; the normal production installer
was compiled and inspected, while the test identity was used for installation.

Upgrade preserves customized shipped defaults and all model fixtures.
Uninstall removes only the named default model assets and their partial files.
Custom models, coursework, user settings, prompts and saved-history fixture
files survive. Conversation persistence is not implemented: normal chat
history stays in memory.

The real CPU test used the pinned base model and projector through the actual
GUSEK pane. Its generated model solved with objective 5 in GLPK. Starting from
a text-only server, the test installed the reader through the reader-only flow,
verified that the old owned server stopped, and restarted inference with vision.
The model read 42 from a picture whose filename did not reveal the number, then
answered 21 to a text-only follow-up asking for half of it. Closing the app
during another request returned exit code 0 and closed its owned server port.
This validates those responses, not every possible generated model.

## Release workflow

`.github/workflows/windows-installer.yml` validates pushed version tags such as
v0.1.1 and v0.2.0-rc1. A Windows build job with read-only repository permissions
fetches pinned tools, builds, runs native/GUI checks, creates both packages and
checks installation, upgrade, uninstall and portable relocation. Models are
excluded. Pinned action revisions are used and checkout credentials are not
persisted.

A separate Ubuntu job runs only for tag push events, verifies package checksums,
and uploads the installer, ZIP and checksum files into a draft release before
publishing it. Prereleases are marked appropriately. Manual runs only produce
Actions artifacts. Existing releases are not overwritten.

## GitHub runner correction (2026-10-06)

The owner reported a failed hosted fetch step: the runner's existing ISCC.exe
rejected --version. Windows PowerShell's ErrorAction=Stop promoted native stderr
to NativeCommandError, aborting before the pinned compiler could be installed.
The local release checks used Inno Setup 7.1.0 and missed this older-compiler path.

Get-InnoSetupVersion now handles rejected probes, validates the exit code and
version response, and allows Install-InnoSetup to fall back to the verified
7.1.0 installer. A newly installed compiler is also checked before reuse.
Build regressions cover unsupported options, failed commands with plausible
version output, malformed output and an older version, including the pinned
installer fallback. Those checks passed under Windows PowerShell 5.1.

The original NativeCommandError was reproduced with a native command fixture
that rejects --version. The actual gusek.cmd fetch -NoModel command then passed
with that candidate and a fresh BuildRoot: it installed the real pinned compiler
from its SHA-verified installer cache. Repeating fetch passed using the resulting
7.1.0 compiler cache. No multi-gigabyte model download was needed. Evidence:
D:\gusek-ci-fix-dbe58ca304c96\fetch-with-old-compiler.log and
D:\gusek-ci-fix-dbe58ca304c96\fetch-with-cached-compiler.log.
The fixture simulates the runner failure; an actual Inno Setup 6 compiler and a
new hosted workflow run were not exercised locally. The application binaries
and distribution hashes below are unchanged by this build-script correction.

## Artifacts and local evidence

Build root: `D:\gusek-production-ceb842debe7b4491a332668cacaae638`.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| gusek-0.1.1-portable.zip | 21,738,446 | `a2a6cc4e75f1672a48f632b92aca8954111d0e585ed85d09d5367146b3e7ff63` |
| gusek-ai-0.1.1-setup.exe | 13,695,718 | `961cb3903526445f4ebbbca3cf99856ee462f4969a3c85e6450bfbe36bf18428` |

Artifacts and adjacent ASCII .sha256 files are in BuildRoot\dist.
Installer file version is 0.1.1.0. The ZIP contains 298 entries, zero GGUF
files and no test or work-profile directories.

The final distribution suite returned exit code 0:
`logs\production-distribution-final.log`, with fixtures under
`tests\run-03385667b236405f8fe5aadb5ef5830f`.
Real inference evidence is in `logs\production-tests-release.log` and
`tests\run-d496db08889d4a75baf00656f9c33e04`, including the generated model,
GLPK output, vision transcript and local runtime logs. Build/package output is
in `logs\production-package.log` and `logs\production-installer.log`.
Release-helper rejection cases are in `logs\release-build-guards.log`.
These machine-local evidence paths and binaries are excluded from Git.

## Reproduce

Use Windows x64, MSVC C++ tools and Python 3. Choose an absolute writable
BuildRoot without spaces, outside the repository.

```powershell
.\gusek.cmd fetch -NoModel -BuildRoot D:\gusek-build
.\gusek.cmd full -BuildRoot D:\gusek-build
.\gusek.cmd package -Version 0.1.1 -BuildRoot D:\gusek-build
.\gusek.cmd installer -Version 0.1.1 -BuildRoot D:\gusek-build
.\gusek.cmd test -Installer -BuildRoot D:\gusek-build
```

For CPU text and vision validation, run fetch without -NoModel, then test
-Real -Installer. Requested suites fail if prerequisites are missing.
The default test command includes fresh native and actual GUI checks.
See AI-README.md for use, configuration, build and release instructions.

## Limits

The packages are unsigned; Windows reputation checks can show a warning.
Signing requires a separately provisioned certificate and is not configured.
A successful GitHub-hosted run remains unverified; the reported first run failed
in compiler detection, as recorded above. GitHub access was kept read-only.
The hosted workflow runs the native and simulated-backend GUI
suites, without downloading multi-gigabyte models; real CPU text/vision was
validated locally.

Windows 10, ARM64 emulation, GPU execution, diverse clipboard applications and
a comprehensive manual accessibility/visual review were not tested. The
installer targets Windows 10 1903+; the measured results here are for Windows
11 x64. Generated models still need user review and explicit solver validation.
