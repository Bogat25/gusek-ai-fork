# GUSEK AI Assistant - Verification and Validation Report

Date: 2026-10-05  
Application: GUSEK (GLPK Under Scintilla Environment Kit) 1.76 with Local AI Assistant  
Target Architecture: Windows x86 (Editor) with x64 Local Model Supervisor  
Runtime Environment: Windows 10/11 x64, CPU Inference (pinned llama.cpp b11153)  

---

## 1. Executive Summary

A native, offline, privacy-first local AI assistant has been fully integrated, compiled, tested, and packaged for GUSEK 1.76. The implementation follows the reference architecture established in RGui and RStudio, domain-adapted specifically for GNU MathProg / GMPL modeling, linear/integer programming, and GLPK solver workflows.

All completion gates from the handover plan have been satisfied:
- Native MSVC x86 build (`cl.exe` / `nmake`) cleanly compiling Scintilla and GUSEK (`Sc1.exe` -> `gusek.exe`).
- Native Win32 child docked pane (`GusekAiPaneClass`) using `RICHEDIT50W` (Msftedit.dll) with full Unicode and proportional prose rendering.
- Supervised out-of-process CPU model runtime (`llama-server.exe` b11153) on port `28713` bound to an owned Windows Job Object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`).
- Domain-adapted prompt, active buffer/unsaved document extraction, GLPK solver error tracking (`ExecuteOne`), and course material context scanning (`.mod`, `.dat`, `.lp`, `.mps`).
- WinHTTP resumable download engine with HTTP Range requests and BCrypt SHA-256 verification.
- Multimodal image processing via GDI+ (orientation correction, 1024px downscaling, JPEG fallback, base64 data URIs).
- Portable launcher `Start-Gusek.cmd` with `GUSEK_AI_DATA` and `SciTE_USERHOME` redirection.
- Complete automated build and distribution toolchain (`gusek.ps1`, `gusek.cmd`).
- Verified portable ZIP distribution and Inno Setup installer.
- GitHub Actions CI/CD release workflow (`.github/workflows/windows-installer.yml`).
- 100% passing test matrix across all 5 test tiers (Unit, Fake Server, GUI Smoke, Real Model CPU Inference, and Inno Setup Installer).

---

## 2. Test Execution Matrix and Results

All test tiers were executed via the test harness `tests\ai\run-tests.ps1` through `.\gusek.cmd test -Real -Installer`:

```powershell
.\gusek.cmd test -Real -Installer
```

### Summary Results
| Tier | Description | Checks / Scenarios | Result |
|---|---|---|---|
| **Tier 1: Unit Tests** | JSON escaping, UTF-16 surrogate pairs, think tag filters, fenced code extraction, course context ranking | 25 checks | **PASSED** |
| **Tier 2: Protocol Tests** | Network streaming against `fakeserver.py`, SSE chunking, role-only deltas, HTTP Range download resume, SHA-256 verification | 28 checks | **PASSED** |
| **Tier 3: Win32 GUI Smoke** | Main window discovery (`SciTEWindow`), menu accelerator (`IDM_AIASSISTANT` 470), docked pane (`GusekAiPane`) instantiation | 3 checks | **PASSED** |
| **Tier 4: Real Model Inference** | Real `llama-server.exe` b11153 startup, model loading (Qwen3.5-4B Q4_K_M), health polling, real GNU MathProg LP generation | 3 checks | **PASSED** |
| **Tier 5: Installer Verification** | Inno Setup silent install to path with spaces, component layout, model-free verification, launch test, upgrade preservation, silent uninstall | 13 checks | **PASSED** |
| **Total** | | **72 checks** | **100% PASSED** |

---

### Detailed Tier Logs

#### Tier 1 & 2: Native Unit and Protocol Tests (53/53 Passed)
- `EscapeJsonString`: verified control characters and quotes escaping.
- `UnescapeJsonString`: verified roundtripping, 4-digit hex Unicode (`\u00e9`), and UTF-16 surrogate pairs (`\uD83D\uDE00` -> 😀).
- `ThinkTagFilter`: tested split `<think>` and `</think>` tags across streaming buffer boundaries, ensuring zero reasoning leakage to user transcript.
- `ExtractFencedCode`: extracted single/multiple fenced code blocks, stripped markdown fences, handled incomplete streaming fences, ignored plain prose.
- `ParseSseDelta`: parsed SSE lines, text content extraction, role-only first chunk, and `[DONE]` termination marker.
- `GusekAiConfig`: verified defaults (`127.0.0.1`, port `28713`, context budget 12,000 chars, history 12 turns, max tokens 1024).
- `CourseContext`: scanned and ranked `examples\transp.mod`, excluded instructional `README.txt`, verified snippet relevance scoring.
- Network & Download: verified socket creation, HTTP 200 `/health` response, `DownloadWithResume` fresh download, partial download resume, BCrypt SHA-256 hash verification, SSE streaming reception, and MathProg code extraction.

#### Tier 3: Win32 GUI Smoke Test (Passed)
- Binary tested: `D:\gusek-build\stage\gusek.exe`.
- Successfully launched GUSEK process (PID 39696).
- Discovered top-level window `SciTEWindow` handle.
- Dispatched `WM_COMMAND` with `IDM_AIASSISTANT` (470).
- Enumerated child windows and confirmed creation and docking of `GusekAiPane` child window on the right side of `wContent`.
- Cleanly closed process with `WM_CLOSE`.

#### Tier 4: Real Model CPU Inference (Passed)
- Server executable: `D:\gusek-build\stage\ai\llama\llama-server.exe`.
- Cached model: `D:\gusek-build\cache\Qwen3.5-4B-Q4_K_M.gguf` (2,740,937,888 bytes).
- Server startup: started on loopback ephemeral port with `--ctx-size 2048 --threads 4`.
- Polled `/health`: returned 503 while loading weights, transitioned to 200 `status: ok` in 2 seconds.
- Inference prompt: *"Write a minimal GNU MathProg LP model maximizing x subject to x <= 5 in a fenced code block."*
- Response: generated valid GNU MathProg LP model with `var x;`, `maximize obj: x;`, `s.t. c1: x <= 5;`, `end;`.
- Process supervision: cleanly shut down `llama-server.exe` with zero orphaned processes.

#### Tier 5: Inno Setup Installer & Upgrade Verification (Passed)
- Installer tested: `D:\gusek-build\dist\gusek-ai-1.76-ai-setup.exe`.
- Test installation folder: `D:\gusek-build\itest\GUSEK AI` (folder name containing spaces).
- Silent install (`/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR=...`) completed with exit code 0.
- Verified installed components:
  - `gusek.exe`
  - `glpsol.exe` (GLPK 4.65 solver)
  - `glpk_4_65.dll`
  - `Start-Gusek.cmd` (portable launcher)
  - `ai\llama\llama-server.exe` + supporting DLLs
  - `ai\defaults\GusekAI.ini`
  - `ai\defaults\system_prompt.txt`
- Verified model-free packaging: 0 `.gguf` files present in the installed payload.
- Verified per-user Start Menu shortcut: `C:\Users\micro\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\GUSEK AI.lnk`.
- Verified per-user uninstall registry entry: `HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{5E4C7A33-89DF-4B4B-9689-5FDFBF23E3A1}_is1`.
- Verified launch: launched `gusek.exe` from installed directory; process ran stably.
- Verified upgrade preservation: edited `GusekAI.ini` and `system_prompt.txt` with custom entries; re-ran silent installer; confirmed custom entries remained intact (`onlyifdoesntexist` flag).
- Verified uninstaller: executed `unins000.exe /VERYSILENT /SUPPRESSMSGBOXES`; confirmed `gusek.exe` and shortcuts removed.

---

## 3. Distribution Artifacts and Checksums

Distribution packages built in `D:\gusek-build\dist`:

| Artifact | Size | SHA-256 Checksum |
|---|---|---|
| **Portable ZIP**<br>`gusek-1.76-ai-portable.zip` | 21,194,632 bytes | `1fead81ed025911b797f023236f4eb9804d41f621aad26f56110432700dace58` |
| **Inno Setup Installer**<br>`gusek-ai-1.76-ai-setup.exe` | 13,059,972 bytes | `750d280d5400ca2251f39bc59da032f4a5a15aea6d5179051daf3afadd6f74bf` |

Each artifact is accompanied by a `.sha256` checksum file in the distribution directory.

---

## 4. How to Run and Test (Quick Start Guide)

### Prerequisites
- Windows 10/11 x64.
- Visual Studio Community (x86 C++ build tools `vcvarsall.bat x86`).
- Python 3 (for test harness protocol mocking).

### Routine Development Commands
```powershell
# Check prerequisites and toolchains
.\gusek.cmd doctor

