# Playable courtyard verification â€” 9 October 2026

Host: Windows x64, MSVC 19.51, CMake 4.4, NVIDIA GeForce RTX 3060 Ti.
First-party targets use C++23, `/W4 /WX /permissive-`, and disabled exceptions.

| Check | Evidence |
|---|---|
| Full Windows Debug build | Passed: D3D12 editor/runtime, Vulkan source/UI checks and DXIL/SPIR-V shaders |
| Windows Debug CTest | 14/14 passed, including editor-only and editor-with-demo modes and optional Windows Vulkan runtime smoke |
| Windows ASan CTest | 14/14 passed; editor and runtime also completed 600 D3D12 validation frames with both resizes |
| Playable course | Public-input route collects all four orbs and finishes; grounded/held jump, sprint, wall collision, camera-relative movement, respawn, restart, win freeze and bounded hitch behavior pass |
| Gameplay allocation guard | Full course, restart and checkpoint respawn make zero C++/Flecs allocation attempts in the measured interval |
| Embedded artwork | Original PNG SHA-256 matches the supplied file: `4fb6920bbef28bfff1bce038ae530e473f9d7e0c27f10dc0e73a95b483db0170`; decoded dimensions and RGBA samples pass |
| Document template | Replacement starts a dirty unnamed document, clears selection/history, assigns new ActorIds and rejects self-move without replacing the world |
| Integrated editor smoke | Opens/closes the dockable demo panel alongside the editor; player reaches the first checkpoint while the nine-actor smoke edit world stays intact; viewport targets retire and reattach |
| Multiple view constants | Eight persistent scopes succeed; ninth returns exhausted; later CPU frame-slot reuse succeeds without overwriting either camera |
| Existing world-building contracts | Undo/redo, dirty revisions, Unicode paths, malformed loads, recovery, simulation restoration, six primitives, actual ImGuizmo gestures and zero-allocation scene extraction still pass |
| Visual QA | Direct D3D12 readback reviewed for the crest, demo HUD/start card, platform depth, docking layout, Outliner and inspector |

The [verification workflow](https://github.com/jamiejustcodes/Souls-Engine/actions/workflows/verify.yml)
builds Windows MSVC and Linux GCC 14 / Clang 19. Linux jobs enable ASan/UBSan,
run all nine non-GPU contracts and exercise both editor-with-demo and editor-only paths, plus runtime modes under
Xvfb/Mesa with Vulkan validation. The workflow badge reports the current revision.

D3D12 validation errors and Vulkan validation callbacks fail the smoke harness.
The optional Windows Vulkan runtime uses this host's NVIDIA driver without the
Khronos validation layer; Linux CI supplies that validation coverage. Engine and
shell code is instrumented; fetched third-party libraries retain their own policy.
The allocation guards cover gameplay and scene operations, excluding ImGui, SDL,
native dialogs, file I/O, PNG decoding and driver work.

Committed images are actual GPU captures. The demo smoke advances through its
first checkpoint before frame 90, so its image shows one collected orb. The
playground smoke adds four parts to cover every mesh; normal editor mode has the
two original primitives. Smoke mode never accesses user recovery files. Frame/FPS
readings are live telemetry, not benchmark claims.

## Reproduce

```powershell
cmake --preset windows -DSOULS_VULKAN_COMPILE_CHECK=ON
cmake --build --preset windows --parallel
ctest --preset windows --output-on-failure
.\tools\CaptureEditor.ps1
.\tools\CaptureEditor.ps1 -Playground -Output build/editor-playground.png
cmake --preset windows-asan -DSOULS_VULKAN_COMPILE_CHECK=ON
cmake --build --preset windows-asan --parallel
ctest --test-dir build/windows-asan -C Debug --output-on-failure
.\build\windows-asan\Debug\SoulsEditor.exe --validation --smoke 600
.\build\windows-asan\Debug\SoulsRuntime.exe --validation --smoke 600
```

## Remaining human UI/device checks

Automated contracts drive the controller, document, actual gizmo and GPU paths.
The native mouse-capture button, OS file picker and all docking/focus combinations
still need human checks on supported desktops:

- Demo panel Play, WASD, Space, sprint and mouse orbit; Esc/focus loss pauses and
  releases the cursor. Resume, Restart and completion should keep correct progress.
- Start in the editor, open/close Demo, dock/tab/resize it and detach to another
  monitor. Both cameras must stay independent; only the demo window captures game
  input. Selection/history must persist. Edit demo must offer Save/Discard/Cancel.
- About should show the complete logo. Resize/minimize, detached panels and mixed
  DPI transitions must keep the crest valid and text readable.
- RMB flight, Q/W/E/R tools, multi-selection, gizmo snapping/cancellation, part
  placement, undo/redo, grouping and locks must remain usable while editing.
- Open/Save/Save As, cancelled pickers, inaccessible files, malformed loads,
  recovery and closing a dirty level must preserve the existing safeguards.

Imported assets, shadows, deferred targets, calibrated PBR exposure, general
physics, scripting and gameplay authoring for edited levels remain future work.
This demo uses authored axis-aligned collision and existing primitive rendering.
