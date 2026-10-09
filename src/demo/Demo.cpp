#include <algorithm>
#include <cmath>
#include <cstdio>
#include <souls/demo/Demo.hpp>

namespace souls::demo {
namespace {
constexpr Vec3 body_half{0.32F, 0.32F, 0.85F};
constexpr Vec3 slate{0.18F, 0.21F, 0.28F}, violet{0.42F, 0.29F, 0.88F}, gold{1, 0.69F, 0.19F};
float &component(Vec3 &value, std::size_t axis) noexcept {
    return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
}
float component(const Vec3 &value, std::size_t axis) noexcept {
    return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
}
bool near(Vec3 a, Vec3 b, float radius) noexcept {
    const Vec3 d = a - b;
    return dot(d, d) <= radius * radius;
}
} // namespace

Result<EntityHandle> Session::part(ActorKind kind, const char *name, Vec3 p, Vec3 scale, Vec3 color,
                                   const char *group) noexcept {
    if (actor_count_ == actors_.size())
        return std::unexpected(Error{ErrorCode::exhausted, "Demo actor capacity exhausted"});
    auto handle = scene_->spawn(kind, name, Transform{p.x, p.y, p.z, {}, scale});
    if (!handle)
        return std::unexpected(handle.error());
    actors_[actor_count_++] = *handle;
    auto actor = scene_->actor(*handle);
    if (!actor)
        return std::unexpected(actor.error());
    actor->color = color;
    actor->material = 0;
    actor->locked = true;
    std::snprintf(actor->group.data(), actor->group.size(), "%s", group);
    auto updated = scene_->update(*handle, *actor);
    if (!updated)
        return std::unexpected(updated.error());
    return *handle;
}
Result<void> Session::block(const char *name, Vec3 p, Vec3 half, Vec3 color) noexcept {
    if (solid_count_ == solids_.size())
        return std::unexpected(Error{ErrorCode::exhausted, "Demo collision capacity exhausted"});
    auto actor = part(ActorKind::cube, name, p, half, color, "Courtyard / Platforms");
    if (!actor)
        return std::unexpected(actor.error());
    solids_[solid_count_++] = {p - half, p + half};
    return {};
}
Result<void> Session::build() noexcept {
    struct Platform {
        const char *name;
        Vec3 p, half, color;
    };
    constexpr Platform platforms[]{{"Arrival courtyard", {0, 0, -0.3F}, {3, 3, 0.3F}, slate},
                                   {"Checkpoint approach", {0, 4.2F, -0.3F}, {1.1F, 1.2F, 0.3F}, slate},
                                   {"Checkpoint island", {0, 7, -0.3F}, {2.1F, 1.6F, 0.3F}, violet},
                                   {"First leap", {0, 11, 0.25F}, {1.3F, 1, 0.3F}, slate},
                                   {"Second leap", {0, 14.3F, 0.8F}, {1.2F, 1, 0.3F}, violet},
                                   {"Finish courtyard", {0, 18.1F, 0.8F}, {2.5F, 2, 0.3F}, slate},
                                   {"Arrival west wall", {-3.4F, 0, 0.6F}, {0.2F, 3, 0.6F}, slate},
                                   {"Arrival east wall", {3.4F, 0, 0.6F}, {0.2F, 3, 0.6F}, slate},
                                   {"Arrival rear wall", {0, -3.4F, 0.6F}, {3.6F, 0.2F, 0.6F}, slate}};
    for (const auto &p : platforms) {
        auto result = block(p.name, p.p, p.half, p.color);
        if (!result)
            return result;
    }
    for (float x : {-2.3F, 2.3F}) {
        for (float y : {-2.1F, 2.1F, 17.2F, 19.4F}) {
            auto pillar = part(ActorKind::cylinder, "Courtyard column", {x, y, 1.6F}, {0.18F, 0.18F, 1.6F},
                               slate, "Courtyard / Architecture");
            if (!pillar)
                return std::unexpected(pillar.error());
            auto cap = part(ActorKind::sphere, "Column beacon", {x, y, 3.27F}, {0.24F, 0.24F, 0.24F}, gold,
                            "Courtyard / Architecture");
            if (!cap)
                return std::unexpected(cap.error());
        }
    }
    auto gate = part(ActorKind::cube, "Finish arch", {0, 19.4F, 3.1F}, {2.5F, 0.18F, 0.2F}, violet,
                     "Courtyard / Architecture");
    if (!gate)
        return std::unexpected(gate.error());
    auto checkpoint = part(ActorKind::cylinder, "Checkpoint disc", {0, 6.8F, 0.04F}, {0.8F, 0.8F, 0.04F},
                           gold, "Courtyard / Checkpoints");
    if (!checkpoint)
        return std::unexpected(checkpoint.error());
    auto finish = part(ActorKind::cylinder, "Finish disc", {0, 18.1F, 1.14F}, {1, 1, 0.04F}, gold,
                       "Courtyard / Checkpoints");
    if (!finish)
        return std::unexpected(finish.error());
    constexpr Vec3 positions[]{{0, 6.8F, 1.1F}, {0, 11, 1.65F}, {0, 14.3F, 2.2F}, {0, 18.1F, 2.2F}};
    for (std::size_t i = 0; i < orbs_.size(); ++i) {
        char label[32]{};
        std::snprintf(label, sizeof(label), "Golden orb %u", static_cast<unsigned>(i + 1));
        auto orb = part(ActorKind::sphere, label, positions[i], {0.28F, 0.28F, 0.28F}, gold,
                        "Courtyard / Collectibles");
        if (!orb)
            return std::unexpected(orb.error());
        orbs_[i] = {*orb, positions[i], false};
    }
    auto player = part(ActorKind::capsule, "Player", respawn_position_, {0.64F, 0.64F, 0.85F},
                       {0.76F, 0.79F, 0.9F}, "Courtyard / Player");
    if (!player)
        return std::unexpected(player.error());
    player_ = *player;
    auto light = part(ActorKind::light, "Courtyard sun", {0, 0, 8}, {1, 1, 1}, {1, 0.95F, 0.84F},
                      "Courtyard / Lighting");
    if (!light)
        return std::unexpected(light.error());
    auto actor = scene_->actor(*light);
    if (!actor)
        return std::unexpected(actor.error());
    actor->transform.rotation = {-40, 25, -30};
    auto updated = scene_->update(*light, *actor);
    if (!updated)
        return updated;
    return reset();
}
Result<Session> Session::create(Scene &scene) noexcept {
    if (scene.size())
        return std::unexpected(Error{ErrorCode::invalid_argument, "Demo needs an empty scene"});
    Session session{scene};
    auto built = session.build();
    if (!built) {
        // Cold construction is transactional: a failed course leaves no half-built actors.
        for (std::size_t i = session.actor_count_; i > 0; --i)
            (void)scene.remove(session.actors_[i - 1]);
        return std::unexpected(built.error());
    }
    return session;
}

Result<void> Session::reset() noexcept {
    state_ = {};
    respawn_position_ = {0, -1, 0.851F};
    state_.position = respawn_position_;
    state_.grounded = true;
    accumulator_ = 0;
    jump_buffer_ = coyote_ = 0;
    jump_held_ = false;
    camera_.yaw = pi / 2;
    camera_.pitch = -0.28F;
    for (auto &orb : orbs_) {
        orb.collected = false;
        auto actor = scene_->actor(orb.handle);
        if (!actor)
            return std::unexpected(actor.error());
        actor->visible = true;
        auto result = scene_->update(orb.handle, *actor);
        if (!result)
            return result;
    }
    return present();
}
void Session::respawn() noexcept {
    state_.position = respawn_position_;
    state_.velocity = {};
    state_.grounded = true;
    jump_buffer_ = coyote_ = 0;
    ++state_.falls;
}
void Session::move_axis(std::size_t axis, float displacement) noexcept {
    float &position = component(state_.position, axis);
    const float start = position, half = component(body_half, axis);
    float destination = start + displacement;
    for (std::size_t i = 0; i < solid_count_; ++i) {
        const auto &solid = solids_[i];
        bool overlaps = true;
        for (std::size_t other = 0; other < 3; ++other) {
            if (other == axis)
                continue;
            const float p = component(state_.position, other), h = component(body_half, other);
            overlaps &= p + h > component(solid.low, other) + 0.0001F &&
                        p - h < component(solid.high, other) - 0.0001F;
        }
        if (!overlaps)
            continue;
        const float low = component(solid.low, axis), high = component(solid.high, axis);
        // Sweep the moving face, rather than just checking the final overlap. This
        // keeps thin walls solid even at sprint speed; only axis-aligned solids collide.
        if (displacement > 0 && start + half <= low + 0.0011F && destination + half >= low) {
            destination = std::min(destination, low - half - 0.001F);
            component(state_.velocity, axis) = 0;
        } else if (displacement < 0 && start - half >= high - 0.0011F && destination - half <= high) {
            destination = std::max(destination, high + half + 0.001F);
            component(state_.velocity, axis) = 0;
            if (axis == 2)
                state_.grounded = true;
        }
    }
    position = destination;
}
Result<void> Session::step(const Input &input) noexcept {
    if (state_.won)
        return {};
    state_.elapsed += fixed_step;
    coyote_ = state_.grounded ? 0.09F : std::max(0.0F, coyote_ - fixed_step);
    jump_buffer_ = std::max(0.0F, jump_buffer_ - fixed_step);
    if (jump_buffer_ > 0 && coyote_ > 0) {
        state_.velocity.z = 7.8F;
        jump_buffer_ = coyote_ = 0;
        state_.grounded = false;
    }
    const Vec3 forward{std::cos(camera_.yaw), std::sin(camera_.yaw), 0};
    const Vec3 right{forward.y, -forward.x, 0};
    Vec3 movement = forward * input.forward + right * input.right;
    const float length = std::sqrt(dot(movement, movement));
    if (length > 1)
        movement = movement * (1 / length);
    const float speed = input.sprint ? 7.2F : 4.8F;
    state_.velocity.x = movement.x * speed;
    state_.velocity.y = movement.y * speed;
    state_.velocity.z -= 20 * fixed_step;
    state_.grounded = false;
    move_axis(0, state_.velocity.x * fixed_step);
    move_axis(1, state_.velocity.y * fixed_step);
    move_axis(2, state_.velocity.z * fixed_step);
    if (state_.position.z < -8)
        respawn();
    for (auto &orb : orbs_) {
        if (!orb.collected && near(state_.position, orb.position, 0.9F)) {
            orb.collected = true;
            ++state_.collected;
            auto actor = scene_->actor(orb.handle);
            if (!actor)
                return std::unexpected(actor.error());
            actor->visible = false;
            auto updated = scene_->update(orb.handle, *actor);
            if (!updated)
                return updated;
        }
    }
    if (state_.checkpoint == 0 && state_.grounded && near(state_.position, {0, 6.8F, 0.851F}, 1.25F)) {
        state_.checkpoint = 1;
        respawn_position_ = {0, 6.8F, 0.851F};
    }
    state_.won = state_.collected == state_.total && state_.grounded &&
                 near(state_.position, {0, 18.1F, 1.951F}, 1.3F);
    return {};
}
void Session::update_camera() noexcept {
    const Vec3 target = state_.position + Vec3{0, 0, 0.4F};
    const Vec3 segment = camera_.forward() * -6.5F;
    float fraction = 1;
    for (std::size_t i = 0; i < solid_count_; ++i) {
        float enter = 0, exit = 1;
        const auto &solid = solids_[i];
        bool hit = true;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const float origin = component(target, axis), d = component(segment, axis);
            const float low = component(solid.low, axis) - 0.15F, high = component(solid.high, axis) + 0.15F;
            if (std::abs(d) < 0.0001F) {
                hit &= origin >= low && origin <= high;
                continue;
            }
            float a = (low - origin) / d, b = (high - origin) / d;
            if (a > b)
                std::swap(a, b);
            enter = std::max(enter, a);
            exit = std::min(exit, b);
            hit &= enter <= exit;
        }
        if (hit && enter > 0)
            fraction = std::min(fraction, std::max(0.1F, enter - 0.03F));
    }
    camera_.position = target + segment * fraction;
}
Result<void> Session::present() noexcept {
    auto actor = scene_->actor(player_);
    if (!actor)
        return std::unexpected(actor.error());
    actor->transform.x = state_.position.x;
    actor->transform.y = state_.position.y;
    actor->transform.z = state_.position.z;
    actor->color = state_.won ? gold : Vec3{0.76F, 0.79F, 0.9F};
    auto updated = scene_->update(player_, *actor);
    if (!updated)
        return updated;
    for (const auto &orb : orbs_) {
        if (orb.collected)
            continue;
        auto value = scene_->actor(orb.handle);
        if (!value)
            return std::unexpected(value.error());
        value->transform.z =
            orb.position.z + 0.12F * std::sin(static_cast<float>(state_.elapsed) * 2.5F + orb.position.y);
        auto result = scene_->update(orb.handle, *value);
        if (!result)
            return result;
    }
    update_camera();
    return {};
}
Result<void> Session::tick(const Input &input, float seconds) noexcept {
    const float values[]{input.forward, input.right, input.look_yaw, input.look_pitch, seconds};
    for (float value : values)
        if (!std::isfinite(value))
            return std::unexpected(Error{ErrorCode::invalid_argument, "Non-finite demo input"});
    if (seconds < 0 || std::abs(input.forward) > 1 || std::abs(input.right) > 1 ||
        std::abs(input.look_yaw) > 100 || std::abs(input.look_pitch) > 100)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid demo input"});
    camera_.yaw = std::remainder(camera_.yaw + input.look_yaw, 2 * pi);
    camera_.pitch = std::clamp(camera_.pitch + input.look_pitch, -0.95F, 0.35F);
    if (input.jump && !jump_held_)
        jump_buffer_ = 0.12F;
    jump_held_ = input.jump;
    // One quarter-second catch-up bounds work after a debugger stop. The fixed
    // quantum keeps collision and jump trajectories independent of render cadence.
    accumulator_ += std::min(seconds, 0.25F);
    const Input bounded{input.forward, input.right, input.jump, input.sprint, 0, 0};
    while (accumulator_ + 1e-9 >= fixed_step) {
        auto result = step(bounded);
        if (!result)
            return result;
        accumulator_ -= fixed_step;
    }
    return present();
}
} // namespace souls::demo
