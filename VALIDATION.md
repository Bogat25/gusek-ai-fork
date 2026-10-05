# GUSEK AI validation

Date: 2026-10-05. Reviewed baseline: `574af4f9c3485e95693e463166d5f02fdcc0138c`.

The reviewed chat, build-wrapper and installer defects have been fixed and tested locally. This report covers those fixes; it does not certify every feature in the original integration plan.

## Fixes verified

- First-run model download: declining, pausing, resuming, failing verification and completing a download leave the controls usable. A completed download supports inference without restarting GUSEK.
- Streaming: WinHTTP decodes HTTP chunks before SSE parsing, including records and Unicode characters split across chunks. HTTP and stream failures remain visible after the controls recover.
- Conversation: successful user/assistant pairs are retained within the configured message limit and included in follow-up requests. New chat clears history and discards old worker messages.
- Worker lifetime: Stop, New chat and shutdown signal cancellation, join owned request/download workers and finish asynchronous HTTP callbacks before releasing pane state. Closing GUSEK also stops its owned model server.
- To editor: conversion to an existing legacy encoding must be lossless. Otherwise the code opens in a new UTF-8 document when a tab is available. When no tab is available, the existing document and its encoding are preserved and insertion failure is reported.
- Solver output: job-thread writes and assistant reads share the existing job mutex. The GUI suite exercises attachment while GLPK produces output and a model request remains active.
- Build/test wrapper: the requested BuildRoot reaches the harness, failures propagate to the caller, and native tests are freshly compiled. GUI tests require the actual staged application and controls. Source synchronization excludes local build products, and shared AI header changes trigger recompilation.
- Portable profile: SciTE_USERHOME now takes precedence for user settings, matching Start-Gusek.cmd.
- Installer: uninstall deletes only the two named default model assets and their partial files. Custom models, coursework, settings, prompts and saved-history files survive. Windows installer version metadata derives from the supplied version.

The default MathProg prompt also states declaration order explicitly. In the real inference test, generated code was sent to the editor and separately checked with GLPK; it solved successfully with an objective value of 5. This is evidence for that response, not a guarantee that every model response will be valid.

## Local validation

The editor was freshly built with MSVC x86. CPU inference used the SHA-256-pinned x64 llama.cpp b11153 runtime and Qwen3.5-4B-Q4_K_M model. Test profiles, temporary files and installer data were isolated under a new build directory.

Full test command:

```powershell
.\gusek.cmd test -Real -Installer -BuildRoot D:\gusek-fixes-28308006dbef4a359b07fbdae40ae4db
```

The wrapper returned exit code 0. All requested suites passed:

| Suite | Result | Scope |
|---|---|---|
| Fresh native tests | 53/53 | JSON, Unicode, think filtering, code extraction, SSE, config, course context, download and verification |
| Build-wrapper regressions | Passed | BuildRoot forwarding, failure/success exit codes, valid and invalid Windows version conversion |
| Actual staged GUSEK GUI | 35 checks passed | History, split chunked Unicode, HTTP error recovery, Stop, New chat, first-run download, Unicode insertion, full tabs, concurrent solver output and shutdown |
| Real CPU inference through GUSEK | 5 checks passed | Actual pane, generated MathProg, GLPK validation, shutdown during a follow-up and owned server cleanup |
| Installer regressions | 34 checks passed | Metadata, checksum, components, model-free payload, upgrade preservation and uninstall ownership |
| Actual installed GUSEK GUI | 35 checks passed | Same application regressions from an installation path containing spaces |
| Extracted portable GUSEK GUI | 35 checks passed | Same regressions after relocating the ZIP to a path containing spaces |

Installer tests compile the same packaging source with a unique AppId, shortcut name and explicit isolated data directory. They install, upgrade and uninstall that test identity. The normal production installer was compiled and inspected, but was not installed into the user's profile.

PowerShell parsing, actionlint for the existing tag workflow, and git diff --check also passed. The portable ZIP contains 292 entries, the model runtime and launcher, and zero GGUF files.

Evidence on this machine:

- Build/package logs: D:\gusek-fixes-28308006dbef4a359b07fbdae40ae4db\logs (final-build.log, package-command.log, installer-command.log).
- Full suite log: D:\gusek-fixes-28308006dbef4a359b07fbdae40ae4db\logs\all-tests-final.log.
- Test files, native logs, real-model transcript, generated model and GLPK output: D:\gusek-fixes-28308006dbef4a359b07fbdae40ae4db\tests\run-ba5720277575480ca8cb54d22f9f0f91.
- Portable relocation log: D:\gusek-fixes-28308006dbef4a359b07fbdae40ae4db\logs\portable-regressions.log.

These local evidence paths are not included in Git or distribution packages.

## Distribution artifacts

Both packages were created from the fixed source with version 0.1.0 under D:\gusek-fixes-28308006dbef4a359b07fbdae40ae4db\dist.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| gusek-0.1.0-portable.zip | 21,891,596 | `8946adbdf9cf5bf5ccadb006df52ab5a3a3e48699ebcf564c69eff3fa9fa0051` |
| gusek-ai-0.1.0-setup.exe | 13,759,217 | `224e7e68aa9079fc38504691a93b1e3f395cabfae0ae0afa0cbd8e04505b7d3f` |

Each has an adjacent .sha256 file. The installer reports Windows file version 0.1.0.0. The packages include the runtime and exclude the large models.

## Reproducing the checks

Use Windows x64, MSVC C++ tools and Python 3. Installer builds/tests also require Inno Setup. Choose an absolute BuildRoot without spaces; the repository, installed app and portable app may live in paths with spaces.

```powershell
.\gusek.cmd doctor -BuildRoot D:\gusek-build
.\gusek.cmd fetch -NoModel -BuildRoot D:\gusek-build
.\gusek.cmd full -BuildRoot D:\gusek-build
.\gusek.cmd test -BuildRoot D:\gusek-build
.\gusek.cmd package -Version 0.1.0 -BuildRoot D:\gusek-build
.\gusek.cmd installer -Version 0.1.0 -BuildRoot D:\gusek-build
.\gusek.cmd test -Installer -BuildRoot D:\gusek-build
```

fetch -NoModel downloads the pinned llama.cpp runtime, not the Inno Setup compiler or model files. Install Inno Setup separately or supply -InnoSetup when building the installer. Installer tests discover the compiler in the standard installation or supported cache locations; see Find-InnoSetup in gusek.ps1.

For real inference checks, run fetch without -NoModel first, or use an already cached pinned model. Then run:

```powershell
.\gusek.cmd test -Real -Installer -BuildRoot D:\gusek-build
```

The default test command always runs GUI regressions in addition to the native and wrapper checks. -Real and -Installer add their respective suites; requested suites fail if their prerequisites are missing. Tests preserve logs in a unique BuildRoot\tests\run-* directory.

## Remaining validation scope

Real vision inference, the complete picture attachment/thumbnail workflow, broader manual layout/accessibility review and a GitHub-hosted workflow execution were not validated here. No GitHub workflow was triggered and no release was published. Generated models still require review and solver validation before use.