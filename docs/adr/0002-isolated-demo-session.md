# A shared demo session owns gameplay in an isolated scene

Status: accepted, 9 October 2026.

A playable startup course needs movement, collision, progression and camera
rules in both the editor and standalone runtime. Putting those rules into the
workspace would duplicate them in the runtime and make headless validation
unreliable. Playing inside EditorDocument would also mix temporary game state
with selection, history and dirty revisions from ADR-0001.

SoulsDemo is a deep module with a small Session interface: create, tick, reset,
state and camera. Its implementation owns the authored course, bounded solid
and collectible storage, fixed 120 Hz steps, sweep collision, camera obstruction,
checkpoint respawn and completion. Session borrows an empty Scene; that object
and its actors must outlive the session. Construction is cold and transactional.
Tick/reset update existing actors without structural changes or allocation.

SDL runtime and ImGui workspace are two real input/presentation adapters. The
editor holds a separate demo world, choosing that scene and camera for rendering
while the demo is active. Esc/focus loss releases relative mouse mode and pauses
progress. Switching workspaces preserves document selection, edits and history.

Edit demo constructs a fresh authored candidate and unlocks its actors. The
existing Save/Discard/Cancel flow protects a dirty document. replace_scene
validates before moving the candidate into the borrowed Scene address, assigns
fresh ActorIds and starts an unnamed dirty document with empty selection/history.
The installed geometry is an editor template; it does not serialize demo rules.

The interface is the test surface: headless tests complete the course with normal
input, compare render cadence, respawn at checkpoints, restart and reject invalid
input. C++ and Flecs allocator hooks guard the measured gameplay interval. GPU
smoke adapters advance the same session and the editor switches both workspaces.

The controller is kinematic and restricted to authored AABB solids. A general
physics module, imported collision and scripting are separate future decisions.
The original logo is a separate cold-loading SoulsBrand module; GraphicsUI owns
its GPU texture independently of the renderer's resizable target.
