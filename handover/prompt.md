Historical implementation handover. Current usage and status are documented in
README.md, docs/README.md, and VALIDATION.md; the source is now in this repository.

Work in the gusek checkout, on main. Implement the same local
offline assistant already present in the RGui and RStudio forks, adapted to
GNU MathProg/GLPK and student optimization coursework.

Read these files completely before implementation:

- handover/ai-assistant-porting-guide.md: the behavioral contract, pinned
  model/protocol, lessons, tests and repository rules.
- handover/gusek-implementation-plan.md: checked workspace/source locations,
  native Win32 design, exact SciTE hooks, MathProg adaptations, phases and gates.

The repository presently holds the packaged application. The real editor
source is in ../Gusek-fork/scite and ../Gusek-fork/scintilla. Phase 0 brings that
source into this repository and establishes a working baseline build. Do not
modify a prebuilt EXE as a substitute for integrating the real source.

Use RGui's native assistant as the code reference, and RStudio's implementation
as the behavioral/test reference. The plan explains licenses and reuse.
Inspect the actual source before trusting line numbers. Keep the assistant in
a native docked pane and keep the editor and solver usable during every model
operation. Generated code is never executed automatically.

Pay particular attention to the existing code.page=0 setting, legacy buffer
encodings, the global Ctrl+Enter autocomplete accelerator, three-pane splitter
geometry, MathProg solver-run/error extraction, unsaved models, and separate
process/data ownership. These are verified integration issues, not optional polish.

Proceed through the plan's phases, building and testing as you go. The specified
defaults authorize routine implementation choices; do not stop at a proposal
or ask for repeated confirmation. If the baseline compiler fails, report the
actual error and repair the build or use the documented fallback.

Use isolated test profiles, ports and files. Never overwrite the owner's
working configuration, notes, history or model caches. Validate cached assets
before reusing them, and keep multi-GB models outside the installer.

Commit locally on main with a few conventional commits per feature; no branches,
pushes, PRs, workflow runs, releases or other GitHub writes. Do not output secret
values/token fragments or add attribution trailers/footers. Keep original
copyright and license notices.

Finish with a working local build, portable package, per-user installer,
locally validated version-tag artifact workflow, and clear user/developer
documentation. Report separately what was tested and seen working, what is
built but untested, and what remains blocked. Do not call a stub or fake-server
demo the completed assistant.
