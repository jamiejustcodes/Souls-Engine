#pragma once
#include <souls/scene/Scene.hpp>
namespace souls::editor {
enum class TransformTool { select, move, rotate, scale };
[[nodiscard]] Result<Transform> decompose_transform(const Mat4 &matrix,
                                                    Vec3 scale_signs = {1, 1, 1}) noexcept;
// Apply the full drag-start delta, never the previous frame's rounded preview.
[[nodiscard]] Result<Transform> apply_gizmo_delta(const Transform &actor, const Mat4 &start,
                                                  const Mat4 &current, TransformTool tool,
                                                  Vec3 pivot_signs = {1, 1, 1}) noexcept;
// Snap in the floor's tangent plane and rest the part on its surface.
[[nodiscard]] Result<Transform> place_on_plane(Vec3 ray_origin, Vec3 ray_direction, const Transform &plane,
                                               ActorKind part, float snap_step = 0) noexcept;
} // namespace souls::editor
