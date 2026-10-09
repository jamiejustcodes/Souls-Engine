#include <cstdio>
#include <cstdlib>
#include <imgui.h>

#include <ImGuizmo.h>
#include <souls/core/Math.hpp>
#include <utility>
namespace {
void check(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
} // namespace
int main() {
    using namespace souls;
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.DisplaySize = {800, 600};
    io.DeltaTime = 1.0F / 60;
    io.IniFilename = nullptr;
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Camera camera;
    camera.position = {6, -9, 5};
    camera.yaw = std::atan2(9.0F, -6.0F);
    camera.pitch = -std::atan(5.0F / std::sqrt(117.0F));
    const auto view = camera.view(), projection = camera.projection(800.0F / 600);
    auto matrix = Mat4::identity();
    auto frame = [&](ImGuizmo::OPERATION operation, ImVec2 mouse, bool down) {
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(0, down);
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({800, 600});
        ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(0, 0, 800, 600);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::Enable(true);
        const bool changed = ImGuizmo::Manipulate(view.m.data(), projection.m.data(), operation,
                                                  ImGuizmo::WORLD, matrix.m.data());
        const bool over = ImGuizmo::IsOver(operation);
        ImGui::End();
        ImGui::Render();
        return std::pair{changed, over};
    };
    for (auto operation : {ImGuizmo::TRANSLATE, ImGuizmo::SCALE, ImGuizmo::ROTATE}) {
        matrix = Mat4::identity();
        frame(operation, {400, 300}, false);
        frame(operation, {400, 300}, false);
        ImVec2 hit{400, 300};
        bool found = frame(operation, hit, false).second;
        if (!found) {
            // Probe the actual widget hit regions, independent of its screen-size tuning.
            for (int y = 230; y <= 370 && !found; y += 2) {
                for (int x = 330; x <= 470 && !found; x += 2) {
                    hit = {static_cast<float>(x), static_cast<float>(y)};
                    found = frame(operation, hit, false).second;
                }
            }
        }
        check(found, "widget has a selectable handle under the engine's left-handed camera");
        frame(operation, hit, true);
        check(ImGuizmo::IsUsing(), "press begins a gizmo gesture");
        bool changed = false;
        for (int i = 1; i <= 4; ++i)
            changed |=
                frame(operation, {hit.x + static_cast<float>(i * 8), hit.y + static_cast<float>(i * 5)}, true)
                    .first;
        frame(operation, {hit.x + 32, hit.y + 20}, false);
        check(changed, "drag modifies the transform");
        check(!ImGuizmo::IsUsing(), "release finishes the gesture");
        matrix = Mat4::identity();
        frame(operation, hit, false);
        frame(operation, hit, false);
        frame(operation, hit, true);
        check(ImGuizmo::IsUsing(), "press starts the gesture that will be cancelled");
        // A cancelled document gesture must also release the widget's internal drag.
        ImGuizmo::Enable(false);
        check(!ImGuizmo::IsUsing(), "disabling the widget cancels its active gesture");
        frame(operation, hit, false);
        for (float value : matrix.m)
            check(std::isfinite(value), "gesture produces a finite matrix");
    }
    ImGui::DestroyContext();
    std::puts("ImGuizmo move, rotate and scale gestures passed with the engine camera");
}
