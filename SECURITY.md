# Security and private data

The maintained scope of this fork is the Windows local assistant. No separate
security support promise is made for old releases or inherited platform code.

## Reporting

Use this repository's **Security > Report a vulnerability** if enabled. Otherwise,
open a minimal issue asking for a private contact route without sharing exploit
details, credentials, or personal data. No public contact email is configured here.

Revoke or rotate exposed credentials with their providers. Removing files does
not erase earlier commits, forks, caches, workflow artifacts, or downloaded
binaries. History cleanup is a separate coordinated action.

## Local AI boundary

The assistant uses loopback for inference and configured public hosts for model
and runtime downloads. Keep the endpoint on `127.0.0.1`; it is not an authenticated
public service. SciTE extensions, external tools, and user code have their own
network behavior. Review generated MathProg before running the solver.

Prompts, reference excerpts, model/solver text, pictures, clipboard contents,
and runtime logs can contain private data. These files are not encrypted by the
application. Portable mode is not a guarantee that the host records no activity.
Untrusted notes and attachments can influence generated answers.

Use synthetic examples for reports. Do not upload real assistant profiles,
coursework, full logs, memory dumps, or screenshots without reviewing their
content and metadata. Generated binaries can contain build paths and symbol
references even when source text looks clean.

See the dated [privacy audit](docs/PRIVACY-AUDIT.md) for repository findings,
remediation, and what remains in historical commits.
