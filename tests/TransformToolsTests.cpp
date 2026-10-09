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
    auto shear = Mat4::identity();
    shear.m[4] = 0.5F;
    check(!decompose_transform(shear), "reject shear instead of corrupting TRS");
    check(!decompose_transform(model_matrix({}, {}, {0, 1, 1})), "reject singular scale");
    std::puts("Gizmo TRS decomposition and group transform contracts passed");
}
