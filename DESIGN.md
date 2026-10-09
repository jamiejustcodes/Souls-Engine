# Editor design contract

The editor follows UE5 workspace conventions. Menus and a
Home/Build/Test/Tools ribbon sit above the main dock. The live 3D viewport owns
roughly three quarters of the upper workspace. World Outliner and Details
stack on the right. The collapsible Content Browser and developer tools share
a bottom dock; the status strip exposes the drawer, log, frame time and draws.

Canvas #1E1E24; panels #2B2B36; raised controls #393945; accent #6C5CE7;
text #F0F0F5; secondary text #B9BBC8; green simulation controls. XYZ inputs and
orientation axes use red, green and blue. Component headers stay neutral; violet
marks active tools and selection. Spacing follows an 8px grid; control
corners are 4px. Segoe UI on Windows and DejaVu/Liberation Sans on Linux,
15px base with dynamic DPI scaling. Familiar dense desktop controls and
keyboard navigation take priority over ornamental chrome.

Selection is shared by viewport picking, Outliner and Details through EditorDocument.
The Build palette and asset tiles share a typed part payload; the viewport previews
placement before committing an actor. Gizmos and Details preview from the gesture
start and commit one undo operation on release. Transform,
visibility, color and light values edit the live Flecs world. Play snapshots
the edit world; Stop restores it. Eject enables camera navigation and Details
editing during simulation. The drawer presents the built-in asset catalog;
all six mesh assets can place actors. Group folders and locks remain visible in
inspection. File actions protect dirty levels with Save/Discard/Cancel; native
dialogs choose paths, and recovery is offered on the next launch. Developer tools contain measured telemetry,
a movable render graph and a working command console. Unavailable driver
budgets are labeled unavailable. The initial budget remains 60 Hz / 16.67ms.

The viewport samples a real RGBA8 offscreen target with D32 depth. Camera-ray
floor shading is infinite and pixel filtered. The pass graph describes grid,
indexed geometry, viewport sampling and presentation. This stage implements
Blinn-Phong lighting and procedural sky color, with no claims of deferred
GBuffer, shadow maps, full landscape sculpting or mesh topology authoring.

Startup uses a dedicated playable canvas. The crest and course name anchor a
60px toolbar; Open editor, Edit demo, Restart and About remain visible above the
game. An objective HUD shows real collectible/checkpoint state. A compact
start/pause card explicitly captures the mouse; Esc releases it. The control
strip explains movement. The editor retains its layout and has a persistent
green Play demo action, separate from primitive rotation Simulate.

The complete original logo is embedded and shown in About. A presentation-only
crest crop works at menu size; the native window icon uses the same region. The
brand texture has its own lifetime, independent of resizable scene targets.
Neutral hover colors reduce competing highlights; violet marks active tools and
green identifies gameplay. The bottom dock occupies 22% of the default workspace.
Outliner and Details split the right column 45/55, with scrolling inspection.
