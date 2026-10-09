#include <cstdio>
#include <cstring>
#include <flecs.h>
#include <new>
#include <souls/scene/Scene.hpp>
namespace souls {
namespace {
float primitive_hit(ActorKind kind, Vec3 o, Vec3 r) noexcept {
    if (kind == ActorKind::plane) {
        if (std::abs(r.z) < 1e-6F)
            return -1;
        const float t = -o.z / r.z;
        const auto p = o + r * t;
        return t >= 0 && std::abs(p.x) <= 1 && std::abs(p.y) <= 1 ? t : -1;
    }
    float closest = 1000;
    const auto sphere = [&](Vec3 center, float radius, int hemisphere) {
        const Vec3 origin = o - center;
        const float aa = dot(r, r), bb = dot(origin, r), cc = dot(origin, origin) - radius * radius;
        const float discriminant = bb * bb - aa * cc;
        if (aa < 1e-12F || discriminant < 0)
            return;
        const float root = std::sqrt(discriminant);
        for (float t : {(-bb - root) / aa, (-bb + root) / aa}) {
            const float z = origin.z + r.z * t;
            if (t >= 0 && t < closest && (hemisphere == 0 || z * static_cast<float>(hemisphere) >= 0))
                closest = t;
        }
    };
    if (kind == ActorKind::sphere) {
        sphere({}, 1, 0);
        return closest < 1000 ? closest : -1;
    }
    if (kind == ActorKind::cylinder || kind == ActorKind::capsule) {
        const bool capsule = kind == ActorKind::capsule;
        const float radius = capsule ? 0.5F : 1.0F, half = capsule ? 0.5F : 1.0F;
        const float aa = r.x * r.x + r.y * r.y, bb = o.x * r.x + o.y * r.y;
        const float cc = o.x * o.x + o.y * o.y - radius * radius, disc = bb * bb - aa * cc;
        if (aa > 1e-12F && disc >= 0) {
            const float root = std::sqrt(disc);
            for (float t : {(-bb - root) / aa, (-bb + root) / aa})
                if (t >= 0 && t < closest && std::abs(o.z + r.z * t) <= half)
                    closest = t;
        }
        if (capsule) {
            sphere({0, 0, half}, radius, 1);
            sphere({0, 0, -half}, radius, -1);
        } else if (std::abs(r.z) > 1e-6F) {
            for (float z : {-half, half}) {
                const float t = (z - o.z) / r.z;
                const auto p = o + r * t;
                if (t >= 0 && t < closest && p.x * p.x + p.y * p.y <= radius * radius)
                    closest = t;
            }
        }
        return closest < 1000 ? closest : -1;
    }
    float near = 0, far = 1000;
    const float origins[]{o.x, o.y, o.z}, dirs[]{r.x, r.y, r.z};
    for (int i = 0; i < 3; ++i) {
        if (std::abs(dirs[i]) < 1e-6F) {
            if (std::abs(origins[i]) > 1)
                return -1;
        } else {
            float lower = (-1 - origins[i]) / dirs[i], upper = (1 - origins[i]) / dirs[i];
            if (lower > upper)
                std::swap(lower, upper);
            near = std::max(near, lower);
            far = std::min(far, upper);
        }
    }
    if (kind == ActorKind::wedge) {
        // Clip the cube interval against the wedge's sloping top, z <= x.
        const float origin = o.z - o.x, direction = r.z - r.x;
        if (std::abs(direction) < 1e-6F) {
            if (origin > 0)
                return -1;
        } else if (direction < 0)
            near = std::max(near, -origin / direction);
        else
            far = std::min(far, -origin / direction);
    }
    return far >= near ? near : -1;
}
} // namespace
struct ActorMetadata {
    std::array<char, 64> label{};
    ActorKind kind = ActorKind::cube;
    bool visible = true;
    float intensity = 100000, attenuation = 0;
    Vec3 color{1, 0.95F, 0.84F};
    EntityHandle handle{};
    std::uint32_t material = 0;
    std::array<char, 64> group{};
    bool locked = false;
};
struct Scene::Impl {
    ecs_world_t *world = nullptr;
    ecs_query_t *query = nullptr;
    ecs_entity_t map = 0, lighting = 0, geometry = 0;
    std::array<ecs_entity_t, 5> components{};
    Pool<ecs_entity_t, EntityTag, 4096> entities;
    std::array<EntityHandle, 4096> handles{};
    std::size_t count = 0;
    ecs_entity_t metadata = 0;
    std::array<ecs_entity_t, 6> transform_columns{};
    ~Impl() {
        if (query)
            ecs_query_fini(query);
        if (world)
            ecs_fini(world);
    }
};
Scene::Scene() noexcept = default;
Scene::~Scene() = default;
Scene::Scene(Scene &&) noexcept = default;
Scene &Scene::operator=(Scene &&) noexcept = default;
Result<Scene> Scene::create() noexcept {
    Scene result;
    result.impl_.reset(new (std::nothrow) Impl);
    if (!result.impl_)
        return std::unexpected(Error{ErrorCode::exhausted, "Scene storage allocation failed"});
    auto &p = *result.impl_;
    p.world = ecs_init();
    if (!p.world)
        return std::unexpected(Error{ErrorCode::exhausted, "Flecs initialization failed"});
    const auto named = [&](const char *name, ecs_entity_t parent) {
        ecs_entity_desc_t desc{};
        desc.name = name;
        auto id = ecs_entity_init(p.world, &desc);
        if (parent)
            ecs_add_pair(p.world, id, EcsChildOf, parent);
        return id;
    };
    auto root = named("World", 0);
    p.map = named("DefaultMap", root);
    p.lighting = named("Lighting", p.map);
    p.geometry = named("Geometry", p.map);
    const char *names[]{"PositionX", "PositionY", "PositionZ", "BoundsRadius", "MaterialIndex"};
    for (std::size_t i = 0; i < p.components.size(); ++i) {
        ecs_entity_desc_t entity{};
        entity.name = names[i];
        ecs_component_desc_t component{};
        component.entity = ecs_entity_init(p.world, &entity);
        component.type.size = i == 4 ? sizeof(std::uint32_t) : sizeof(float);
        component.type.alignment = i == 4 ? alignof(std::uint32_t) : alignof(float);
        p.components[i] = ecs_component_init(p.world, &component);
        if (!p.components[i])
            return std::unexpected(Error{ErrorCode::exhausted, "Flecs component registration failed"});
    }
    ecs_component_desc_t meta{};
    meta.type.size = sizeof(ActorMetadata);
    meta.type.alignment = alignof(ActorMetadata);
    p.metadata = ecs_component_init(p.world, &meta);
    for (auto &id : p.transform_columns) {
        ecs_component_desc_t c{};
        c.type.size = sizeof(float);
        c.type.alignment = alignof(float);
        id = ecs_component_init(p.world, &c);
    }
    ecs_query_desc_t desc{};
    desc.cache_kind = EcsQueryCacheAll;
    for (std::size_t i = 0; i < p.components.size(); ++i) {
        desc.terms[i].id = p.components[i];
        desc.terms[i].inout = EcsIn;
    }
    for (std::size_t i = 0; i < 6; ++i) {
        desc.terms[i + 5].id = p.transform_columns[i];
        desc.terms[i + 5].inout = EcsIn;
    }
    desc.terms[11].id = p.metadata;
    desc.terms[11].inout = EcsIn;
    p.query = ecs_query_init(p.world, &desc);
    if (!p.query)
        return std::unexpected(Error{ErrorCode::exhausted, "Flecs query creation failed"});
    return result;
}
Result<EntityHandle> Scene::add(Transform transform, float radius, std::uint32_t material) noexcept {
    auto &p = *impl_;
    auto handle = p.entities.emplace(ecs_entity_t{0});
    if (!handle)
        return std::unexpected(handle.error());
    auto entity = ecs_new(p.world);
    ecs_add_pair(p.world, entity, EcsChildOf, p.geometry);
    *p.entities.get(*handle) = entity;
    const float values[]{transform.x, transform.y, transform.z, radius};
    for (std::size_t i = 0; i < 4; ++i)
        ecs_set_id(p.world, entity, p.components[i], sizeof(float), &values[i]);
    ecs_set_id(p.world, entity, p.components[4], sizeof(material), &material);
    ActorMetadata a{};
    a.handle = *handle;
    a.material = material;
    std::snprintf(a.label.data(), a.label.size(), "Actor_%u", handle->index);
    ecs_set_id(p.world, entity, p.metadata, sizeof(a), &a);
    const float extra[]{transform.rotation.x, transform.rotation.y, transform.rotation.z,
                        transform.scale.x,    transform.scale.y,    transform.scale.z};
    for (std::size_t i = 0; i < 6; ++i)
        ecs_set_id(p.world, entity, p.transform_columns[i], sizeof(float), &extra[i]);
    p.handles[p.count++] = *handle;
    return *handle;
}
Result<void> Scene::remove(EntityHandle entity) noexcept {
    auto *id = impl_->entities.get(entity);
    if (!id)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid scene entity"});
    ecs_delete(impl_->world, *id);
    for (std::size_t i = 0; i < impl_->count; ++i)
        if (impl_->handles[i] == entity) {
            impl_->handles[i] = impl_->handles[--impl_->count];
            break;
        }
    return impl_->entities.erase(entity);
}
Result<RenderBatch> Scene::extract(LinearArena &arena) const noexcept {
    const auto count = size();
    // One arena block holds every column. The batch borrows it until the frame slot retires.
    auto storage = arena.allocate(count * 72, alignof(EntityHandle));
    if (!storage)
        return std::unexpected(storage.error());
    if (!count)
        return RenderBatch{};
    auto *base = reinterpret_cast<float *>(storage->data());
    RenderBatch batch{{base, count},
                      {base + count, count},
                      {base + count * 2, count},
                      {base + count * 3, count},
                      {reinterpret_cast<std::uint32_t *>(base + count * 13), count}};
    for (std::size_t i = 0; i < 3; ++i) {
        batch.rotation[i] = {base + count * (4 + i), count};
        batch.scale[i] = {base + count * (7 + i), count};
        batch.color[i] = {base + count * (10 + i), count};
    }
    batch.handles = {reinterpret_cast<EntityHandle *>(base + count * 14), count};
    batch.visible = {reinterpret_cast<std::uint32_t *>(base + count * 16), count};
    batch.kind = {reinterpret_cast<std::uint32_t *>(base + count * 17), count};
    std::size_t offset = 0;
    auto it = ecs_query_iter(impl_->world, impl_->query);
    while (ecs_query_next(&it)) {
        const auto n = static_cast<std::size_t>(it.count);
        const std::array<std::span<float>, 4> columns{batch.x, batch.y, batch.z, batch.radius};
        for (std::int8_t column = 0; column < 4; ++column) {
            const auto *values = static_cast<const float *>(ecs_field_w_size(&it, sizeof(float), column));
            for (std::size_t row = 0; row < n; ++row)
                columns[static_cast<std::size_t>(column)][offset + row] = values[row];
        }
        const auto *values =
            static_cast<const std::uint32_t *>(ecs_field_w_size(&it, sizeof(std::uint32_t), 4));
        for (std::size_t row = 0; row < n; ++row)
            batch.material[offset + row] = values[row];
        for (std::int8_t column = 0; column < 6; ++column) {
            const auto *transform_values = static_cast<const float *>(
                ecs_field_w_size(&it, sizeof(float), static_cast<std::int8_t>(column + 5)));
            auto dest = column < 3 ? batch.rotation[static_cast<std::size_t>(column)]
                                   : batch.scale[static_cast<std::size_t>(column - 3)];
            for (std::size_t row = 0; row < n; ++row)
                dest[offset + row] = transform_values[row];
        }
        const auto *meta =
            static_cast<const ActorMetadata *>(ecs_field_w_size(&it, sizeof(ActorMetadata), 11));
        for (std::size_t row = 0; row < n; ++row) {
            batch.kind[offset + row] = static_cast<std::uint32_t>(meta[row].kind);
            batch.handles[offset + row] = meta[row].handle;
            batch.visible[offset + row] = meta[row].visible ? 1U : 0U;
            batch.color[0][offset + row] = meta[row].color.x;
            batch.color[1][offset + row] = meta[row].color.y;
            batch.color[2][offset + row] = meta[row].color.z;
        }
        offset += n;
    }
    assert(offset == count);
    return batch;
}
std::size_t Scene::size() const noexcept {
    return impl_->entities.size();
}
std::span<const EntityHandle> Scene::actors() const noexcept {
    return {impl_->handles.data(), impl_->count};
}
Result<Actor> Scene::actor(EntityHandle h) const noexcept {
    const auto *id = impl_->entities.get(h);
    if (!id)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid actor"});
    const auto &meta = *static_cast<const ActorMetadata *>(ecs_get_id(impl_->world, *id, impl_->metadata));
    Actor a{};
    a.label = meta.label;
    a.kind = meta.kind;
    a.visible = meta.visible;
    a.intensity = meta.intensity;
    a.attenuation = meta.attenuation;
    a.color = meta.color;
    a.material = meta.material;
    a.group = meta.group;
    a.locked = meta.locked;
    a.transform.x = *static_cast<const float *>(ecs_get_id(impl_->world, *id, impl_->components[0]));
    a.transform.y = *static_cast<const float *>(ecs_get_id(impl_->world, *id, impl_->components[1]));
    a.transform.z = *static_cast<const float *>(ecs_get_id(impl_->world, *id, impl_->components[2]));
    const auto &c = impl_->transform_columns;
    a.transform.rotation = {*static_cast<const float *>(ecs_get_id(impl_->world, *id, c[0])),
                            *static_cast<const float *>(ecs_get_id(impl_->world, *id, c[1])),
                            *static_cast<const float *>(ecs_get_id(impl_->world, *id, c[2]))};
    a.transform.scale = {*static_cast<const float *>(ecs_get_id(impl_->world, *id, c[3])),
                         *static_cast<const float *>(ecs_get_id(impl_->world, *id, c[4])),
                         *static_cast<const float *>(ecs_get_id(impl_->world, *id, c[5]))};
    return a;
}
Result<void> Scene::update(EntityHandle h, const Actor &a) noexcept {
    const auto *id = impl_->entities.get(h);
    if (!id)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid actor"});
    const auto &t = a.transform;
    const float v[]{t.x, t.y, t.z, t.rotation.x, t.rotation.y, t.rotation.z, t.scale.x, t.scale.y, t.scale.z};
    for (float f : v)
        if (!std::isfinite(f))
            return std::unexpected(Error{ErrorCode::invalid_argument, "Non-finite transform"});
    if (std::abs(t.scale.x) < 0.001F || std::abs(t.scale.y) < 0.001F || std::abs(t.scale.z) < 0.001F ||
        !std::isfinite(a.intensity) || a.intensity < 0 || !std::isfinite(a.attenuation) || a.attenuation < 0)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid scale or light intensity"});
    if (static_cast<std::uint32_t>(a.kind) >= actor_kind_count || !std::isfinite(a.color.x) ||
        !std::isfinite(a.color.y) || !std::isfinite(a.color.z) || a.color.x < 0 || a.color.y < 0 ||
        a.color.z < 0)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid actor kind or color"});
    ActorMetadata meta{a.label, a.kind, a.visible, a.intensity, a.attenuation, a.color, h, a.material};
    meta.label.back() = 0;
    meta.group = a.group;
    meta.group.back() = 0;
    meta.locked = a.locked;
    auto parent =
        (a.kind == ActorKind::light || a.kind == ActorKind::sky) ? impl_->lighting : impl_->geometry;
    if (meta.group[0]) {
        auto folder = ecs_lookup_child(impl_->world, parent, meta.group.data());
        if (!folder) {
            // Folder creation is a structural editor operation; stored transforms remain world-space.
            ecs_entity_desc_t desc{};
            desc.parent = parent;
            desc.name = meta.group.data();
            folder = ecs_entity_init(impl_->world, &desc);
        }
        parent = folder;
    }
    if (ecs_get_target(impl_->world, *id, EcsChildOf, 0) != parent)
        ecs_add_pair(impl_->world, *id, EcsChildOf, parent);
    ecs_set_id(impl_->world, *id, impl_->metadata, sizeof(meta), &meta);
    ecs_set_id(impl_->world, *id, impl_->components[4], sizeof(a.material), &a.material);
    for (std::size_t i = 0; i < 3; ++i)
        ecs_set_id(impl_->world, *id, impl_->components[i], sizeof(float), &v[i]);
    for (std::size_t i = 0; i < 6; ++i)
        ecs_set_id(impl_->world, *id, impl_->transform_columns[i], sizeof(float), &v[i + 3]);
    return {};
}
Result<EntityHandle> Scene::spawn(ActorKind kind, const char *label, Transform t) noexcept {
    auto h = add(t, kind == ActorKind::sphere ? 1.0F : 1.732F, kind == ActorKind::sphere ? 1U : 0U);
    if (!h)
        return h;
    auto a = actor(*h);
    a->kind = kind;
    auto id = *impl_->entities.get(*h);
    ecs_add_pair(impl_->world, id, EcsChildOf,
                 (kind == ActorKind::light || kind == ActorKind::sky) ? impl_->lighting : impl_->geometry);
    if (kind == ActorKind::cube)
        a->color = {0.36F, 0.44F, 0.61F};
    if (kind == ActorKind::sphere)
        a->color = {0.72F, 0.36F, 0.12F};
    if (kind == ActorKind::sphere)
        a->material = 1;
    if (kind == ActorKind::sky)
        a->color = {0.075F, 0.12F, 0.21F};
    std::snprintf(a->label.data(), a->label.size(), "%s", label);
    auto done = update(*h, *a);
    if (!done) {
        const auto ignored = remove(*h);
        (void)ignored;
        return std::unexpected(done.error());
    }
    return h;
}
Result<EntityHandle> Scene::duplicate(EntityHandle h) noexcept {
    auto a = actor(h);
    if (!a)
        return std::unexpected(a.error());
    a->transform.x += 2;
    auto b = spawn(a->kind, a->label.data(), a->transform);
    if (!b)
        return b;
    std::snprintf(a->label.data(), a->label.size(), "Copy_%u", b->index);
    auto done = update(*b, *a);
    if (!done)
        return std::unexpected(done.error());
    return b;
}
Result<void> Scene::playground() noexcept {
    for (auto kind :
         {ActorKind::light, ActorKind::sky, ActorKind::floor, ActorKind::cube, ActorKind::sphere}) {
        const char *names[]{"Test_Cube", "Test_Sphere", "DirectionalLight", "SkyAtmosphere", "Floor_Grid"};
        Transform t{};
        if (kind == ActorKind::cube)
            t = {-2, 0, 1};
        if (kind == ActorKind::sphere)
            t = {2, 0, 1};
        if (kind == ActorKind::light)
            t.rotation = {0, 45, -35};
        auto h = spawn(kind, names[static_cast<std::size_t>(kind)], t);
        if (!h)
            return std::unexpected(h.error());
    }
    return {};
}
EntityHandle Scene::pick(const Camera &camera, Vec3 ray) const noexcept {
    float closest = 1000;
    EntityHandle selected{};
    for (auto h : actors()) {
        const auto a = actor(h);
        if (!a || !a->visible || a->locked || !is_primitive(a->kind))
            continue;
        const auto &t = a->transform;
        const auto rotation = model_matrix({}, t.rotation, {1, 1, 1});
        const Vec3 d = camera.position - Vec3{t.x, t.y, t.z};
        const auto local = [&](Vec3 v) {
            const auto &m = rotation.m;
            return Vec3{(m[0] * v.x + m[1] * v.y + m[2] * v.z) / t.scale.x,
                        (m[4] * v.x + m[5] * v.y + m[6] * v.z) / t.scale.y,
                        (m[8] * v.x + m[9] * v.y + m[10] * v.z) / t.scale.z};
        };
        // Leave the local ray unnormalized so hit distances remain comparable across scaled actors.
        const auto o = local(d), r = local(ray);
        const float hit = primitive_hit(a->kind, o, r);
        if (hit >= 0 && hit < closest) {
            closest = hit;
            selected = h;
        }
    }
    return selected;
}
} // namespace souls
