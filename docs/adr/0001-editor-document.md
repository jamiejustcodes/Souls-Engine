# Editor document owns editing state

Status: accepted, 9 October 2026.

The workspace previously changed Flecs actors directly and maintained a separate
simulation snapshot. Adding undo, persistence and multi-selection to each widget
would spread identity and restore rules across unrelated UI paths.

EditorDocument borrows the existing Scene object and owns stable ActorIds,
selection, dirty revisions, 64 fixed-capacity history operations and the simulation
snapshot. Viewport and Details adapters begin an edit, preview from gesture-start
values and commit once. Structural commands use the same document boundary.
Rendering still reads Scene and generation-checked handles; it does not depend on
editor history or file dialogs.

Snapshots keep actor identity and selection by ActorId. Restore preserves handles
where possible and remaps recreated actors. Load parses and validates a candidate
scene before replacing the borrowed Scene's contents. Saves write and flush a
sibling temporary file before atomic replacement. Native file dialogs and callback
lifetime stay in a separate SDL adapter. File I/O and structural edits run before
GPU frame recording; previews use bounded storage without per-drag allocation.

Groups are logical folders, not transform parents. Actor transforms remain TRS
with world-space position and Euler degrees. Group gizmos rotate around a shared
pivot; group scaling adjusts spacing and local scale without manufacturing shear.
Headless document and transform tests cover these contracts, and a separate ImGui
harness drives real ImGuizmo gestures with the engine's camera convention.

This costs a bounded history allocation at document creation and limits editor
levels to 256 actors. Larger scenes will need command payload history or chunked
snapshots; the renderer's 4096-actor pool remains independent of that decision.