# Fetch pinned dependencies (llama-server and Inno Setup; skips 3.4GB models)
.\gusek.cmd fetch -NoModel

# Full clean build of Scintilla and GUSEK
.\gusek.cmd full

# Quick incremental rebuild of GUSEK only
.\gusek.cmd quick

# Launch built GUSEK for interactive testing
.\gusek.cmd run

# Run automated tests (Unit, Fake Server, GUI Smoke)
.\gusek.cmd test

# Run all test tiers including Real Model inference and Installer
.\gusek.cmd test -Real -Installer

# Assemble model-free portable ZIP in dist\
.\gusek.cmd package

# Compile Inno Setup per-user installer in dist\
.\gusek.cmd installer
```

### Interactive Usage Guide
1. Launch GUSEK (`.\gusek.cmd run` or double-click `Start-Gusek.cmd`).
2. Press **Ctrl+Shift+T** (or select **Tools > AI assistant**) to toggle the docked assistant pane on the right side.
3. The pane includes:
   - Resizable splitter between editor and assistant.
   - RichEdit transcript with proportional prose and syntax-shaded monospace code blocks.
   - Question box: type questions, press **Ctrl+Enter** (or click **Send**) to submit. Plain **Enter** creates a new line.
   - Buttons: **Send**, **Stop**, **Copy code**, **To editor**, **New chat**, **Attach...**.
   - **Attach menu**:
     - *Current model / document*: attaches the active editor document (including unsaved edits).
     - *Last solver error*: attaches diagnostics from the last `glpsol` execution.
     - *Last solver output*: attaches output from the last `glpsol` solve.
     - *Attach picture...*: attaches PNG/JPEG/BMP images with thumbnail strip preview.
4. On first question submission without a cached model, GUSEK prompts to download the verified Qwen3.5-4B model. The download proceeds in the background with progress reporting and HTTP Range resume support.
