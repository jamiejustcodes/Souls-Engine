# Contributing to Souls Engine

Keep changes focused on a single engine concern and explain the resulting
behavior in the commit message. Code comments should explain ownership, lifetime,
synchronization, or a decision that would otherwise be easy to misread.

## Architecture

- Keep platform and graphics SDK types behind the RHI and platform boundaries.
- Use C++23 and explicit result types. Engine code does not use exceptions.
- Keep frame recording free of heap allocation and runtime backend dispatch.
- Treat resource creation, destruction, uploads, and ECS structural edits as
  separate work from frame recording.
- Preserve generation checks and retirement rules when changing resource pools,
  command tokens, frame arenas, or mapped GPU constants.
- Keep UI code dependent on engine interfaces rather than native SDK objects.

## Validation

Follow the build commands in [README.md](README.md) and run the checks relevant
to the change. Core contracts and scene tests cover allocator behavior, handles,
transforms, picking, and scene extraction. The allocation harness checks that
live transform edits and extraction do not allocate.

Graphics changes also need shader compilation and a GPU smoke run. Use
`--validation --smoke 120` on a supported device with the appropriate validation
layers installed. Smoke tests exercise resizing and resource retirement; manual
input and docking checks are listed in [VERIFICATION.md](VERIFICATION.md).

Use the sanitizer presets for changes to storage, ownership, or lifetime. Record
the platform, commands, results, and any checks that could not run. A successful
Windows Vulkan check does not replace validation on Linux.

## Pull requests

Describe the concrete problem, the resulting behavior, and the verification
performed. Include an editor capture for visible UI changes. Keep generated build
files and downloaded dependencies out of the repository. Format C++ changes with
the repository's `.clang-format` configuration.
