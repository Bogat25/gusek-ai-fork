# GUSEK AI

**A native Windows MathProg/GLPK editor with a local AI assistant for optimization models, solver output, pictures, and reference notes.**

This repository is an independent **hard fork** of GUSEK and its bundled
SciTE/Scintilla source. Local AI is the main development goal. Long-term
synchronization with the original repositories is not planned; this fork
maintains its own fixes, dependencies, and releases. It is not an official
GUSEK, GLPK, SciTE, or Scintilla release. Original notices and licenses remain.

## What it adds

- A native assistant pane beside the editor and solver output, with streaming
  formatted Unicode answers and cancellation.
- Explicit attachments for the active unsaved model, solver/recent output,
  picture files, and clipboard screenshots.
- Local reference-note lookup using bounded keyword-ranked excerpts. This is
  document lookup on your computer, not web search or model training.
- Picture previews, individual attachment removal, and image follow-ups with
  up to four recent pictures per request.
- **Copy code** and **To editor** actions. Generated MathProg is never executed
  automatically; you review it and run GLPK yourself.
- Per-user installation without administrator permissions, plus a portable ZIP.

The default assistant runs **Qwen3.5-4B Q4_K_M** and its multimodal projector
through **llama.cpp on the CPU**. The packaged application requires no cloud AI
account, API key, GPU, Node, or Python installation for normal use. Initial
model downloads need Internet access; inference works offline afterward.

## Install and start

1. Download `gusek-ai-<version>-setup.exe` or
   `gusek-<version>-portable.zip` from a successful release in this repository,
   when available. Compare its SHA-256 against the adjacent checksum file.
2. Run the installer, or extract the portable ZIP into a writable folder and
   start `Start-Gusek.cmd`. The installer installs for the current user.
3. Open **Tools > AI assistant**, or press **Ctrl+Shift+T**.
4. Accept the initial model/picture-reader download, about **3.4 GB** combined.
5. Enter a question and select **Send** or **Ctrl+Enter**. Use **Stop** to cancel
   loading, generation, or downloading.

Windows 11 x64 is the tested release environment. The installer targets Windows
10 1903+; broader platform coverage is not established. Packages are unsigned.
The legacy `gusek.exe` retained at the source root predates the assistant; use a
fresh fork build or release package for AI features.

**Current validation limit:** the screenshot-paste correction is implemented,
but its new end-to-end clipboard checks still need a successful rerun after
Windows denied clipboard access in the validation session. Earlier file-image
inference passed. See [VALIDATION.md](VALIDATION.md) for exact evidence and limits.

## Reference notes and settings

Installed assistant data defaults to `%LOCALAPPDATA%\GusekAI`: models,
`GusekAI.ini`, `system_prompt.txt`, `context`, and runtime logs. Portable use
places assistant data under `work/assistant` and editor settings under
`work/config`. A `.ai-context` directory beside the active document can provide
project references. Keep private notes, models, and chat data out of Git.
The [assistant guide](AI-README.md) explains settings and attachment behavior.

## Build from source

Install Visual Studio C++ tools and Python 3, then run from this checkout:

```powershell
.\gusek.cmd doctor -BuildRoot C:\gusek-build
.\gusek.cmd fetch -NoModel -BuildRoot C:\gusek-build
.\gusek.cmd full -BuildRoot C:\gusek-build
.\gusek.cmd test -BuildRoot C:\gusek-build
.\gusek.cmd package -Version 0.1.0 -BuildRoot C:\gusek-build
.\gusek.cmd installer -Version 0.1.0 -BuildRoot C:\gusek-build
```

`0.1.0` is an example fork version. BuildRoot must be a writable absolute path
outside the checkout, without spaces. MSVC discovery supports installed editions
through vswhere; `-VcVars` is an explicit override. Omit `-NoModel` when fetching
to prepare real inference tests. Models are excluded from release packages.

## Documentation

| Guide | Contents |
| --- | --- |
| [Documentation index](docs/README.md) | User and maintainer entry points |
| [Assistant guide](AI-README.md) | Chat, pictures, notes, configuration, data, build overview |
| [Build and tests](docs/BUILDING.md) | Compiler, native/GUI/real suites, packaging |
| [Architecture](docs/ARCHITECTURE.md) | SciTE integration, native modules, request/data ownership |
| [Releases](docs/RELEASING.md) | Version tags, installer/ZIP artifacts, checksums |
| [Validation](VALIDATION.md) | Measured results, clipboard blocker, unsupported cases |
| [Privacy audit](docs/PRIVACY-AUDIT.md) | Redacted findings and history limitations |
| [Contributing](CONTRIBUTING.md) / [Security](SECURITY.md) | Reports, development, sensitive data |

The original [GUSEK README](README), [GUSEK help](gusek.html),
[MathProg manual](gmpl.pdf), and [examples](examples) are retained. Plans under
`handover` are historical implementation records; current guides and validation
take precedence over their early proposed design.

## License and origins

GUSEK and the bundled GLPK solver use **GPL-3.0-or-later**; see [COPYING](COPYING).
SciTE, Scintilla, llama.cpp, runtime dependencies, and downloaded models retain
their own terms. [THIRD-PARTY.md](THIRD-PARTY.md) records provenance and notice
locations. This fork does not replace or remove original attribution.
