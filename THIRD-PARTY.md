# Included software and notices

GUSEK and the bundled GNU GLPK 4.65 solver use GPL version 3 or later.
The full GPL text is included as `COPYING`. Original GUSEK information is in
`README`. GLPK source is available from
[GNU's GLPK distribution](https://ftp.gnu.org/gnu/glpk/glpk-4.65.tar.gz).
The GUSEK fork's source corresponds to the repository revision recorded by its
release tag; GitHub provides a source archive for that tag.

Scintilla and SciTE retain their original notices, included in
`licenses/scintilla-LICENSE.txt` and `licenses/scite-LICENSE.txt`.

The CPU runtime is llama.cpp release `b11153`, retrieved from the upstream
[release](https://github.com/ggml-org/llama.cpp/releases/tag/b11153) and verified
against the SHA-256 pin in `gusek.ps1`. Its MIT notice is included as
`licenses/llama.cpp-LICENSE.txt`, copied from that release's
[LICENSE](https://github.com/ggml-org/llama.cpp/blob/b11153/LICENSE).
The runtime archive's LLVM OpenMP notice is retained as
`ai/llama/LICENSE-LLVM-OpenMP`.

Model files are excluded from both packages. The default downloads are the
Qwen3.5-4B Q4_K_M model and F16 multimodal projector; their URLs, sizes and
SHA-256 pins are recorded in `ai/defaults/GusekAI.ini` and `gusek.ps1`.
Model documentation and notices are provided by the upstream repositories at
those URLs.
