# Contributing to GUSEK AI

This independent hard fork focuses on local AI for MathProg/GLPK. Routine
upstream synchronization is not planned. Use this repository's Issues tab for
fork-specific bugs and proposals; upstream authors do not support this distribution.

Include the tag/commit, Windows version, installation type, reproduction steps,
expected/actual behavior, and a small synthetic model. For assistant problems,
include model/runtime versions, non-secret settings, attachment source, and
whether the thumbnail and actual request behave differently.

Do not publish personal paths, emails, private notes, real solver data, clipboard
contents, credentials, user profiles, or unreviewed logs/screenshots. See
[SECURITY.md](SECURITY.md) for sensitive reports.

Read [architecture](docs/ARCHITECTURE.md), [build/tests](docs/BUILDING.md), and
[validation](VALIDATION.md) before changing the native integration. Preserve
UI-thread ownership, cancellation, encoding-safe insertion, bounded context/images,
explicit user-controlled execution, process ownership, and upgrade data handling.
Run regressions appropriate to the change and report blocked/skipped checks.

Keep generated editor/Scintilla binaries, symbols, downloaded runtimes/models,
and test profiles outside Git. Retain original copyright/license notices and
document dependency changes. External contributors may use GitHub's normal
fork/pull-request process; maintainer publishing is a separate decision, and this
guide does not authorize automated GitHub writes.
