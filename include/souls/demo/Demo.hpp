#pragma once
#include <souls/scene/Scene.hpp>

namespace souls::demo {
struct Input {
    float forward = 0, right = 0;
    bool jump = false, sprint = false;
    // Radians since the last presentation frame, rather than radians per tick.
    float look_yaw = 0, look_pitch = 0;
};
struct State {
    Vec3 position{}, velocity{};
    bool grounded = false, won = false;
    std::uint32_t collected = 0, total = 4, checkpoint = 0, falls = 0;
    double elapsed = 0;
};

// A small kinematic platformer, independent of the editor and window system.
// Scene must outlive Session and its contents must remain intact while playing.
class Session final {
  public:
    static constexpr float fixed_step = 1.0F / 120.0F;
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    Session(Session &&) noexcept = default;
    Session &operator=(Session &&) noexcept = default;
    [[nodiscard]] static Result<Session> create(Scene &empty_scene) noexcept;
    [[nodiscard]] Result<void> tick(const Input &input, float seconds) noexcept;
    [[nodiscard]] Result<void> reset() noexcept;
    [[nodiscard]] const State &state() const noexcept {
        return state_;
    }
    [[nodiscard]] const Camera &camera() const noexcept {
        return camera_;
    }
    [[nodiscard]] EntityHandle player_handle() const noexcept {
        return player_;
    }

  private:
    explicit Session(Scene &scene) noexcept : scene_(&scene) {}
    struct Solid {
        Vec3 low{}, high{};
    };
    struct Orb {
        EntityHandle handle{};
        Vec3 position{};
        bool collected = false;
    };
    [[nodiscard]] Result<EntityHandle> part(ActorKind kind, const char *name, Vec3 position, Vec3 scale,
                                            Vec3 color, const char *group) noexcept;
    [[nodiscard]] Result<void> block(const char *name, Vec3 position, Vec3 half_size, Vec3 color) noexcept;
    [[nodiscard]] Result<void> build() noexcept;
    [[nodiscard]] Result<void> step(const Input &input) noexcept;
    [[nodiscard]] Result<void> present() noexcept;
    void move_axis(std::size_t axis, float displacement) noexcept;
    void respawn() noexcept;
    void update_camera() noexcept;
    Scene *scene_ = nullptr;
    EntityHandle player_{};
    std::array<EntityHandle, 100> actors_{};
    std::size_t actor_count_ = 0;
    std::array<Solid, 32> solids_{};
    std::size_t solid_count_ = 0;
    std::array<Orb, 4> orbs_{};
    State state_{};
    Camera camera_{};
    Vec3 respawn_position_{0, -1, 0.851F};
    double accumulator_ = 0;
    float jump_buffer_ = 0, coyote_ = 0;
    bool jump_held_ = false;
};
} // namespace souls::demo
