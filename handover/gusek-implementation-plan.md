# GUSEK: implementation plan for the local AI assistant

Historical design record. Source import and implementation have since happened;
use [current documentation](../docs/README.md) and [validation](../VALIDATION.md)
for present behavior and remaining work. Paths below describe the original layout.

Checked on 2026-10-04. Read [ai-assistant-porting-guide.md](ai-assistant-porting-guide.md)
completely first. Its section 2 defines the behavior to reproduce; this plan
maps it to the actual GUSEK checkout and replaces R-specific attachments and
prompts with GNU MathProg equivalents. This is an implementation handover,
not a report that the feature has already been implemented.

## 1. Workspace and baseline

The repository to work in is the **gusek checkout**, branch
**main**, inspected at **f4d7456**. It currently contains the packaged application,
configuration, solver, manuals and examples. The editable application source
is in **../Gusek-fork**, which currently has no Git metadata. Do not build a
solution that depends permanently on that untracked sibling directory.

| Checked component | Current location and finding |
|---|---|
| Application | gusek.exe: PE machine 0x014c (x86), file version 1.76 g0.2 |
| Solver | glpsol.exe: x86, GLPK 4.65; version command and transp.mod check passed |
| Editor source | ../Gusek-fork/scite and ../Gusek-fork/scintilla; Scintilla version.txt is 176 |
| Build entry point | ../Gusek-fork/Build_Gusek_MinGW.bat; assumes C:/MinGW and GCC 3.4.5, replaces HOME, and can run UPX |
| Native makefiles | scite/win32/makefile and scite/win32/scite.mak; scintilla/win32/makefile and scintilla/win32/scintilla.mak |
| Runtime configuration | SciTEGlobal.properties imports gmpl.properties, gusek.lua and gnuplot.properties |
| Matching source/runtime assets | gusek.lua, gmpl.properties, SciTEGlobal.properties, gnuplot.properties and README match their copies in ../Gusek-fork/gusek |
| Reference implementation | ../RGui/src/gnuwin32/aichat.c, aiimage.c, and their headers; RGui inspected at f893504 |
| Second implementation | ../rstudio/src/node/local-assistant and package/windows; RStudio inspected at 492ccba66e |

No GUSEK source build or GUI exercise was performed while writing this plan.
The installed solver was checked with:

~~~powershell
.glpsol.exe --version
.glpsol.exe --check --math .examples	ransp.mod
~~~

The old batch file's C:/MinGW does not exist here. D:/rtools45 and the VS 2026
Community vcvarsall.bat exist, but an x86 GUSEK build has not been verified with
either. Treat compiler compatibility as the first implementation gate.

## 2. Design and defaults

Use a **native Win32 child pane docked on the right**, inside GUSEK's content
area. Keep the editor/output split in the remaining area, and keep the menu,
toolbar, tab bar and status bar available. Closing the assistant hides the
same pane and conversation. It must not replace the solver output pane.

Adapt RGui's native assistant into small GUSEK modules. Use RStudio as the
second behavioral reference and source of test cases. GUSEK has no browser,
Electron, Node runtime or Chat-provider infrastructure. Its Lua command hooks
already manage solver options; model networking and streaming belong in native
modules separate from those commands.

Recommended defaults:

- **Tools > AI assistant**, with **Ctrl+Shift+T**, configurable through an
  assistant shortcut property. No toolbar button. This shortcut was not found
  in the checked default files; still check the user's overrides.
- Enabled by default, with **ai.enabled=0** removing the command and preventing
  pane creation, model startup and downloads. Turning it off while active
  cancels work, stops owned children and releases the pane.
- Qwen3.5-4B Q4_K_M and its F16 projector, CPU only, using the same pinned
  llama.cpp bundle as the existing integrations.
- Proposed GUSEK model endpoint **127.0.0.1:28713**, configurable and separate
  from RGui's 8713 and RStudio's 18713. Check availability at startup.
- Native RichEdit transcript and question box, Unicode throughout.
- Per-user data by default; a launcher selects a writable work folder for
  portable use. Models are downloaded on first use and excluded from installers.

