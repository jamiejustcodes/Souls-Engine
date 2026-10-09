# Souls Engine

## Platform
Native desktop: Windows and Linux, SDL3 and Dear ImGui.

## Users and purpose
Game engine developers inspecting real-time rendering, scene data and frame budgets.
A UE5-style editing workspace pairs with approachable studio building tools.
Startup opens the editor. Souls Courtyard is a playable third-person course in
a movable, dockable demo panel available from the toolbar and Window menu. The
panel runs independently and can open its authored geometry for editing.

## Constraints
D3D12 with Agility on Windows; Vulkan 1.3 on Linux. Static backend selection,
no engine exceptions, frame arenas and generation-checked pools, Flecs ECS,
hermetic module interfaces and explicit verification. 3D first; future 2D is
a separate renderer layer. The default telemetry target is 60 Hz / 16.67 ms.

## Brand commitments
Souls Engine; charcoal #1E1E24 and #2B2B36, violet #6C5CE7,
high contrast, DPI awareness, 8px grid and a dockable professional workspace.

## Evidence and open decisions
Indexed 3D meshes, camera/light constants, depth and a procedural grid are available. GBuffer, shadows, shader authoring,
GPU-driven scene rendering and full pass timelines belong to later stages.
Do not present absent passes or illustrative GPU performance as live results.

The maintainer-supplied black-and-white Souls crest/wordmark is the official
artwork. Preserve the PNG; crop the crest only for compact UI and window icons.
