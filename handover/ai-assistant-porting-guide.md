# Porting the local AI assistant to another desktop application

This is a hand-over specification. It describes an AI assistant that was
built into **RGui** (the Windows GUI of R) and is in daily use there, and
tells you how to build the same assistant into another open-source
desktop application — for example **GUSEK** (the GLPK/MathProg IDE) or
**RStudio**. You are working in a fork of that application.

Read all of it before you start. Section 2 is the contract: every item
there is a requirement the owner has asked for and checked in RGui.
Sections 4–9 are what was learned building it; most of them describe a
mistake that was made once and should not be made again.

---

## 1. What it is, in one paragraph

A panel inside the host application where a student asks questions in
plain language and gets answers from a **local** language model
(Qwen3.5-4B, quantised, running on the CPU through llama.cpp's
`llama-server`). It knows the course material the student puts in a
folder, can be given the student's last error, script, console output,
plot or a screenshot, answers in formatted Markdown with code in boxes,
and can copy the code or insert it into the editor. **It never runs
anything itself.** Nothing leaves the computer after the one-time model
download. The host application must keep working exactly as before,
whether or not the model is there, loading, answering or crashed.

---

## 2. Requirements (the contract)

### 2.1 Host application

- The host must look and behave as before. The assistant is one
  discreet entry: a **menu item** (in RGui: *Misc > AI assistant*) and a
  **keyboard shortcut** (Ctrl+T in RGui; pick a free one in the host and
  make it configurable). No new toolbar button.
