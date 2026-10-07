# Native local assistant architecture

## Request and UI flow

The assistant is a native child pane docked beside the SciTE editor/output area.
It does not replace solver output and does not require Electron or a browser.
The host supplies snapshots of the current unsaved model and solver output.
Workers handle model loading, generation, and downloads; UI updates return to
the window thread.

```text
SciTE editor/solver -> explicit attachment -> editable question
  -> prompt + bounded notes + bounded text/image history
  -> native worker -> 127.0.0.1:28713 -> llama-server -> local model/projector
  <- HTTP/SSE parser -> UI messages -> RichEdit transcript
  -> Copy code / To editor -> user review -> explicit GLPK run
```

The assistant performs bounded keyword lookup in designated reference folders.
It is not a filesystem-wide index, an Internet search engine, or a training job.
Reference text and model output are untrusted suggestions. Automatic solver/code
execution is outside the assistant's behavior.

## Source map

| Location | Responsibility |
| --- | --- |
| [SciTEWin.cxx](../scite/win32/SciTEWin.cxx), [SciTEWin.h](../scite/win32/SciTEWin.h) | Menu, shortcut, pane lifecycle, layout and host callbacks |
| [GusekAiPane](../scite/win32/ai/GusekAiPane.cxx), [GusekAiHost.h](../scite/win32/ai/GusekAiHost.h) | Controls, request state, explicit attachments and editor insertion |
| [GusekAiConfig](../scite/win32/ai/GusekAiConfig.cxx) | Defaults, user overrides, paths, system prompt and reference selection |
| [GusekAiModel](../scite/win32/ai/GusekAiModel.cxx) | Health checks, CPU process launch, ownership, startup and cleanup |
| [GusekAiHttp.h](../scite/win32/ai/GusekAiHttp.h), [GusekAiProtocol](../scite/win32/ai/GusekAiProtocol.cxx) | HTTP transport, JSON/SSE, generation framing |
| [GusekAiDownload](../scite/win32/ai/GusekAiDownload.cxx) | Range downloads, cancellation, integrity and recovery |
| [GusekAiImage](../scite/win32/ai/GusekAiImage.cxx), [GusekAiRender](../scite/win32/ai/GusekAiRender.cxx) | GDI+ picture normalization, thumbnails/viewer, Unicode/Markdown rendering |
| [ai/defaults](../ai/defaults) | Shipped configuration, editable-prompt template, reference scaffold |
| [tests/ai](../tests/ai) | Native/control, GUI, real-model, wrapper and installer regressions |
| [gusek.ps1](../gusek.ps1), [scripts](../scripts), [packaging](../packaging) | Source mirror, toolchain discovery, verified assets, distribution and releases |

## Ownership and data

Each application has its own profile and owned server. Healthy externally
started servers can be reused without killing them on exit. Downloads and model
workers must stop before UI state is destroyed. Chat history is in memory;
configuration, notes, prompt, models, and diagnostics live in the selected data
directory. The portable launcher selects `work/assistant` and `work/config`.

RichEdit can embed a pasted bitmap as an OLE object without attaching it to a
model request. The paste correction routes native Ctrl+V/Shift+Insert through the
attachment handler so images become actual request payloads. The regression
contract checks decoded pixels and follow-up bytes, not just visible thumbnails.
The new full clipboard checks remain unverified in the last recorded session.

## Fork relationship and maintenance

RGui AI uses C/GraphApp with R context; RStudio AI uses the existing Chat
session/GWT/Node stack; GUSEK AI uses C++/SciTE with MathProg context. Their default
model ports are 8713, 18713, and 28713 respectively. Profiles and process ownership
are independent; public model assets may be reused by build tools only after
verification.

Routine upstream synchronization is not planned. Update dependencies deliberately,
preserve notices, and validate actual model requests and lifecycle behavior after
protocol, image, or worker changes. The historical handover files explain early
design decisions; current implementation and validation determine behavior.
