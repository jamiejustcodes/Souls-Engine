# Engine terminology

| Term | Meaning |
|---|---|
| Actor | An editor-visible Flecs entity with transform and render/light metadata. |
| EntityHandle | Generation-checked scene-pool reference, used by rendering and picking. |
| ActorId | Persistent document identity, retained across undo and file round trips. |
| Editor document | The borrowed scene plus selection, bounded history, file state and play snapshot. |
| Edit transaction | Gesture-start snapshot, live previews, and one commit or cancellation. |
| Group | Logical Flecs folder; members retain world-space TRS transforms. |
| Recovery level | Periodic per-user snapshot, restored as an unsaved level. |
| Simulation world | Temporary state that Stop replaces with the edit-world snapshot. |
| Part | One of six built-in static primitive meshes. |