- The same shortcut toggles the panel **from inside the panel** too.
- The panel lives **inside** the host window (a docked pane, an MDI
  child, a tab — whatever the host's own panes are), with the host's own
  menu bar and toolbar still usable while it is active.
- Closing the panel **hides** it. Reopening shows the same conversation.
- The host stays fully usable while the model loads, downloads or
  answers: the user can keep typing and running code.
- If the model server is missing, fails to start, crashes or hangs, the
  host is unaffected and the panel's status line says what happened in
  one sentence (including the server's own error message when there is
  one).
- A setting turns the whole feature off; then no assistant code runs and
  the menu entry does not appear.

### 2.2 The panel

From top to bottom:

1. **Status line**: Ready / Loading the model… / Reading the picture… /
   Answering… / Downloading 1.2 of 3.4 GB (35%) / the last error.
2. **Transcript** (read-only): "You" (bold, dark red) and "R assistant"
   (bold, navy) headers; the question; the answer rendered as Markdown
   while it streams (see 2.4). Text is selectable and copyable.
3. **Attachment strip** (only while pictures are attached): a thumbnail
   per picture with its name; click opens it full size; an × on each
   removes just that one.
4. **Question box**: multi-line, plain text. **Ctrl+Enter sends**, Enter
   is a newline. Pasting always pastes plain text (or attaches a picture,
   if the clipboard holds one).
5. **Buttons**: Send, Stop, Copy code, To editor, New chat.
6. **Menu** (in the panel's menu bar, or the host's equivalent): an
   **Attach** menu with *Last error from the console*, *Current script*,
   *Recent console output*, *Current plot*, *Picture file…*, *Picture
   from the clipboard*, *Remove pictures*; plus Copy / Paste / Select
   all, New chat, and the toggle entry with its shortcut.

Behaviour:

- **Send** while an answer is running is refused with a status message.
  **Stop** cancels the answer (or pauses the download) promptly.
- **Copy code** puts the code of the last answer's fenced blocks on the
  clipboard, fences removed, blocks joined. If the answer has **no
  fenced block**, it says so in the status line and leaves the clipboard
  alone (it must never copy prose).
- **To editor** inserts that code at the cursor of the open script, as
  one undoable step; with no script open, it opens a new one with the
  code. Same "no code block" rule.
- **Attach** items put their text into the question box, where the user
  sees and edits it before sending. Long attachments are trimmed (keep
  the end of console output, the start of a script). "Last error" means
  the command that caused it plus the error lines.
- **Nothing is executed** by the assistant. Not code, not commands.
- **New chat** clears the transcript, the history and attached pictures.

### 2.3 Text

- Every Unicode character works everywhere: typed, pasted, set, sent,
  displayed — accents (Hungarian ő ű), Greek, maths symbols (≤ ∑ √),
  Chinese, emoji. Test exactly these.
- Emoji and characters missing from the code font must fall back to a
  font that has them (on Windows: Segoe UI Emoji; Segoe UI with its
  linked fonts for CJK).

### 2.4 Answer rendering (Markdown, while streaming)

- Prose in a proportional UI font that wraps to the panel width; no
  horizontal scroll bar.
- Fenced code blocks in a monospace font on a light grey background,
  indented, **without the ``` fences** shown.
- `inline code` in monospace on grey; **bold**, *italic* (only around
  words: `2 * 3` stays arithmetic), `#` headings bold, `-`/`*` bullets as
  "•" with an indent. Horizontal rules dropped.
- An unfinished line is shown as it arrives and re-rendered with its
  formatting once its newline arrives.
- The raw Markdown is kept separately for Copy code / To editor.

### 2.5 Scrolling and selection while an answer streams

- The view follows new text **only while it is at the bottom**. If the
  user scrolls up, the view stays exactly where it is while the answer
  grows; scrolling back to the bottom makes it follow again. A new
  question always jumps to the end.
- A selection in the transcript is kept while text streams in.
- While the left mouse button is down in the transcript (a selection
  being dragged), streamed text is held back and written when the button
  is released — and only while the button is physically down, so an
  answer can never get stuck.

### 2.6 Pictures

- The model reads pictures (see 3.2). Sources: the current plot (taken
  from the plot device's **own bitmap**, not a screen capture — see 6),
  a file (PNG, JPEG, BMP, GIF, TIFF; multi-select), the clipboard
  (screenshot, or image files copied in the file manager), Ctrl+V in the
  question box.
- Pictures are scaled to at most 1600 px on the long side, phone photos
  turned upright from their EXIF orientation, sent as PNG (JPEG when the
  PNG would exceed ~1.2 MB).
- A picture with no question asks "What does this picture show? If it is
  a plot, R output or an error, explain it."
- Sent pictures appear in the transcript as thumbnails (about 260×170)
  with a caption; clicking one opens it. The picture viewer is a
  resizable window, fitted to its size, titled with the picture's name;
  Esc closes it.
- Pictures stay in the conversation for follow-up questions, **at most 4
  per request** counting history, newest first; older ones are replaced
  by the note "[A picture was attached here; it is no longer shown.]".
- If the picture reader file is missing, attaching a picture offers to
  download it; declined, the assistant stays text-only.

### 2.7 First-run download

- The model is **not** in the installer (too big for a release asset).
  The first time the panel opens without it, ask once (Yes/No dialog)
  whether to download the model and its picture reader (3.4 GB) now.
- Download in the background with progress in the status line; the host
  stays usable; Stop pauses; an interrupted download **resumes** from a
  `.part` file with an HTTP `Range` request; the file gets its real name
  only after its **SHA-256** matches the pinned one; check free disk
  space first; use the system proxy settings (school networks need it).
- If the model is already there but the picture reader is not, offer
  just the reader (0.7 GB); afterwards restart the model server with it.
- After the download, everything works offline.

### 2.8 Deployment

- Portable and offline, no administrator rights: runs from a USB stick
  or a per-user install. Paths in the configuration are relative to the
  application's home so the stick can change drive letter.
- Target machine: Intel i5, 16 GB RAM, **no GPU**, Windows 10/11.
- A per-user installer (RGui used Inno Setup, `PrivilegesRequired=lowest`)
  that keeps the user's settings, prompt and notes on upgrade and removes
  the downloaded model on uninstall.

### 2.9 What the owner wants in your reports

Say plainly what you **tested and saw working**, and what is **built
but not tested**, separately. Never claim a check you did not run.

---

## 3. The model

### 3.1 Files (pinned)

| What | Value |
|---|---|
| Model | `Qwen3.5-4B-Q4_K_M.gguf` from `unsloth/Qwen3.5-4B-GGUF` on Hugging Face |
| URL | `https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/main/Qwen3.5-4B-Q4_K_M.gguf` |
| SHA-256 | `00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4` |
| Size | 2 740 937 888 bytes |
| Picture reader (multimodal projector) | `mmproj-F16.gguf` from the same repository, **saved as** `Qwen3.5-4B-mmproj-F16.gguf` |
| URL | `https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/main/mmproj-F16.gguf` |
| SHA-256 | `cd88edcf8d031894960bb0c9c5b9b7e1fea6ebee02b9f7ce925a00d12891f864` |
| Size | 672 423 616 bytes |
| Server | llama.cpp release **b11153**, `llama-b11153-bin-win-cpu-x64.zip` from `github.com/ggml-org/llama.cpp/releases` |
| SHA-256 of the zip | `569d19826f3fb00a3fc2df7bd68ab9ad33e5c0d6d69ce022b24372700cee7931` |

Ship **all** the DLLs of the zip next to `llama-server.exe` and start it
with that folder as its working directory, so nothing has to be put on
`PATH`. The Hugging Face API
(`https://huggingface.co/api/models/unsloth/Qwen3.5-4B-GGUF/tree/main`)
lists sizes and SHA-256 (`lfs.oid`) if you need to re-pin.

Why this model (checked in October 2026): it is natively multimodal,
the strongest reasoner of its size class (MMLU-Pro 79, GPQA 76), and at
Q4_K_M fits the target machine. Gemma 4 E4B was considered: faster, but
clearly weaker at reasoning and a bigger download. No smaller Qwen3.6
existed.

### 3.2 Measured performance (CPU only)

| | Value |
|---|---|
| RAM, text only | ≈ 3.4 GB |
| RAM with the picture reader loaded | ≈ 5.0 GB (the reader costs ~1.6 GB, not 0.7) |
| Model load from SSD | 4–8 s (minutes from a slow USB stick the first time) |
| Generation | 8–11 tokens/s (13th-gen i7, 4 threads); expect 5–10 on an i5 |
| Reading one picture at `--image-max-tokens 256` | 7–11 s on the i7; estimate 20–30 s on an older i5 |
| Follow-up about the same picture | cheap: the server reuses its cache (260 of 264 prompt tokens cached in the test) |

### 3.3 Server command line

```
llama-server.exe -m <model> --host 127.0.0.1 --port 8713 -c 8192 -ngl 0
                 [-t <threads>] [--mmproj <reader> --image-max-tokens 256]
                 [<extra_args from the config, verbatim>]
```

- **Loopback only.** Nothing listens on an external interface.
- Leave `extra_args` **empty by default.** llama.cpp renames options
  between releases (`--no-mmap` became `--load-mode none` in b11153), and
  one unknown option stops the server from starting. RGui's default
  `--no-mmap` broke every start once.
- Start it as a child in a **job object with KILL_ON_JOB_CLOSE**, so
  killing the host kills the server and nothing keeps the USB stick
  locked. Hide its window (`CREATE_NO_WINDOW`).
- Redirect its stdout/stderr to a log file in the temp folder. When it
  exits during start-up, put its last line containing "error" or
  "failed" in the status line, and the log path in the docs.
- If something already answers `/health` on the port, use it instead of
  starting a second server.
- Start it when the panel first opens (warm-up), so the first question
  does not pay for the model load.

### 3.4 Protocol

- `GET /health`: 200 ready, 503 still loading. Poll every ~0.5 s up to a
  configurable `startup_timeout` (240 s default), giving up early if the
  process has exited.
- `POST /v1/chat/completions` (OpenAI-compatible), streaming:

```json
{"messages":[
   {"role":"system","content":"<system prompt + course material>"},
   {"role":"user","content":"earlier question"},
   {"role":"assistant","content":"earlier answer"},
   {"role":"user","content":[
      {"type":"image_url","image_url":{"url":"data:image/png;base64,...."}},
      {"type":"text","text":"What kind of plot is this?"}]}],
 "temperature":0.3, "top_p":0.9, "max_tokens":1024, "stream":true,
 "chat_template_kwargs":{"enable_thinking":false}}
```

  - Content is a plain string unless the message has pictures; then a
    list, **pictures first**, then the text.
  - `enable_thinking:false` turns off Qwen's reasoning mode (it triples
    answer time on a CPU). Still strip any `<think>…</think>` block from
    the stream, including tags split across chunks (keep back up to
    `len(tag)-1` bytes until you know).
  - The response is chunked transfer encoding carrying SSE records
    `data: {json}` separated by blank lines, ending with `data: [DONE]`.
    Records and even JSON tokens are split across chunks. Take
    `choices[0].delta.content`; the first delta is role-only with
    `"content": null` — not an error, not text.
  - JSON strings contain `\uXXXX` escapes including surrogate pairs for
    emoji; decode them to UTF-8.
  - Escape control characters (`\n`, `\t`, `\u0001`…) when building the
    request. A raw newline in the body was a real bug.
  - On HTTP errors, read the body and show its `message` field.
- Resend the last `keep_history` (12) messages each time.
- Cancel by closing the socket from the UI thread; the worker must
  notice within a second.

---

## 4. Architecture

```
 host UI thread                      worker thread (one at a time)
 ─────────────                       ───────────────────────────────
 menu/shortcut → panel               start llama-server if needed
 Send: build the whole request ────► wait for /health 200
       body (JSON string)            POST, read SSE, filter <think>
                                     append text to a locked buffer
 message/event handler   ◄───────── post "data waiting" (coalesced)
   drain buffer → render Markdown    post "status: …", post "done(ok)"
   finish turn on "done"
```

- **The model runs out of process.** A crash, a hang or a missing file
  in it cannot take the host down, and no C++ runtime or model library
  is linked into the host.
- **All UI work and every call into the host's own API happen on the UI
  thread.** The worker only does sockets/HTTP and plain memory; it hands
  text to the UI thread through a mutex-protected buffer and a posted
  message (one pending notification at a time). In RGui the worker must
  not touch R at all; in RStudio the same rule applies to the R session;
  in GUSEK to the editor.
- The **request is built on the UI thread** (it reads the conversation
  and the course material) and handed to the worker as one finished
  string.
- The **turn is finished on the UI thread** (store the answer, re-enable
  Send), so Send cannot slip in between the last token and the end.
- Downloads run on their own worker with the same rules.

### Course material

- A folder (`ai/context/`) of `.txt .md .R .Rmd .csv` files (for GUSEK:
  add `.mod .dat`), read fresh on every question; no index.
- If everything fits in `context_max_chars` (12 000) it is all sent;
  otherwise each file is scored by how many words of four or more
  characters it shares with the question, best first, until the budget
  is used. Ship a `context/README.txt` that explains this to the
  teacher/student and suggests one topic per file with descriptive names.
- The system prompt is a file the user can edit, with a built-in
  fallback if it is missing. The course material is appended under the
  heading `COURSE REFERENCE MATERIAL`.

### Configuration file (keys and defaults)

```
enabled = yes            hotkey = T
server_exe = ai/llama/llama-server.exe
model = ai/models/Qwen3.5-4B-Q4_K_M.gguf
model_url / model_sha256 / model_bytes   (3.1; empty model_url = no download offer)
vision = yes
vision_model = ai/models/Qwen3.5-4B-mmproj-F16.gguf
vision_url / vision_sha256 / vision_bytes
image_max_tokens = 256
host = 127.0.0.1         port = 8713
autostart = yes          startup_timeout = 240     request_timeout = 120
extra_args =             (empty, see 3.3)
ctx_size = 8192          n_predict = 1024          threads = 0     gpu_layers = 0
temperature = 0.3        top_p = 0.9
thinking = no            strip_think = yes
system_prompt_file = ai/system_prompt.txt
context_dir = ai/context context_max_chars = 12000 keep_history = 12
```

Relative paths resolve against the application's home. Comment every key
in the shipped file. Keep defaults in code too, so an old config file
from an earlier version still gets new features. The installer must
**not overwrite** an existing config or prompt on upgrade.

### Folder layout

```
<app home>/
  etc/<config file>
  ai/system_prompt.txt
  ai/context/README.txt         + the course files
  ai/llama/llama-server.exe     + all its DLLs
  ai/models/                    the two .gguf files (downloaded)
```

---

## 5. The system prompt

Adapt the subject and the code-fence language to the host, keep the
structure. This is RGui's, verbatim:

```
You are an assistant built into RGui. You help one student with the R
programming used in their statistics course.

HOW TO ANSWER

- Your main job is R, statistics, handling data and writing up results
  for this course. That is where to put your effort.
- If a question is about something else, answer it briefly and plainly.
  Do not refuse ordinary questions, and do not lecture or philosophise.
  Where it fits, offer to get back to the course.
- Put every piece of runnable code in a fenced block that starts with
  ```r and ends with ```. The Copy code and To editor buttons take the
  code out of those blocks, so code outside them is easy to lose.
- Prefer base R and the packages the course already uses. Do not reach
  for a new package unless there is no reasonable base R way; if you do,
  name it and say why.
- Keep code short. Comment the lines that are not obvious. After the
  code, say in one or two sentences what the output means, in the
  language of the course rather than the language of a programmer.
- Use the notation, the variable names, the function choices and the
  reporting conventions of the COURSE REFERENCE MATERIAL below, even
  where another approach would work equally well. The course is the
  authority, not your own habits.
- If the reference material does not cover something, say which part is
  your own suggestion rather than the course's method.
- If you do not know a function name or an argument, say so. Do not
  invent one. A wrong function name costs the student more time than a
  missing answer.

WHEN DEBUGGING

- Ask for the exact error message and the output of str() on the data if
  they are not already given. (The student can add both with the Attach
  menu: the last error, the current script, recent console output.)
- Explain what the error means before giving a fix.
- Give the smallest change that fixes the problem, not a rewrite.

WHEN A PICTURE IS ATTACHED

- It is the student's plot, a screenshot of an error or of output, or a
  photo of an exercise.
- First say briefly what you see in it that matters for the question:
  the kind of plot, the variables on the axes, the groups, the error
  text. Then answer.
- Read text in the picture exactly as it is written. If part of it is
  too small or unclear to read, say so instead of guessing.
- Do not guess which dataset or package a plot comes from unless the
  picture shows it.

WHEN ASKED TO EXPLAIN

- Explain the statistics first and the R second.
- State the assumptions a method makes and how the course expects them
  to be checked.
- Do not claim significance or effect sizes for data you have not seen.

The student reads, checks and runs all code themselves. Nothing you
write is executed automatically.
```

The owner's rule for scope: answer general questions briefly and freely;
do not refuse, lecture or philosophise; steer back to the course where
it fits. Do not make the assistant refuse non-course questions.

---

## 6. Lessons learned (do not repeat these)

1. **Screen-capturing a window captures whatever covers it.** The first
   "Attach current plot" grabbed the panel lying over the plot window,
   and the model confidently described the wrong picture. Read the plot
   from the plot device's own off-screen bitmap (in RGui: the same path
   `savePlot()` uses). Test it with the plot window covered, and check
   the picture the server *received* by its colours.
