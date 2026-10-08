# Phase 2 verification — 8 October 2026

Host: Windows x64, MSVC19.51, CMake4.4, NVIDIA GeForce RTX3060Ti.
First-party targets use C++23, `/W4 /WX /permissive-`, and disabled exceptions.
This report separates compilation, runtime checks and manual inspection.

| Check | Evidence |
|---|---|
| Full Debug build | Passed, including production D3D12 editor/runtime |
| Vulkan source checks | Backend and UI bridge compiled on Windows |
| Shader checks | All four HLSL entries compiled to DXIL and SPIR-V; reflected vertex locations0/1, uniform set0/binding0, frame offsets0–240 and object offsets0/64/80 match C++ |
| Debug CTest | 6/6 passed with the optional Vulkan GPU check enabled |
| ASan build/tests | 6/6 passed with the optional Vulkan GPU check enabled |
| D3D12 validation | Editor/runtime 120-frame smoke passes; editor additionally completed 600 frames under ASan |
| Vulkan GPU smoke | Optional Windows Vulkan runtime completed 120 frames and both resizes on the NVIDIA driver |
| Vulkan readback | Engine-owned offscreen color captured; cube, sphere, grid, depth and directional shading inspected |
| Scene contracts | Generation safety, duplicate/delete, visible/hidden transformed picking, invalid transforms, SoA live values, view/projection checks passed |
| Editor workflow | Smoke exercises select/edit, duplicate/delete, simulation snapshot, temporary actor removal and Stop restore |
| Allocation guard | 2000 live transform updates and SoA extractions: zero C++/Flecs allocation attempts |
| Visual QA | Direct D3D12 GPU readback of the complete editor: ribbon, selected actor, axis gizmo, XYZ Details, content tree/tiles and shared developer tabs |

D3D12 validation errors and Vulkan validation callbacks fail the smoke harness.
The Windows Vulkan smoke ran without validation: this host does not have
`VK_LAYER_KHRONOS_validation`. Requesting it returns an explicit install error.
Linux GCC/Clang, Linux window systems, Linux ASan/UBSan and Linux Vulkan/editor
execution have not run locally because no Linux/WSL environment is installed.
The existing Ubuntu CI jobs configure/build the Linux sanitizer preset and run
both shells with Mesa and Vulkan validation under Xvfb. Their results remain
pending until CI executes. Windows Vulkan rendering does not certify Linux.

ASan runtime DLLs deploy beside every test executable, including the optional
Vulkan check. Third-party source libraries retain their own warning/sanitizer
policies. First-party engine and shells are instrumented. The allocation guard
covers engine scene operations, not allocations internal to ImGui, SDL or drivers.
The direct GPU captures are `build/editor-playground.png` and
`build/vulkan-playground.png`; they contain engine output, not desktop content.
Frame/FPS numbers in captures are live measurements, not benchmark claims.

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

## Manual interaction checks

These are the remaining human UI/device checks; controller-level smoke does
not emulate every mouse action. Verify RMB flight, wheel speed, Q/E and Shift;
click X/Y/Z to align, click either mesh to select, and F to focus. Confirm typing
F in actor labels or search never moves the camera. Rename, hide, duplicate and
delete via Outliner; edit XYZ, snapping, mesh/material slots, color and sun lux.
Play, pause, eject, edit and stop; confirm edit-world restoration. Filter folders
and assets and double-click a mesh tile to place it. Open Output Log while
Telemetry is already visible: it should activate Log and keep the tools open.
Collapse/reopen Content Browser; use Window to toggle tools. Detach/redock,
close detached panels, minimize/restore, and move between monitors with different
DPI. Resize down to the supported DPI-scaled minimum. Linux rendering and input
checks must also run on a Linux display or Xvfb where applicable.

Current stage implements built-in meshes/material instances, Blinn-Phong shading,
procedural sky/grid, depth, static backend dispatch and explicit uploads. Full
landscape sculpting, modeling topology, imported assets, calibrated physical
exposure, shadows, deferred passes, undo/save and shader authoring remain later
stages; none are presented as completed production features.
