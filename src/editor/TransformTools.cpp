#include <souls/editor/TransformTools.hpp>
namespace souls::editor {
namespace {
Vec3 position(const Transform &t) noexcept {
    return {t.x, t.y, t.z};
}
Vec3 direction(const Mat4 &matrix, Vec3 p) noexcept {
    const auto &m = matrix.m;
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z, m[1] * p.x + m[5] * p.y + m[9] * p.z,
            m[2] * p.x + m[6] * p.y + m[10] * p.z};
}
Mat4 transpose_rotation(const Mat4 &r) noexcept {
    auto result = Mat4::identity();
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            result.m[column * 4 + row] = r.m[row * 4 + column];
    return result;
}
} // namespace
Result<Transform> decompose_transform(const Mat4 &matrix, Vec3 signs) noexcept {
    const auto &m = matrix.m;
    for (float value : m)
        if (!std::isfinite(value))
            return std::unexpected(
                Error{ErrorCode::invalid_argument, "The transform contains a non-finite value."});
    Vec3 axes[]{{m[0], m[1], m[2]}, {m[4], m[5], m[6]}, {m[8], m[9], m[10]}};
    Vec3 scale{};
    float *values[]{&scale.x, &scale.y, &scale.z};
    const float references[]{signs.x, signs.y, signs.z};
    for (std::size_t i = 0; i < 3; ++i) {
        *values[i] = std::sqrt(dot(axes[i], axes[i])) * (references[i] < 0 ? -1.0F : 1.0F);
        if (std::abs(*values[i]) < 0.001F)
            return std::unexpected(
                Error{ErrorCode::invalid_argument, "Scale must stay above 0.001 on every axis."});
        axes[i] = axes[i] * (1 / *values[i]);
    }
    // Actor transforms are TRS. Silently decomposing shear would visibly corrupt a part.
    if (std::abs(dot(axes[0], axes[1])) > 0.002F || std::abs(dot(axes[0], axes[2])) > 0.002F ||
        std::abs(dot(axes[1], axes[2])) > 0.002F || dot(cross(axes[0], axes[1]), axes[2]) < 0.99F)
        return std::unexpected(
            Error{ErrorCode::invalid_argument, "The transform cannot be represented without shear."});
    Transform result{m[12], m[13], m[14]};
    result.scale = scale;
    const float y = std::asin(std::clamp(-axes[0].z, -1.0F, 1.0F));
    const float x = std::abs(std::cos(y)) > 0.0001F ? std::atan2(axes[1].z, axes[2].z)
                                                    : std::atan2(-axes[2].y, axes[1].y);
    const float z = std::abs(std::cos(y)) > 0.0001F ? std::atan2(axes[0].y, axes[0].x) : 0;
    result.rotation = Vec3{x, y, z} * (180 / pi);
    return result;
}
Result<Transform> apply_gizmo_delta(const Transform &actor, const Mat4 &start, const Mat4 &current,
                                    TransformTool tool, Vec3 pivot_signs) noexcept {
    auto before = decompose_transform(start, pivot_signs);
    auto after = decompose_transform(current, pivot_signs);
    if (!before || !after)
        return std::unexpected(!before ? before.error() : after.error());
    auto result = actor;
    Vec3 p = position(actor);
    if (tool == TransformTool::move)
        p = p + position(*after) - position(*before);
    if (tool == TransformTool::rotate) {
        const auto old_rotation = model_matrix({}, before->rotation, {1, 1, 1});
        const auto new_rotation = model_matrix({}, after->rotation, {1, 1, 1});
        const auto delta = new_rotation * transpose_rotation(old_rotation);
        p = position(*after) + direction(delta, p - position(*before));
        auto rotation = decompose_transform(delta * model_matrix({}, actor.rotation, {1, 1, 1}));
        if (!rotation)
            return std::unexpected(rotation.error());
        result.rotation = rotation->rotation;
    }
    if (tool == TransformTool::scale) {
        const Vec3 factor{after->scale.x / before->scale.x, after->scale.y / before->scale.y,
                          after->scale.z / before->scale.z};
        const auto basis = model_matrix({}, before->rotation, {1, 1, 1});
        auto offset = direction(transpose_rotation(basis), p - position(*before));
        offset = {offset.x * factor.x, offset.y * factor.y, offset.z * factor.z};
        p = position(*after) + direction(basis, offset);
        // Group scaling preserves each actor's local axes rather than introducing affine shear.
        result.scale = {actor.scale.x * factor.x, actor.scale.y * factor.y, actor.scale.z * factor.z};
    }
    result.x = p.x;
    result.y = p.y;
    result.z = p.z;
    if (std::abs(result.scale.x) < 0.001F || std::abs(result.scale.y) < 0.001F ||
        std::abs(result.scale.z) < 0.001F)
        return std::unexpected(
            Error{ErrorCode::invalid_argument, "Scale must stay above 0.001 on every axis."});
    return result;
}
Result<Transform> place_on_plane(Vec3 origin, Vec3 ray, const Transform &plane, ActorKind part,
                                 float snap) noexcept {
    if (!is_primitive(part) || !std::isfinite(snap) || snap < 0)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid placement part or snap step."});
    const auto basis = model_matrix({}, plane.rotation, {1, 1, 1});
    const auto normal = direction(basis, {0, 0, 1});
    const float denominator = dot(ray, normal);
    if (std::abs(denominator) < 0.0001F)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Aim at the floor to place a part."});
    const float distance = dot(position(plane) - origin, normal) / denominator;
    if (!std::isfinite(distance) || distance <= 0)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Aim at the floor to place a part."});
    auto tangent = direction(transpose_rotation(basis), origin + ray * distance - position(plane));
    if (snap > 0) {
        tangent.x = std::round(tangent.x / snap) * snap;
        tangent.y = std::round(tangent.y / snap) * snap;
    }
    tangent.z = primitive_bounds(part).z;
    auto point = position(plane) + direction(basis, tangent);
    Transform result{point.x, point.y, point.z};
    result.rotation = plane.rotation;
    return result;
}
} // namespace souls::editor
