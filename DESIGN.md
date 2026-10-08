# Editor design contract

The editor follows UE5 workspace conventions. Menus and a
compact mode/play ribbon sit above the main dock. The live 3D viewport owns
roughly three quarters of the upper workspace. World Outliner and Details
stack on the right. The collapsible Content Browser and developer tools share
a bottom dock; the status strip exposes the drawer, log, frame time and draws.

Canvas #1E1E24; panels #2B2B36; raised controls #393945; accent #6C5CE7;
text #F0F0F5; secondary text #B9BBC8; green simulation controls. XYZ inputs and
orientation axes use red, green and blue. Spacing follows an 8px grid; control
corners are 4px. Segoe UI on Windows and DejaVu/Liberation Sans on Linux,
15px base with dynamic DPI scaling. Familiar dense desktop controls and
keyboard navigation take priority over ornamental chrome.

Selection is shared by viewport picking, Outliner and Details. Transform,
visibility, color and light values edit the live Flecs world. Play snapshots
the edit world; Stop restores it. Eject enables camera navigation and Details
editing during simulation. The drawer presents the built-in asset catalog;
mesh assets can place actors. Developer tools contain measured telemetry,
a movable render graph and a working command console. Unavailable driver
budgets are labeled unavailable. The initial budget remains 60 Hz / 16.67ms.

The viewport samples a real RGBA8 offscreen target with D32 depth. Camera-ray
floor shading is infinite and pixel filtered. The pass graph describes grid,
indexed geometry, viewport sampling and presentation. This stage implements
Blinn-Phong lighting and procedural sky color, with no claims of deferred
GBuffer, shadow maps, full landscape sculpting or mesh topology authoring.
