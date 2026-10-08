#include <souls/platform/Window.hpp>
#include <souls/renderer/Renderer.hpp>
#include <souls/scene/Scene.hpp>
int main(int argc, char **argv) {
    using namespace souls;
    auto args = platform::options(argc, argv);
    if (!args)
        return platform::report(args.error());
    platform::Window window;
    if (auto created = window.create("Souls Runtime | 3D Playground"); !created)
        return platform::report(created.error());
    auto device = rhi::Device::create({window.get(), args->validation, args->vsync});
    if (!device)
        return platform::report(device.error());
    auto scene = Scene::create();
    if (!scene)
        return platform::report(scene.error());
    if (auto ready = scene->playground(); !ready)
        return platform::report(ready.error());
    Renderer renderer{*device};
    Camera camera{};
    if (auto ready = renderer.initialize(); !ready)
        return platform::report(ready.error());
    std::uint32_t frames = 0;
    bool running = true;
    while (running && (!args->smoke_frames || frames < args->smoke_frames)) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                                                 event.window.windowID == SDL_GetWindowID(window.get())))
                running = false;
        if (!running)
            break;
        const auto pixels = window.pixels();
        if (pixels != device->swapchain().extent) {
            if (auto resized = device->resize(pixels); !resized)
                return platform::report(resized.error());
        }
        if (!pixels.width || !pixels.height) {
            SDL_Delay(16);
            continue;
        }
        if (auto ready = renderer.resize_viewport(pixels); !ready)
            return platform::report(ready.error());
        auto command = device->begin_frame({0.075F, 0.085F, 0.16F, 1});
        if (!command) {
            if (command.error().code == ErrorCode::suspended) {
                SDL_Delay(16);
                continue;
            }
            return platform::report(command.error());
        }
        auto batch = scene->extract(device->frame_arena());
        if (!batch) {
            const auto ignored = device->end_frame(*command);
            (void)ignored;
            return platform::report(batch.error());
        }
        if (auto ready = renderer.record(*command, *scene, *batch, camera); !ready)
            return platform::report(ready.error());
        if (auto ready = device->composite_texture(*command, renderer.viewport()); !ready)
            return platform::report(ready.error());
        auto completed = device->end_frame(*command);
        if (!completed)
            return platform::report(completed.error());
        ++frames;
        if (args->capture && frames == 90) {
            auto captured = device->capture_frame(args->capture);
            if (!captured)
                return platform::report(captured.error());
        }
        if (args->smoke_frames && !platform::smoke_resize(window.get(), frames))
            return platform::report({ErrorCode::platform, SDL_GetError()});
    }
    if (args->smoke_frames && frames != args->smoke_frames)
        return platform::report({ErrorCode::platform, "Smoke test closed before completion"});
    if (auto ready = renderer.shutdown(); !ready)
        return platform::report(ready.error());
    if (auto idle = device->wait_idle(); !idle)
        return platform::report(idle.error());
    std::printf("Runtime passed: %u frames on %s\n", frames, device->adapter_name());
    return 0;
}
