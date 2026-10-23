# Windows offline installer releases

GUSEK AI uses the shared [SourceForge release infrastructure](../release/SOURCEFORGE.md).
Complete the one-time account, project, SSH-key and GitHub environment setup
in that guide before publishing a new version tag.

The [Windows workflow](../.github/workflows/windows-installer.yml) builds the tagged source, fetches both pinned
models, creates the complete offline installer, and runs the repository checks.
The installer suite verifies model hashes, local text/picture inference,
application launch, upgrade preservation and uninstall under a separate identity.
A serialized Linux publisher verifies the public SourceForge copies before
selecting the stable Windows default and pruning older files in that channel.
Prereleases use their own channel and never replace the stable default.

GitHub releases contain SourceForge download links and the small checksum file.
The multi-GB installer is hosted on SourceForge. Installer Actions artifacts
expire after one day; diagnostic logs have separate retention. SourceForge
credentials are confined to the `sourceforge` GitHub environment.

## Local packaging

```powershell
.\gusek.cmd fetch -BuildRoot D:\gusek-build
.\gusek.cmd full -BuildRoot D:\gusek-build
.\gusek.cmd installer -Offline -Version 0.2.0 -BuildRoot D:\gusek-build
.\gusek.cmd test -Installer -Offline -BuildRoot D:\gusek-build
```

Use the same fork version for the tag and installer, with three numeric
components. A suffix such as `-rc.1` denotes a prerelease.
The complete installer is named `gusek-ai-<version>-offline-setup.exe`.
Omit `-Offline` for the smaller first-use-download installer. GUSEK's optional
portable ZIP stays slim and can remain a GitHub release asset.

Before tagging, review the actual local validation, licenses and checksums.
A locally passing build is separate from a successful hosted publication.
The owner pushes the committed changes and a new unique tag; a local tag alone
does not trigger the workflow. A rerun uses the tagged source. Use a new version
for changed bytes rather than moving a published tag or overwriting a release.

Packages are currently unsigned. Offline installers require no additional model
download, but inherited application features and user code may still use the
network. Settings, notes, and custom models are preserved by upgrades and
uninstall; the two pinned default models belong to the distribution.
