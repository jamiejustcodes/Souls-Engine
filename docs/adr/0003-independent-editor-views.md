# Editor and demo views keep independent camera data

Status: accepted, 9 October 2026.

The editor opens into the normal workspace. Souls Courtyard is an optional
ImGui panel that can be moved, resized, docked, tabbed or detached. Replacing the
editor workspace with the demo did not fit the requested workflow.

Each view uses its own Renderer instance and offscreen color/depth target.
GraphicsUI retains two independent scene texture registrations; resizing one
retires and reattaches only that registration. Hidden/tab-inactive demo panels
pause and skip their scene pass. Both scenes otherwise record sequentially into
the same frame command list. Draw telemetry sums the visible scene passes.

A single mapped FrameConstants block would let the second camera overwrite data
used by earlier GPU draws. Each retired CPU frame now owns eight persistent,
aligned constant slices. begin_geometry consumes a slice; exhaustion returns
an explicit error before changing target state. D3D12 binds a 256-byte-aligned
root CBV offset. Vulkan binds an aligned dynamic uniform offset from persistent
descriptors, respecting minUniformBufferOffsetAlignment. Fences protect slice
reuse. This adds no allocation during command recording and leaves whole-frame
GPU timing intact.

Demo capture tracks the owning SDL window ID. Focus loss, close, collapse,
inactive tabs and Escape release that window's relative mouse mode. Editor
shortcuts, flying and gizmos cannot consume captured demo input. The panel's
Scene/Session remain isolated from EditorDocument, as established by ADR-0002.

GPU smoke opens/closes the panel, records both cameras and resizes the primary
window. It also fills eight geometry scopes, rejects the ninth, submits, and
exercises later frame-slot reuse. Headless gameplay/history tests remain the
same because this change stays in rendering and presentation adapters.