2. **Legacy ANSI layers truncate UTF-8.** RGui's GUI toolkit subclasses
   controls with an ANSI window procedure and pumps messages with
   `PeekMessageA`; text through it was cut after the first non-Latin
   characters and typed emoji became `?`. Fix: a Unicode (W) subclass on
   top that sends text/char messages straight to the control's own
   Unicode procedure, and takes a key's `WM_CHAR`s off the queue with
   `PeekMessageW`. Use `EM_SETTEXTEX`/`EM_GETTEXTEX` with code page 1200.
   Check whatever the host uses (Scintilla: its UTF-8 mode; GWT/web: none
   of this, but check the Electron/desktop clipboard paths).
3. **RichEdit's automatic font binding fights you.** It switched text
   after Chinese characters to SimSun and hid emoji. Turn off
   `IMF_AUTOFONT` and assign fallback fonts per run yourself. A
   `CFM_CHARSET` in the control's default format made later font changes
   ignored.
4. **Only msftedit's RichEdit (`RICHEDIT50W`) shows RTF pictures**; the
   older `RichEdit20W` silently drops them. Insert thumbnails as
   `{\rtf1{\pict\pngblip\picwN\pichN\picwgoalT\pichgoalT <hex>}}`.
5. **Auto-scrolling on every chunk** made the start of long answers
   unreadable until they finished. See 2.5 for the rule. Save the scroll
   position before an update and restore it after unless the view was at
   the bottom; formatting by selecting ranges moves the view otherwise.
