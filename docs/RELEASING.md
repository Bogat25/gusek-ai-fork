# Windows installer and portable releases

Fork package versions are independent of the legacy GUSEK, SciTE, and GLPK
version numbers. Use the same fork version for the tag, installer, and ZIP.

[windows-installer.yml](../.github/workflows/windows-installer.yml) runs on
version-tag pushes such as `v0.1.0`, `0.1.0`, and `v0.2.0-rc1`, and supports manual
dispatch. The workflow must be committed at the tagged revision. A local tag
alone does not trigger GitHub; the owner must publish it.

## Pipeline

1. Validate the version, fetch verified CPU/compiler assets, and build
   Scintilla/GUSEK from source.
2. Run native, wrapper, and actual GUI regressions without multi-GB models.
3. Build the installer, portable ZIP, and checksums.
4. Verify isolated per-user install, launch, upgrade, uninstall, and portable use.
5. For tag pushes, verify checksums again, upload the complete asset set to a
   draft GitHub release, and publish it. Suffix versions become prereleases.

Manual runs create downloadable Actions artifacts without publishing a release.
Existing releases are not overwritten. An interrupted draft may require owner
cleanup before retrying. Build jobs have read-only repository permissions; only
the tag publisher has release-write permission and uses GitHub's job token.

## Before tagging

Commit the intended source/workflow changes, run default and real-model checks,
build the intended version, and run installer checks without elevation. Resolve
or explicitly disclose the clipboard verification gap in [VALIDATION.md](../VALIDATION.md).
Inspect model exclusions, notices, private data, version metadata, and checksums.
Record the actual suite results, including skips and failures.

Rerunning a failed job uses the same tagged source; later source fixes require a
new tag. Do not move a public release tag silently. A locally passing pipeline
is separate from the status of a hosted run.

## Assets

- `gusek-ai-<version>-setup.exe`: per-user installer, no administrator prompt.
- `gusek-<version>-portable.zip`: extract into a writable folder and use
  `Start-Gusek.cmd`.
- Adjacent `.sha256` files for both assets.

Model weights are downloaded on first use and are absent from both packages.
The current release is unsigned. Compare downloads with their checksums:

```powershell
Get-FileHash -Algorithm SHA256 .\gusek-ai-0.1.0-setup.exe
Get-Content .\gusek-ai-0.1.0-setup.exe.sha256
```

Checksums detect corruption or mismatched files; they do not replace a trusted
download source or code signing. See [BUILDING.md](BUILDING.md) for local commands
and [THIRD-PARTY.md](../THIRD-PARTY.md) for source and license provenance.
