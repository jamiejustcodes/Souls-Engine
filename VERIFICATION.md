# World-building verification — 9 October 2026

Host: Windows x64, MSVC 19.51, CMake 4.4, NVIDIA GeForce RTX 3060 Ti.
First-party targets use C++23, `/W4 /WX /permissive-`, and disabled exceptions.

| Check | Evidence |
|---|---|
| Full Windows Debug build | Passed: D3D12 editor/runtime, Vulkan source/UI checks and DXIL/SPIR-V shader compilation |
| Windows Debug CTest | 10/10 passed, including the optional Windows Vulkan runtime smoke |
| Windows ASan | 10/10 passed; editor also completed 600 validation frames and both resizes |
| Document contracts | Transaction coalescing/cancel, undo/redo, branching and eviction, clean/dirty revisions, stable IDs and selection restoration |
| Level files | All actor kinds, 256-actor capacity, Unicode paths, save/load path aliasing, recovery, and malformed-file rejection without replacing the current world |
| Primitive contracts | Six shape-aware ray intersections, transformed bounds, locked picking, logical groups and material SoA updates |
| Transform tools | Mirrored/nonuniform TRS, Euler gimbal cases, shared-pivot rotation/scale, singular/shear rejection and snapped elevated/rotated-floor placement |
| Actual widget gestures | Headless ImGui/ImGuizmo mouse press/drag/release for move, rotate and scale using the engine camera; active-gesture cancellation |
| Integrated editor smoke | Coalesced Details edits undo/redo, duplication, simulation restoration and temporary actor removal; all six meshes render |
| Allocation guard | 2000 live scene transform updates and SoA extractions with zero C++/Flecs allocation attempts |
| Visual QA | Direct D3D12 GPU readback: docked workspace, tabs, inspector, orientation/transform gizmos and six part thumbnails |

[Implementation CI run](https://github.com/jamiejustcodes/Souls-Engine/actions/runs/37867036927)
passed Windows MSVC, Linux GCC 14 and Linux Clang 19. Both Linux jobs built with
ASan/UBSan, passed the seven non-GPU contracts and ran the runtime and editor for
120 frames under Xvfb/Mesa with Vulkan validation enabled. The final revision is
also covered by the repository's [verification workflow](https://github.com/jamiejustcodes/Souls-Engine/actions/workflows/verify.yml).

D3D12 validation errors and Vulkan validation callbacks fail the smoke harness.
The optional Windows Vulkan runtime smoke uses this host's NVIDIA driver without
`VK_LAYER_KHRONOS_validation`; the Linux CI runs supply that validation coverage.
Third-party libraries retain their own warning/sanitizer policies. Engine and
shell code is instrumented. The allocation guard covers scene operations and
extraction, not ImGui, SDL, native dialogs, file I/O, or driver allocations.

The committed editor image is an engine GPU capture, not desktop content. Its
frame/FPS readings are live telemetry rather than benchmark claims. Smoke tests
add four parts to exercise every mesh; normal launch starts with the two original
playground primitives. Smoke mode disables user preference/recovery access.

## Reproduce

```powershell
cmake --preset windows -DSOULS_VULKAN_COMPILE_CHECK=ON
cmake --build --preset windows --parallel
ctest --preset windows --output-on-failure
.\build\windows\Debug\SoulsEditor.exe --validation --smoke 120
.\build\windows\Debug\SoulsRuntime.exe --validation --smoke 120
.\build\windows\Debug\SoulsVulkanRuntimeCheck.exe --smoke 120 --capture build/vulkan-playground.bmp
.\tools\CaptureEditor.ps1
cmake --preset windows-asan -DSOULS_VULKAN_COMPILE_CHECK=ON
cmake --build --preset windows-asan --parallel
ctest --test-dir build/windows-asan -C Debug --output-on-failure
.\build\windows-asan\Debug\SoulsEditor.exe --validation --smoke 600
```

## Remaining human UI/device checks

Automated tests drive the actual gizmo widget, document transactions and graphics
smoke paths. They do not automate the operating system's file-picker UI or every
docking/input combination. Verify these on the supported desktop environments:

- RMB flight, vertical movement, boost, wheel speed and F focus; Q/W/E/R tool
  shortcuts must not interfere with typing or flight.
- Ctrl/Shift multi-selection, empty-space marquee, axis/plane/ring gestures,
  local/world mode, snapping, Escape cancellation and focus-loss cancellation.
- Drag a part from Build and Content Browser. Confirm the preview, floor alignment,
  delivery, selection, single undo, and redo. Numeric multi-edit should stay relative.
- Rename, hide, duplicate, delete, group/ungroup and lock/unlock through Outliner.
  Double-click a group to select its members. Undo/redo should restore values and IDs.
- Native Open/Save/Save As, cancelled picker, inaccessible destination and malformed
  files. A failed action should display an error and keep the current world.
- Close/new/open with dirty state: Save/Discard/Cancel, including cancelling Save As.
  Idle recovery, next-launch Restore/Discard/Keep, and saving a restored unnamed level.
- Play, pause, eject, edit and stop; confirm the edit-world history and selection return.
- Content/tool tabs, detach/redock, detached-window close, minimize/restore,
  minimum-size ribbon layout, and mixed-DPI monitor transitions.

Imported assets, shadows, deferred targets, physically calibrated PBR exposure,
physics, scripting, landscape sculpting and topology authoring remain future
rendering/editor milestones. This milestone delivers the built-in world-building
workflow and level persistence.
