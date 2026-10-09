#include "editor/GraphicsUI.hpp"
#include "editor/Workspace.hpp"
#include <chrono>
#include <imgui_impl_sdl3.h>
#include <souls/platform/Window.hpp>
#include <souls/renderer/Renderer.hpp>
#include <souls/scene/Scene.hpp>
int main(int argc, char **argv) {
    using namespace souls;
    using Clock = std::chrono::steady_clock;
    const auto us = [](auto from, auto to) {
        return std::chrono::duration<double, std::micro>(to - from).count();
    };
    auto args = platform::options(argc, argv);
    if (!args)
        return platform::report(args.error());
    platform::Window window;
    if (auto created = window.create("Souls Editor | Rendering workspace"); !created)
        return platform::report(created.error());
    auto logo = brand::Logo::load();
    if (!logo)
        return platform::report(logo.error());
    if (auto icon = window.set_icon(*logo); !icon)
        return platform::report(icon.error());
    auto device = rhi::Device::create({window.get(), args->validation, args->vsync});
    if (!device)
        return platform::report(device.error());
    auto scene = Scene::create();
    if (!scene)
        return platform::report(scene.error());
    if (auto ready = scene->playground(); !ready)
        return platform::report(ready.error());
    Renderer renderer{*device};
    editor::GraphicsUI graphics{*device};
    auto document = editor::EditorDocument::create(*scene);
    if (!document)
        return platform::report(document.error());
    auto demo_scene = Scene::create();
    if (!demo_scene)
        return platform::report(demo_scene.error());
    auto demo = demo::Session::create(*demo_scene);
    if (!demo)
        return platform::report(demo.error());
    editor::Workspace workspace{*document, *scene, *demo_scene, *demo};
    if (auto initialized = graphics.initialize(window.get(), *logo); !initialized)
        return platform::report(initialized.error());
    if (auto ready = renderer.initialize(); !ready)
        return platform::report(ready.error());
    workspace.set_logo(graphics.logo());
    workspace.initialize(window.get(), !args->smoke_frames);
    workspace.show_demo(!args->playground);
    float scale = SDL_GetWindowDisplayScale(window.get());
    editor::Workspace::theme(scale);
    if (auto resized = renderer.resize_viewport(workspace.requested_extent()); !resized)
        return platform::report(resized.error());
    auto texture = graphics.attach(renderer.viewport());
    if (!texture)
        return platform::report(texture.error());
    if (args->smoke_frames) {
        if (auto verified = renderer.verify_resources(); !verified)
            return platform::report(verified.error());
        if (auto verified = workspace.verify_workflow(*scene); !verified)
            return platform::report(verified.error());
        auto probe = device->create_render_target({16, 16});
        if (!probe)
            return platform::report(probe.error());
        if (auto retired = device->destroy_texture(*probe); !retired)
            return platform::report(retired.error());
        if (device->destroy_texture(*probe))
            return platform::report({ErrorCode::gpu, "Stale texture handle accepted"});
    }
    auto previous = Clock::now();
    bool running = true;
    std::uint32_t frames = 0, resizes = 0;
    while (running && (!args->smoke_frames || frames < args->smoke_frames)) {
        const auto start = Clock::now();
        const double frame_ms = us(previous, start) / 1000;
        previous = start;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            workspace.event(event);
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                                                 event.window.windowID == SDL_GetWindowID(window.get())))
                workspace.request_exit();
        }
        if (!running || workspace.quit_requested())
            break;
        const auto pixels = window.pixels();
        if (pixels != device->swapchain().extent) {
            if (auto resized = device->resize(pixels); !resized)
                return platform::report(resized.error());
            ++resizes;
        }
        if (!pixels.width || !pixels.height) {
            SDL_Delay(16);
            continue;
        }
        const auto next_scale = SDL_GetWindowDisplayScale(window.get());
        if (scale != next_scale) {
            scale = next_scale;
            editor::Workspace::theme(scale);
            if (!SDL_SetWindowMinimumSize(window.get(), static_cast<int>(1100 * scale),
                                          static_cast<int>(720 * scale)))
                return platform::report({ErrorCode::platform, SDL_GetError()});
        }
        if (renderer.extent() != workspace.requested_extent()) {
            if (auto detached = graphics.detach(); !detached)
                return platform::report(detached.error());
            if (auto resized = renderer.resize_viewport(workspace.requested_extent()); !resized)
                return platform::report(resized.error());
            texture = graphics.attach(renderer.viewport());
            if (!texture)
                return platform::report(texture.error());
        }
        const auto events_end = Clock::now();
        if (args->smoke_frames && !args->playground) {
            demo::Input input{};
            input.forward = frames < 110 ? 1.0F : 0.0F;
            if (auto tick = demo->tick(input, 1.0F / 60); !tick)
                return platform::report(tick.error());
            // Exercise both workspaces and viewport texture retirement in one run.
            if (frames == 20)
                workspace.show_demo(false);
            if (frames == 55)
                workspace.show_demo(true);
        }
        graphics.new_frame();
        workspace.draw(*texture, renderer.extent(), device->adapter_name(), *scene, window.get(),
                       SDL_GetWindowPixelDensity(window.get()));
        ImGui::Render();
        const auto ui_end = Clock::now();
        auto command = device->begin_frame({30.0F / 255, 30.0F / 255, 36.0F / 255, 1});
        if (!command) {
            if (command.error().code == ErrorCode::suspended) {
                SDL_Delay(16);
                continue;
            }
            return platform::report(command.error());
        }
        auto &render_scene = workspace.render_scene();
        auto batch = render_scene.extract(device->frame_arena());
        if (!batch) {
            const auto ignored = device->end_frame(*command);
            (void)ignored;
            return platform::report(batch.error());
        }
        if (auto drawn =
                renderer.record(*command, render_scene, *batch, workspace.camera(), workspace.selected(),
                                workspace.grid(), workspace.lit(), workspace.selection());
            !drawn) {
            const auto ignored = device->end_frame(*command);
            (void)ignored;
            return platform::report(drawn.error());
        }
        graphics.render(*command);
        editor::Sample sample{};
        sample.frame_ms = frame_ms;
        sample.scene_draws = renderer.draws();
        sample.arena_bytes = device->frame_arena().used();
        auto *data = ImGui::GetDrawData();
        for (int i = 0; i < data->CmdListsCount; ++i)
            for (const auto &draw : data->CmdLists[i]->CmdBuffer)
                if (!draw.UserCallback && draw.ElemCount)
                    ++sample.draws;
        const auto record_end = Clock::now();
        if (auto submitted = device->end_frame(*command); !submitted)
            return platform::report(submitted.error());
        if (args->smoke_frames && device->end_frame(*command))
            return platform::report({ErrorCode::gpu, "Expired command token accepted"});
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        const auto end = Clock::now();
        sample.cpu_us = {us(start, events_end), us(events_end, ui_end), us(ui_end, record_end),
                         us(record_end, end)};
        sample.gpu = device->stats();
        workspace.push(sample);
        ++frames;
        if (args->capture && frames == 90) {
            auto captured = device->capture_frame(args->capture);
            if (!captured)
                return platform::report(captured.error());
        }
        if (args->smoke_frames && !platform::smoke_resize(window.get(), frames))
            return platform::report({ErrorCode::platform, SDL_GetError()});
    }
    if (args->smoke_frames && (frames != args->smoke_frames || resizes < 2))
        return platform::report({ErrorCode::platform, "Smoke test did not complete both resize transitions"});
    if (args->smoke_frames && workspace.demo_active() &&
        (scene->size() != 9 || demo->state().checkpoint != 1 || demo->state().collected != 1))
        return platform::report(
            {ErrorCode::invalid_argument, "Demo did not advance independently of the editor world"});
    if (auto closed = graphics.shutdown(); !closed)
        return platform::report(closed.error());
    if (auto closed = renderer.shutdown(); !closed)
        return platform::report(closed.error());
    std::printf("Editor passed: %u frames, %u resizes, adapter %s\n", frames, resizes,
                device->adapter_name());
    return 0;
}