6. **A mouse drag in progress is broken by changing the selection.**
   Hold streamed text while the button is down (2.5). Detect it by the
   button-down event in the transcript *and* the physical button state,
   not by mouse capture alone (capture can be released elsewhere).
7. **A default option that a newer server does not know stops it
   starting** (`--no-mmap`). Default extra arguments: none.
8. **A fallback prompt without a literal ```r example** made the model
   write bare "r" lines. Show the exact fence in the prompt.
9. **Paths with spaces**: R aborts if its temp directory contains a
   space ("R_TempDir contains space"), which a per-user install under
   "Programs\RGui AI" produced. The launcher sets a space-free TMPDIR.
   Check the host for the same class of problem, and test installs into
   a folder with a space in its name.
10. **Checksum files** must use LF line endings or `sha256sum -c` fails.
11. **The clipboard is shared**: retry `OpenClipboard` for ~0.5 s (a
    clipboard manager may hold it), own it with a real window, and in
    tests save and restore the user's clipboard.
12. **Never copy prose as code.** "Copy code" on an answer without a
    fenced block must say so, not copy the answer.
13. **The model is too big for a release asset** (GitHub's 2 GB limit):
    download on first run (2.7).

---

## 7. Tests (build them as you go; the owner expects all four layers)

1. **Unit tests**, in the host's own language/test framework: JSON
   escape and unescape (surrogate pairs, `null` content), the `<think>`
   filter fed one byte at a time, code extraction (several blocks,
   unterminated fence, none), last-error extraction, course-material
   selection, request shape (system first, history, question last,
   thinking off, pictures before text, the 4-picture limit and its
   note), picture scaling/encoding (PNG vs JPEG, upright), Markdown
   rendering on a **real** text control (no markup left, fonts per run,
   `2 * 3` not italic), Unicode round trips, scroll/selection rules.
2. **Protocol tests against a fake server** (a small Python script):
   - streams a canned answer as SSE inside chunked encoding cut at odd
     sizes (7, 18, 29… bytes), with a split `<think>` block, a
     role-only first delta, `\u` escapes and an emoji;
   - replays a **captured stream from the real llama-server** byte for
     byte;
   - answers prose only when the question contains `NOCODE`;
   - echoes how many pictures it got (`PICTURES last=N total=M`) and
     saves them to disk for the test to inspect;
   - serves an 8 MB deterministic file at `/file/...` with `Range`
     support and a `?slow` mode, for download, resume, Stop, checksum
     mismatch, HTTP 404 and unreachable-server tests.
3. **Real-model test**: start the real server exactly as the host does,
   ask for one line of R in a fenced block, check the answer, draw "42"
   into a picture and check the model reads 42, stop the server.
4. **GUI tests** driving the built application through window messages
   or its UI automation: menu entry and shortcut, toggle/hide/close,
   all buttons, the host running user code **while** the model answers,
   first-run download offer and download, missing model failing
   politely, Attach items, no-code answers, To editor into an open
   script, a Hungarian/Greek/Chinese/emoji question round trip, the plot
   sent being the plot itself, a pasted screenshot, the × on a
   thumbnail, the real model recognising a boxplot, the server dying
   with the host. Tests must restore anything they borrow (config,
   clipboard).

Also test the installer: install into a folder with a space in its name,
start from the Start-menu entry, upgrade over it (user config, prompt and
notes survive), uninstall (program and model gone, user work stays).

The GUI tests need an unlocked desktop session; with the screen locked
the clipboard and real mouse input do not work. Report such skips as
"not tested", not as passes.

---

## 8. Build pipeline and releases

RGui got one script (`rgui.ps1`, wrapped by `rgui.cmd`) with commands
`doctor`, `fetch` (all downloads into a cache, SHA-256 checked,
resumable), `full`, `quick` (fast rebuild of only what changed), `run`,
`dev` (rebuild + start), `test [-Real] [-Gui] [-Installer]`, `package`
(the USB-stick layout), `deploy -Drive E:`, `installer`, `clean`. Each
command ends by printing where its output went. Build outside the source
tree if the host's build cannot cope with spaces in paths. Make the same
kind of script for the host: the owner wants the edit-build-run loop as
fast as possible.

A GitHub Actions workflow builds, tests and publishes the installer when
a `v*` tag is pushed (actions pinned by commit SHA, toolchain pinned by
version and SHA-256, `fetch -NoModel` in CI). **Do not push tags or
trigger it yourself** (see 10).

---

## 9. Notes for the likely targets

These are starting points. Verify each against the fork's code before
relying on it.

### GUSEK (GLPK/MathProg IDE for Windows)

- Built on the SciTE editor (Scintilla, C++/Win32), with GLPK's
  `glpsol` run on the current model and its output shown in an output
  pane. Find where SciTE builds its menus and its output pane; the panel
  can be a second pane or a separate docked window. SciTE's Lua
  extension may be enough for menu glue, but the HTTP worker, streaming
  and rendering belong in C++.
- The subject is linear/integer programming in GNU MathProg, not R:
  rewrite the system prompt (fence language: ```mathprog or ```ampl —
  Copy code takes any fenced block), let the course folder also read
  `.mod` and `.dat`, and map Attach to: *Last solver output/error*
  (glpsol's output pane), *Current model* (the open `.mod`), *Recent
  output*. There is no plot device; *Current plot* becomes *Picture
  file…*/clipboard only, unless the fork renders graphs.
- Scintilla is UTF-8 capable: make sure the document and the panel run
  in UTF-8 mode (lesson 2).
- To editor: insert at the caret as one undo action
  (`SCI_BEGINUNDOACTION`/`SCI_REPLACESEL`/`SCI_ENDUNDOACTION`).

### RStudio

- A C++ backend (`rsession`) and a front end in GWT/Java and TypeScript,
  shown in a browser or the Electron desktop shell. The panel is a
  front-end pane; the model server should be supervised by the backend
  (it already starts and supervises processes) or by the desktop shell,
  bound to loopback, with a backend RPC/endpoint streaming the answer to
  the front end. Keep R itself out of the worker (lesson: never touch
  the R session off its own thread).
- Check whether the fork already has an assistant/Copilot/chat pane and
  its own process supervision and settings UI; reuse its plumbing and
  keep this feature's behaviour (section 2) rather than adding a second
  chat.
- Attach maps naturally: the console's last error, the active source
  document, console output, and the Plots pane's current plot (export it
  from the graphics device, not by screenshot — lesson 1).
- Rendering is HTML: Markdown to sanitised HTML, code blocks with a copy
  button are fine, and Unicode comes for free; the scroll/selection
  rules (2.5) still apply.
- RStudio is cross-platform; the target machines are Windows, so ship
  the Windows llama.cpp CPU build first and make the server path
  configurable for others.

---

## 10. Rules for working in the fork

- Commit locally on the default branch (`main`/`master`), no feature
  branches; one or two clean conventional commits per feature
  (`feat(...)`, `fix(...)`, `test: ...`); squash or amend unpushed
  fix-ups.
- **Never push**, open or edit pull requests, push tags, trigger
  workflows or publish releases. Treat GitHub as read-only.
- **No `Co-Authored-By` or any AI attribution** in commits, files or
  docs.
- Never print or log secret values or tokens; check only that they exist.
- Keep the host's code style; comment like the surrounding code.
- Compile the new code with warnings as errors in a separate check if
  the host does not already.
- Before deleting or overwriting anything you did not create, look at it.
- When you finish, report what was tested and seen working, and what is
  built but not tested, separately (2.9).