Microsoft documents Msftedit.dll/MSFTEDIT_CLASS for RichEdit 4.1 and requires
Unicode data for Unicode controls. Use the RGui RICHEDIT50W approach with
explicit W APIs. [RichEdit documentation](https://learn.microsoft.com/en-us/windows/win32/controls/about-rich-edit-controls).

~~~text
GUSEK UI thread
  menu / shortcut / native assistant pane
  live editor and output snapshots; explicit code insertion
       |
       | immutable request and cancellation event
       v
assistant worker                    download worker
  server supervision                  Range / progress / SHA-256
  health polling / HTTP / SSE          .part -> verified model
       |                                    |
       +----- queued events + PostMessage --+
       |
       v
llama-server.exe, loopback, CPU, owned Windows job
~~~

Workers never read or mutate Scintilla controls or call the Lua extension.
All host operations and transcript updates happen on the UI thread. Network
I/O, model loading and download/hash work must not block that thread. Give each
turn a generation identifier so late events from a cancelled turn cannot alter
a newer conversation. Drain queued events before disposing their owner.

## 3. Source integration map

First import **scite/** and **scintilla/** from ../Gusek-fork into this repository,
preserving licenses. Keep the existing packaged assets at the repository root.
All paths below then refer to files inside this repository.

| File / symbol verified in the sibling source | Required integration |
|---|---|
| scite/src/SciTE.h | Reserve a free command ID below 2000, e.g. IDM_AIASSISTANT=470 after checking all resources |
| scite/win32/SciTERes.rc | Add the Tools menu label; keep the configurable shortcut display synchronized; handle static Sc1 resources too |
| scite/win32/SciTEWin.h | Own the assistant controller; declare host-adapter and message-filter methods |
| scite/win32/SciTEWin.cxx: Command() | Route assistant command IDs directly, including focus-aware Edit actions |
| SciTEWin::ReadProperties() / CheckMenus() | Read enabled/shortcut/layout properties, add/remove command, and update checked state |
| SciTEWin::EventLoop() / KeyDown() | Dispatch assistant shortcuts before global editor accelerators when focus is in the pane |
| scite/win32/SciTEWinBar.cxx: SizeContentWindows() / SizeSubWindows() | Reserve assistant width, lay out the original editor/output split inside the remainder |
| SciTEWin::Paint() / WndProcI() | Update splitter painting and hit-testing to use that same remaining rectangle |
| scite/src/SciTEBase.cxx: NormaliseSplit() / MoveSplit() | Account for available editor/output width when the assistant is visible; keep hidden behavior identical |
| SciTEWin::QuitProgram() / destructor / WM_DESTROY | Stop workers and owned model before destruction; do not stop it if the save-confirmation dialog cancels quitting |
| SciTEWin::ExecuteOne() | Track solver-run boundaries in plain worker data and marshal completion to the UI; never put model work in the solver job queue |
| SciTEBase::SendEditor() / SendOutput() / GetRange() / New() | Host adapter for live unsaved model, output, insertion and a new MathProg document |
| scite/src/SciTEProps.cxx: GetMenuCommandAsInt() | Command names come from IFaceTable, not merely the #define |
| scite/src/IFaceTable.cxx / scite/scripts/IFaceTableGen.py | Add/regenerate the assistant constant if exposing it through user.shortcuts; constants are sorted for binary lookup |
| scite/win32/makefile and scite.mak | Compile/link assistant modules in both normal SciTE and static Sc1 targets |
| SciTEGlobal.properties / embedded properties | Add documented assistant defaults; staged runtime must use GUSEK's properties, not stock SciTE files |

Important: the existing layout also paints and drags the output splitter.
Changing only SizeContentWindows() leaves the split bar in the wrong place.
Give the assistant its own splitter; clamp both panes at narrow window sizes.
Test the original horizontal and vertical output modes with the assistant
visible, hidden and after resizing.

Suggested new files, with responsibilities rather than mandatory names:

~~~text
scite/win32/ai/
  GusekAiHost.h                 live context, explicit insert/new model
  GusekAiPane.h/.cxx            controls, menus, focus, hide/show, layout
  GusekAiRender.h/.cxx          streaming Markdown, selection, fonts, images
  GusekAiController.h/.cxx      turn lifecycle, worker/event ownership
  GusekAiProtocol.h/.cxx        JSON, SSE, think filter, history, fenced code
  GusekAiModel.h/.cxx           process/job, health, logging, cancellation
  GusekAiDownload.h/.cxx        pinned assets, resume, SHA, proxy, progress
  GusekAiConfig.h/.cxx          validated settings, paths, prompt, course files
  GusekAiImage.h/.cxx           GDI+ normalization, clipboard, strip/viewer
tests/ai/                      unit, fake-server and GUI/real-model tests
ai/defaults/                   commented config, MathProg prompt, context README
packaging/gusek-ai.iss          per-user installer
gusek.ps1 / gusek.cmd           build, test, package, installer commands
~~~

## 4. Reuse the tested implementations carefully

Read RGui's aichat.c fully before extracting anything. It mixes portable
protocol/model logic with R/GraphApp integration and configuration.

| Reusable reference | What to retain / adapt |
|---|---|
| aichat.c: dynbuf, json_escape(), json_unescape(), json_find_string() | Request encoding, null-role delta handling, Unicode escapes and surrogate pairs; add hostile/truncated response tests |
| aichat.c: HTTP/chunked/SSE functions, ai_worker(), w_emit_filtered() | Streaming parser and split think-tag filtering; no UI or host calls in worker |
| aichat.c: ai_server_start_locked(), log/error and download routines | Job ownership, hidden startup, DLL working directory, readiness/error reporting, WinHTTP resume and bcrypt verification |
| aichat.c: re_*(), md_*(), tr_*(), ai_extract_code() | Unicode RichEdit, Markdown formatting, scrolling/selection buffering, fenced code extraction |
| aiimage.c / aiimage.h | GDI+ orientation/scale/encode, clipboard files, thumbnails, image strip and viewer |
| RGui/src/gnuwin32/aitests | Fake-server behavior and protocol regression cases; replace R/GraphApp stubs with GUSEK host stubs |
| RStudio/src/node/local-assistant/src/shared/chat.ts | Four newest pictures, old-picture marker, incomplete fences, history and split think tags |
| RStudio/src/node/local-assistant/src/server/config.ts | Validated defaults, relative-path handling, course-file ranking; extend to MathProg |
| RStudio local-assistant/test and e2e/rstudio/tests/panes/local-assistant | Failure/cancel/reader-only/Unicode/scroll/edit/first-run acceptance scenarios |
| RGui/rgui.ps1 and packaging/rgui-ai.iss; RStudio/rstudio.ps1 and package/windows | Build/staging metadata, pinned fetches, upgrade preservation and scoped uninstall tests |

Remove R/GraphApp includes, Rhome lookup, console/editor callbacks and MDI
window glue from adapted code. Do not import aiplot.c, link R.dll or add an R
runtime. GUSEK has no R graphics device. GNUplot is an external command here,
not an in-process plot bitmap; retain file/clipboard image attachment.

Keep source copyright/license notices: RGui assistant files are GPL-2.0-or-later;
GUSEK's README states GPL-3.0-or-later and SciTE/Scintilla have their own
permissive license. RStudio's assistant package declares AGPL-3.0-only.
This native plan uses RGui code and treats RStudio as a behavior/test reference;
do not paste its code into GPL-labeled files while dropping its license.

## 5. Host behavior and MathProg context

### Commands, focus and editing

Ctrl+Enter already invokes IDM_COMPLETEWORD globally in SciTERes.rc. When the
question box owns focus, intercept it in the event loop before
TranslateAccelerator and send the question. Plain Enter inserts a newline.
Apply the same early filter to the configurable toggle shortcut inside all
assistant controls. Preserve Ctrl+Enter autocomplete in the normal editor.

Edit menu Copy/Paste/Select all must target the focused assistant control;
the host currently knows editor/output focus. Transcript text is read-only,
question pastes are plain text, and a pasted picture becomes an attachment.
Do not send ordinary menu commands through the solver's ExecuteOne() queue.

Copy code takes only fenced blocks from the most recent answer, joins them
without fences, and preserves the clipboard if none exist. To editor is a
user-invoked operation: check read-only state, identify the target buffer, use
SCI_BEGINUNDOACTION / SCI_REPLACESEL / SCI_ENDUNDOACTION, and keep the document
unsaved. With no suitable editable model, create an unsaved MathProg document
through New() and set its language without writing a file or running glpsol.
Capture the target at the action, and abort if it closes or changes during
any deferred operation. Code from the model is inert text.

### Attach menu

| Item | Source and behavior |
|---|---|
| Current model / document | Read the active Scintilla buffer, including unsaved edits; show filename/format; retain the start within the text budget |
| Last solver error | Command plus relevant diagnostic lines from the last glpsol run; report no error when no diagnostic exists |
| Last solver output | Snapshot the most recent solver run, separately from unrelated external-tool output |
| Recent output | Bounded tail of wOutput, visible and editable in the question before sending |
| Picture files / clipboard / Remove pictures | Same image behavior as RGui; multi-select and Explorer file-list paste |

Current .dat, .lp, .mps, .glp or .out documents can be attached as the current
document, identified accurately. Do not silently attach another tab or reread
the disk version of an unsaved file. Add selected external data only as an
explicit action if implemented; do not execute Lua to discover/run it.

GUSEK's output starts jobs with a greater-than command line. Track the run's
start/end in the existing output append path and post completion data to the
UI. Recognize GLPK/MathProg diagnostics and nonzero exits, not R's Error regex.
Distinguish syntax/runtime errors, infeasible/unbounded solver status, and a
successful solve with warnings. Keep unrelated gnuplot/Python output from
being called a solver error. Never restart the solver to obtain an attachment.

### Unicode

Both source and installed SciTEGlobal.properties default to code.page=0.
Use UTF-8 for new assistant-created documents, and do explicit conversions
at the boundary for existing documents using another code page. Read the
buffer's actual encoding/unicodeMode and SCI_GETCODEPAGE. Detect loss when
inserting nonrepresentable text into a legacy-encoded buffer and offer explicit
UTF-8 conversion or a new UTF-8 document; never silently replace characters.
Keep an existing file's save encoding unless the user chooses conversion.

Preserve raw solver bytes with a defined decoding policy; split multibyte
sequences can cross pipe reads. Test Hungarian comments/data and paths. Merely
setting output.code.page=65001 does not establish the solver's output encoding.
Use UTF-8 internally, UTF-16 for new Win32 controls and file paths, explicit
CreateWindowExW/SendMessageW/clipboard conversions, and Segoe UI/Emoji fallback.
Do not globally enable UNICODE across legacy SciTE as an unreviewed shortcut.

### Conversation, rendering and pictures

Reproduce guide 2.2-2.6: status, sender labels (You / GUSEK assistant), transcript,
attachment strip, question, Send/Stop/Copy code/To editor/New chat and menus.
Use proportional wrapping prose and shaded monospace code, preserve raw
Markdown, and render the unfinished line during streaming. Arithmetic such as
2 * 3 must remain arithmetic. Treat model text as text rather than RTF commands.
Preserve selections and scroll position; buffer during a physical mouse drag
and flush after release even if capture was lost.

Image formats: PNG/JPEG/BMP/GIF/TIFF, EXIF orientation, longest side at most
1600px, PNG with JPEG fallback around 1.2MB. Use file/clipboard images and
Explorer multi-file paste, thumbnails with removal, a fitted resizable viewer
and Esc. Release the clipboard promptly; do not hold it during image encoding.
Retain at most four newest pictures in requests including history, add the
old-picture marker, and keep the transcript intact. A missing projector offers
the reader-only download; declining keeps text chat usable. New chat clears
history and attachments as well as the display.

### Prompt and course-material search

Write an editable student prompt for linear/integer programming, GNU MathProg,
GLPK and course exercises. Require fenced MathProg code for .mod examples and
clearly separated .dat examples; code extraction accepts any fence language.
Teach objective, constraints, units, indexes, variable bounds/integrality,
feasibility and interpretation before describing syntax. Ask for exact model,
data and solver messages when needed, explain the error, then suggest the
smallest change. Do not invent objective values or claim a model was solved.
Answer ordinary general questions briefly, as required by the shared guide.

Read course notes fresh per request: .txt, .md, .csv, .mod, .dat, .lp and .mps;
README is instructional and excluded. Bound file size/count and total context
(default 12,000 characters), rank shared words of four or more characters,
break ties deterministically and label each excerpt with its filename.
Support an explicit context directory, an active model directory's .ai-context
folder, then the user's context directory. No automatic whole-disk or project
scan. Do not mix notes/prompts/history from RGui or RStudio.

## 6. Model, downloads and data ownership

Keep the existing pins; these are inherited implementation assets, not newly
chosen model benchmarks. Store them once in a manifest used by fetch/runtime:

| Asset | Pin |
|---|---|
| CPU server | llama.cpp b11153, llama-b11153-bin-win-cpu-x64.zip |
| Server ZIP SHA-256 | 569d19826f3fb00a3fc2df7bd68ab9ad33e5c0d6d69ce022b24372700cee7931 |
| Text model | Qwen3.5-4B-Q4_K_M.gguf; 2,740,937,888 bytes |
| Model SHA-256 | 00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4 |
| Reader | Download mmproj-F16.gguf, save as Qwen3.5-4B-mmproj-F16.gguf; 672,423,616 bytes |
| Reader SHA-256 | cd88edcf8d031894960bb0c9c5b9b7e1fea6ebee02b9f7ce925a00d12891f864 |

Take exact URLs from the copied guide and compare with RGui/RStudio manifests.
Ship the server executable, every needed DLL and license notice together.
Keep the editor x86 for the initial port; the model is a separate x64 process.
The supported AI target is Windows 10/11 x64, i5-class CPU, 16GB, no GPU.
An x86 editor does not make this x64 model bundle usable on 32-bit Windows.

Launch on first panel opening with host 127.0.0.1, the distinct configured port,
context 8192, GPU layers 0, threads 0, image tokens 256 when the reader exists.
Default extra arguments are empty; thinking off, history 12, max output 1024,
temperature 0.3, top_p 0.9, startup timeout 240s and request timeout 120s.
Wait for /health with a deadline and early-exit detection. Surface the useful
server error and log path. Restart owned crashed servers; stop/cancel promptly.

Create the child suspended and hidden, assign it to a checked
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE job, then resume. Keep the job handle private
and non-inheritable; if assignment fails, clean up the child and report it.
Microsoft documents job membership and kill-on-close ownership in
[Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)
and [AssignProcessToJobObject](https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-assignprocesstojobobject).
Use explicit executable paths, Windows command-line quoting and the binary
directory as cwd. Keep this job separate from user-launched glpsol.

Do not terminate a process merely because it occupies a port. Report an
unexpected occupied port; any supported external-server mode must be explicit
and never stop a server GUSEK did not start.

First-run Yes/No offer once, roughly 3.4GB; reader-only offer about 0.7GB.
Persist the decision. Background download shows bytes/percentage, Stop pauses,
and Range resumes .part files. Validate Content-Range, handle ignored ranges
without appending duplicate data, verify the pinned SHA before atomic rename,
and show retryable errors. Check free space including existing partial files.
Use Windows proxy resolution through WinHTTP; keep loopback traffic direct.
Do not claim PAC/WPAD or authenticated school proxies work without testing.

Proposed layout:

~~~text
<install>/
  gusek.exe, glpsol.exe, GLPK DLLs, properties, manuals, examples
  ai/llama/llama-server.exe + DLLs + notices
  ai/defaults/GusekAI.ini, system_prompt.txt, context/README.txt
  Start-Gusek.cmd
<data root>/                     %LOCALAPPDATA%/GusekAI by default
  GusekAI.ini                    editable settings
  system_prompt.txt              copied only when absent
  context/                      course notes
  models/                       downloaded models and .part files
  logs/                         owned server logs
  history/                      optional persisted conversations
~~~

For portable launch, explicitly select <install>/work/assistant through a new
GUSEK_AI_DATA override and route SciTE user settings to a work/config folder.
Implement and test that settings path; do not assume SciTE recognizes a new
environment variable. Derive app home from the executable, not the working
directory. Explicit relative path settings resolve against app home; unset
model/prompt/context paths use data root. Preserve relative settings rather
than saving resolved drive letters. Do not automatically migrate shared caches.

## 7. Build and distribution

First make the existing Sc1 target reproducible with Lua enabled. Prefer a
verified VS x86/nmake route on this machine; repair obsolete flags such as
OPT:NOWIN98 only after observing their failures. If that route is impractical,
pin a tested mingw-w64 x86 toolchain and update the obsolete -mno-cygwin flags.
Record the chosen compiler/version and exact commands; do not claim either
route is working until it compiles and runs. Do not modernize the entire editor
or replace its lexer as part of adding the assistant.

Build in a dedicated space-free mirror, e.g. D:/gusek-build, with source,
objects, stage, portable ZIP, installer, cache and logs separated. Preserve
current root binaries during development. RGui's Rtools compiler is x64;
do not accidentally change GUSEK's architecture because it is available.
Keep C++ compatible with the baseline build and compile new modules with
warnings treated as errors separately from existing legacy warnings.

Add native modules and libraries (ws2_32, winhttp, bcrypt, gdiplus and existing
ole32/comdlg32 dependencies) to both dynamic and static targets. Update object
dependency tracking so quick rebuilds include assistant/header/resource edits.
Handle embedded defaults as well as external GUSEK property files.
Use the new wrapper instead of the old HOME-changing/UPX batch entry point.

Provide gusek.cmd wrapping gusek.ps1 with the familiar commands:
doctor, fetch -NoModel, full, quick, run, dev, test -Real/-Gui/-Installer,
package, deploy -Drive, installer -Version, clean. These commands are proposed,
not available yet. Report paths and elapsed build times. Keep fetches pinned
and verified, build signatures sensitive to toolchain/options, and cleanup
restricted to resolved paths inside the selected build directory.

Package a model-free portable ZIP and per-user Inno Setup EXE with SHA-256
files. Installer uses PrivilegesRequired=lowest, a unique stable GUSEK AppId,
per-user shortcuts, and a default install under LocalAppData/Programs.
Upgrade preserves settings, custom prompt, notes, history, models and partial
downloads. Actual uninstall removes installed program files and the two known
default model files/parts, preserving coursework/prompts/settings/history.
Never recursively delete work, custom model folders or another application's
data. Add Explorer integration only via an explicit per-user choice; the old
Lua commands use HKEY_CLASSES_ROOT and are not the non-admin installer path.

Once the local build/tests/installer work, add a version-tag Windows workflow,
using RStudio's .github/workflows/windows-installer.yml as the pattern. Match
the verified GUSEK toolchain/architecture, validate numeric and dotted
prerelease tags, use pinned actions, fetch -NoModel, build/test/package, and
upload setup EXE + checksum as a downloadable Actions artifact. Default to
contents: read and artifact downloads. Do not copy RStudio's R setup, npm/GWT
steps or MSVC145 dependency cache into GUSEK. Writing the workflow locally does
not authorize pushing tags, running it or publishing a release.

## 8. Order of work and completion gates

| Phase | Concrete deliverable | Gate before continuing |
|---|---|---|
| 0: Baseline | Import real source into this repository, reproducible Sc1 build, staged known solver, build wrapper skeleton | Open/edit/save MathProg, user-initiated check/solve, output and stop work; record build timings |
| 1: Host pane | Menu/shortcut, native dock, focus routing, enabled setting, live document/output adapter | Toggle from editor and question; close/reopen preserves text; Ctrl+Enter keeps its distinct meanings; both split modes work |
| 2: Text protocol | Configuration, model supervisor, JSON/SSE/history/cancel with fake server | Host edits and runs a solver during a stream; no assistant action executes a command; malformed streams/timeouts/crashes recover |
| 3: Useful conversations | MathProg prompt/course search, error/output attachment, Markdown, code actions, Unicode | Correct run attached; unsaved model preserved; single undo insertion; no-code clipboard unchanged; text round trips |
| 4: Images | File/clipboard strip/viewer, normalization/history, reader-only flow | Five-format fixtures, orientation/size, Explorer paste, four-picture request cap, follow-up and declined reader work |
| 5: Downloads | First-run decision, resumable verified assets, free-space/proxy behavior | Local fake download tests cover pause/resume/ignored range/bad hash/HTTP failure; real cached assets verify |
| 6: Real runtime | Actual pinned CPU model, one MathProg response and image recognition | Fenced usable example, image with 42 recognized, cancellation and owned-process hard-stop verified in GUSEK |
| 7: Distribution | Portable ZIP, per-user installer, tag workflow and user/dev docs | Relocation, spaced install path, upgrade preservation, uninstall scoping and workflow validation pass |

Do not leave a fake-server demo as the completed assistant. Do not download
the multi-GB models twice: inspect existing cache availability and hashes,
reuse verified archives or explicit test copies without sharing writable app
settings. Build/tests use isolated profiles, ports, files and logs.

Keep local commits few per feature: a source/build baseline can be one commit,
and coherent assistant/distribution work can be grouped into working commits;
do not turn every fix into another permanent commit. Work on main, amend
unpushed fixups and preserve any changes made by the owner.

## 9. Validation checklist

All four layers from guide section 7 are required. Add tests with the code
they protect, not just tests that check a new file or method exists.

1. **Unit/control tests:** JSON control escapes/null/surrogates; incomplete SSE
   and think tags; fenced blocks/none/unfinished; MathProg diagnostic extraction
   and run boundaries; course ranking/budget/reload; validated paths/config;
   request shape and four pictures; image orientation/size; real RichEdit
   Markdown, font fallback, scroll and selection behavior.
2. **Fake-server tests:** split chunked SSE with Hungarian/Greek/Chinese/emoji,
   a captured real stream, role-only first delta, HTTP error/truncated stream,
   hung startup/request, crash/restart, sub-second cancellation, duplicate Send,
   NOCODE clipboard preservation, image count and captured pixels, resumable
   deterministic download with bad SHA/404/ignored or invalid Range.
3. **Real-model tests:** generate a short fenced MathProg model, let the test
   explicitly run its own verification separately, recognize 42 in an image,
   text-only then reader restart, and confirm CPU settings and process cleanup.
   Real-model tests are opt-in and a skipped test must be reported as skipped.
4. **GUI/installer tests:** menus/focus/toggle/disable, all Attach/button flows,
   single undo/new model/read-only behavior, unsaved Unicode buffers, host solve
   during streaming/loading/download, model unavailable, clipboard restoration,
   drag release outside the pane, layout resize, portable relocation, unique
   install folder with spaces, Start-menu launch, upgrade preserving user data,
   uninstall removing only owned/default assets, hard-kill owned server cleanup.

Also test enabling RGui, RStudio and GUSEK together: distinct data directories,
model endpoints and process ownership. Stop GUSEK and confirm the others remain
running. Check that no model/download process starts when ai.enabled=0.

Keep a VALIDATION.md with exact commands/results/artifact paths, failures and
skips. Distinguish GUI tests run on an unlocked desktop from static source
review. Test Windows 10 and the target i5/16GB machine when available; do not
turn measurements from another host into compatibility/performance claims.

## 10. Working rules

- Commit locally on the default branch; no feature branches.
- Never push, create/modify PRs, trigger workflows, publish releases or write to
  GitHub. The owner's own push does not change this rule for the implementing agent.
- Never print secrets or token fragments; test existence only. Keep agent
  accounts/tokens/endpoints separate; this assistant needs no cloud credentials.
- No Co-Authored-By trailers or attribution footers in commits/files/docs.
- Preserve existing application behavior, user work and the checked source licenses.
- Do not request confirmation for routine decisions already specified here.
  If an actual constraint blocks implementation, give evidence and continue
  independent work; report tested behavior and remaining gaps honestly.

Use [prompt.md](prompt.md) as the implementation starting prompt.
