#pragma once
#include <memory>
#include <souls/core/Core.hpp>
#include <souls/core/Math.hpp>
namespace souls {
struct EntityTag;
using EntityHandle = Handle<EntityTag>;
struct Transform {
    float x = 0, y = 0, z = 0;
    Vec3 rotation{}, scale{1, 1, 1};
};
enum class ActorKind : std::uint32_t { cube, sphere, light, sky, floor, cylinder, wedge, capsule, plane };
inline constexpr std::uint32_t actor_kind_count = 9;
inline constexpr bool is_primitive(ActorKind kind) noexcept {
    return kind == ActorKind::cube || kind == ActorKind::sphere ||
           (kind >= ActorKind::cylinder && kind <= ActorKind::plane);
}
inline constexpr std::size_t primitive_mesh_index(ActorKind kind) noexcept {
    return kind == ActorKind::cube ? 0 : (kind == ActorKind::sphere ? 1 : static_cast<std::size_t>(kind) - 3);
}
inline constexpr Vec3 primitive_bounds(ActorKind kind) noexcept {
    return kind == ActorKind::capsule ? Vec3{0.5F, 0.5F, 1}
           : kind == ActorKind::plane ? Vec3{1, 1, 0.01F}
                                      : Vec3{1, 1, 1};
}
struct Actor {
    std::array<char, 64> label{};
    Transform transform{};
    ActorKind kind = ActorKind::cube;
    bool visible = true;
    float intensity = 100000, attenuation = 0;
    Vec3 color{1, 0.95F, 0.84F};
    std::uint32_t material = 0;
    std::array<char, 64> group{};
    bool locked = false;
};
struct RenderBatch {
    std::span<float> x, y, z, radius;
    std::span<std::uint32_t> material;
    std::array<std::span<float>, 3> rotation{}, scale{}, color{};
    std::span<EntityHandle> handles{};
    std::span<std::uint32_t> visible{};
    std::span<std::uint32_t> kind{};
};
// Flecs is confined to the implementation; graphics and UI do not depend on ECS headers.
class Scene final {
  public:
    Scene() noexcept;
    ~Scene();
    Scene(Scene &&) noexcept;
    Scene &operator=(Scene &&) noexcept;
    [[nodiscard]] static Result<Scene> create() noexcept;
    [[nodiscard]] Result<EntityHandle> add(Transform transform, float radius,
                                           std::uint32_t material) noexcept;
    [[nodiscard]] Result<void> remove(EntityHandle entity) noexcept;
    [[nodiscard]] Result<EntityHandle> spawn(ActorKind kind, const char *label,
                                             Transform transform = {}) noexcept;
    [[nodiscard]] Result<Actor> actor(EntityHandle entity) const noexcept;
    [[nodiscard]] Result<void> update(EntityHandle entity, const Actor &actor) noexcept;
    [[nodiscard]] Result<EntityHandle> duplicate(EntityHandle entity) noexcept;
    [[nodiscard]] Result<void> playground() noexcept;
    [[nodiscard]] EntityHandle pick(const Camera &camera, Vec3 ray) const noexcept;
    [[nodiscard]] std::span<const EntityHandle> actors() const noexcept;
    [[nodiscard]] Result<RenderBatch> extract(LinearArena &arena) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace souls
