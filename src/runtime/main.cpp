#include <chrono>
#include <optional>
#include <souls/demo/Demo.hpp>
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
    std::optional<demo::Session> game;
    if (args->playground) {
        if (auto ready = scene->playground(); !ready)
            return platform::report(ready.error());
    } else {
        auto session = demo::Session::create(*scene);
        if (!session)
            return platform::report(session.error());
        game.emplace(std::move(*session));
    }
    Renderer renderer{*device};
    Camera camera{};
    if (auto ready = renderer.initialize(); !ready)
        return platform::report(ready.error());
    std::uint32_t frames = 0;
    bool running = true;
    bool captured = false;
    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();
    std::puts("Souls Courtyard: click to play; WASD move, Space jump, Shift sprint, mouse look, Esc pause, R "
              "restart.");
    while (running && (!args->smoke_frames || frames < args->smoke_frames)) {
        const auto now = Clock::now();
        const auto seconds = std::chrono::duration<float>(now - previous).count();
        previous = now;
        demo::Input input{};
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                                                 event.window.windowID == SDL_GetWindowID(window.get())))
                running = false;
            if (!game)
                continue;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)) {
                captured = false;
                SDL_SetWindowRelativeMouseMode(window.get(), false);
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
                !game->state().won) {
                captured = SDL_SetWindowRelativeMouseMode(window.get(), true);
                if (!captured)
                    return platform::report({ErrorCode::platform, SDL_GetError()});
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_R &&
                !event.key.repeat) {
                if (auto reset = game->reset(); !reset)
                    return platform::report(reset.error());
            }
            if (captured && event.type == SDL_EVENT_MOUSE_MOTION) {
                input.look_yaw -= event.motion.xrel * 0.003F;
                input.look_pitch -= event.motion.yrel * 0.003F;
            }
        }
        if (!running)
            break;
        if (game) {
            if (captured) {
                const auto *keys = SDL_GetKeyboardState(nullptr);
                input.forward =
                    static_cast<float>(keys[SDL_SCANCODE_W]) - static_cast<float>(keys[SDL_SCANCODE_S]);
                input.right =
                    static_cast<float>(keys[SDL_SCANCODE_D]) - static_cast<float>(keys[SDL_SCANCODE_A]);
                input.jump = keys[SDL_SCANCODE_SPACE];
                input.sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
            }
            // Smoke runs advance deterministically even without a focused native window.
            if (args->smoke_frames)
                input.forward = frames < 110 ? 1.0F : 0.0F;
            if (auto tick = game->tick(input, args->smoke_frames ? 1.0F / 60 : captured ? seconds : 0); !tick)
                return platform::report(tick.error());
            camera = game->camera();
            const auto &state = game->state();
            if (state.won && captured) {
                captured = false;
                SDL_SetWindowRelativeMouseMode(window.get(), false);
            }
            char title[256]{};
            std::snprintf(title, sizeof(title), "Souls Courtyard | %u / %u orbs | %s | R restart",
                          state.collected, state.total,
                          state.won  ? "Complete"
                          : captured ? "Esc pause"
                                     : "Click to play: WASD, Space, Shift");
            SDL_SetWindowTitle(window.get(), title);
        }
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
        if (auto ready = renderer.record(*command, *scene, *batch, camera, {}, !game); !ready)
            return platform::report(ready.error());
        if (auto ready = device->composite_texture(*command, renderer.viewport()); !ready)
            return platform::report(ready.error());
        auto completed = device->end_frame(*command);
        if (!completed)
            return platform::report(completed.error());
        ++frames;
        if (args->capture && frames == 90) {
            auto image = device->capture_frame(args->capture);
            if (!image)
                return platform::report(image.error());
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
