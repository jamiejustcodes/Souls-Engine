#pragma once
#include <souls/scene/Scene.hpp>
namespace souls::editor {
enum class TransformTool { select, move, rotate, scale };
[[nodiscard]] Result<Transform> decompose_transform(const Mat4 &matrix,
                                                    Vec3 scale_signs = {1, 1, 1}) noexcept;
// Apply the full drag-start delta, never the previous frame's rounded preview.
[[nodiscard]] Result<Transform> apply_gizmo_delta(const Transform &actor, const Mat4 &start,
                                                  const Mat4 &current, TransformTool tool, Vec3 pivot_signs = {1, 1, 1}) noexcept;
} // namespace souls::editor
