# Repository privacy review

Review date: **2026-10-07**. This report records categories and repository
locations without copying personal paths, email addresses, secrets, or token fragments.

## Scope

The initial tracked inventory contained 790 files. The review checked tracked
working-tree bytes, staged documentation/removals, Git author/committer metadata,
commit messages, and 779 unique blob objects reachable from local refs before
this documentation commit. Common token/private-key/credential patterns,
maintainer-email matches, home/workspace paths, and selected UTF-16 encodings
were included. Binary bytes were scanned as well as text.

## Findings and remediation

| Finding | Action |
| --- | --- |
| Private workspace path in `handover/prompt.md` and `handover/gusek-implementation-plan.md` | Replaced with a checkout-relative description; marked the plans historical |
| Private build/workspace paths embedded in `scintilla/bin/SciLexer.dll` and `scintilla/bin/Scintilla.dll` | Removed generated DLLs from version control; fresh source builds produce required binaries |
| Machine-specific evidence directories in public validation notes | Replaced with build/audit-root placeholders without changing results |
| Inherited author/contact addresses and an AutoIt documentation URL | Retained original attribution and examples; no maintainer credential identified |

No live credential or personal mailbox belonging to the local maintainer was
identified by these checks. Ignore rules now exclude generated DLLs (while
retaining the inherited GLPK redistributables), models, private profiles/reference
folders, environment files, credential containers, and dumps. The root legacy
GUSEK executable is clearly distinguished from the fresh AI build in the README.

## Historical exposure remains

The private workspace strings and old generated DLLs **remain in earlier commits
and any tags/forks/caches referencing them**. Removing them in the current tree
does not erase that exposure. No history rewrite, force push, tag change, release
deletion, or other GitHub write was performed. Existing release artifacts were
not rebuilt or purged during this documentation task.

Git authorship is also public, including public no-reply identities. If stronger
anonymization of published history is needed, it requires a separate coordinated
cleanup across refs, hosting caches, forks, and downloaded artifacts. Exposed
credentials would additionally require provider revocation, not just a rewrite.

## Limits

Pattern scanning is not proof of absence. The review did not OCR every image,
decode every compressed PDF, or assess arbitrary custom secrets or all personal
identities. It covered locally reachable refs, not remote-only/unreachable
objects, Actions logs, hosted caches, or every release binary. Ignored local
files were not part of the tracked public inventory.

Before publishing, review screenshots, metadata, new binaries, staged files,
and Git identity privately. Keep real notes and model/server diagnostics outside
Git; use synthetic reproductions. See [SECURITY.md](../SECURITY.md).
