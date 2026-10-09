#include <cstdio>
#include <cstdlib>
#include <souls/editor/TransformTools.hpp>
namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
bool close(float a, float b) {
    return std::abs(a - b) < 0.002F;
}
} // namespace
int main() {
    using namespace souls;
    using namespace souls::editor;
    for (auto angles : {Vec3{15, 25, 35}, Vec3{0, 90, 35}, Vec3{20, -90, 0}, Vec3{170, -40, -150}}) {
        const Vec3 scale{-2, 3, 0.5F};
        const auto matrix = model_matrix({4, -7, 2}, angles, scale);
        auto transform = decompose_transform(matrix, scale);
        check(transform.has_value(), "decompose mirrored, nonuniform transform");
        const auto roundtrip =
            model_matrix({transform->x, transform->y, transform->z}, transform->rotation, transform->scale);
        for (std::size_t i = 0; i < 16; ++i)
            check(close(matrix.m[i], roundtrip.m[i]), "TRS roundtrip including gimbal poles");
    }
    Transform actor{3, 0, 0};
    auto rotated = apply_gizmo_delta(actor, Mat4::identity(), model_matrix({}, {0, 0, 90}, {1, 1, 1}),
                                     TransformTool::rotate);
    check(rotated && close(rotated->x, 0) && close(rotated->y, 3) && close(rotated->rotation.z, 90),
          "group rotation uses a shared pivot");
    auto moved = apply_gizmo_delta(actor, Mat4::identity(), model_matrix({2, 4, 6}, {}, {1, 1, 1}),
                                   TransformTool::move);
    check(moved && close(moved->x, 5) && close(moved->y, 4), "move from drag-start state");
    auto scaled =
        apply_gizmo_delta(actor, Mat4::identity(), model_matrix({}, {}, {2, 1, 1}), TransformTool::scale);
    check(scaled && close(scaled->x, 6) && close(scaled->scale.x, 2),
          "group scaling changes position and size");
    actor.scale = {-2, 3, 1};
    auto mirrored_group = apply_gizmo_delta(actor, Mat4::identity(), model_matrix({1, 0, 0}, {}, {1, 1, 1}),
                                            TransformTool::move);
    check(mirrored_group && close(mirrored_group->x, 4) && close(mirrored_group->scale.x, -2),
          "mirrored actor moves in a positive-scale group pivot");
    auto mirrored_start = model_matrix({actor.x, actor.y, actor.z}, actor.rotation, actor.scale);
    auto mirrored_end = model_matrix({actor.x + 2, actor.y, actor.z}, actor.rotation, actor.scale);
    auto mirrored_single =
        apply_gizmo_delta(actor, mirrored_start, mirrored_end, TransformTool::move, actor.scale);
    check(mirrored_single && close(mirrored_single->x, 5), "single mirrored pivot preserves scale signs");
    Transform floor{0, 0, 4};
    auto placement = place_on_plane({2.2F, 3.3F, 10}, {0, 0, -1}, floor, ActorKind::cube, 1);
    check(placement && close(placement->x, 2) && close(placement->y, 3) && close(placement->z, 5),
          "placement snaps to an elevated floor and rests on it");
    floor.rotation = {0, 90, 0};
    placement = place_on_plane({5, 0, 4}, {-1, 0, 0}, floor, ActorKind::capsule);
    check(placement && close(placement->x, 1) && close(placement->z, 4),
          "placement aligns to a rotated floor");
    check(!place_on_plane({0, 0, 5}, {1, 0, 0}, Transform{}, ActorKind::cube),
          "parallel placement ray is rejected");
    check(!place_on_plane({0, 0, 5}, {0, 0, 1}, Transform{}, ActorKind::cube),
          "floor behind the camera is rejected");
    auto shear = Mat4::identity();
    shear.m[4] = 0.5F;
    check(!decompose_transform(shear), "reject shear instead of corrupting TRS");
    check(!decompose_transform(model_matrix({}, {}, {0, 1, 1})), "reject singular scale");
    std::puts("Gizmo TRS decomposition and group transform contracts passed");
}
