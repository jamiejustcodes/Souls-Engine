#include "ShaderData.hpp"
#include <souls/renderer/Renderer.hpp>
#include <vector>
namespace souls {
Result<void> Renderer::verify_resources() noexcept {
    const rhi::Vertex vertex{{0, 0, 0}, {0, 0, 1}};
    auto b = device_.create_buffer(std::as_bytes(std::span{&vertex, 1}), rhi::BufferUsage::vertex);
    if (!b)
        return std::unexpected(b.error());
    if (auto done = device_.destroy_buffer(*b); !done)
        return done;
    if (device_.destroy_buffer(*b))
        return std::unexpected(Error{ErrorCode::gpu, "Stale buffer accepted"});
    auto p = device_.create_pipeline(
        {std::as_bytes(std::span{shaders::mesh_vs}), std::as_bytes(std::span{shaders::mesh_ps}), false});
    if (!p)
        return std::unexpected(p.error());
    if (auto done = device_.destroy_pipeline(*p); !done)
        return done;
    if (device_.destroy_pipeline(*p))
        return std::unexpected(Error{ErrorCode::gpu, "Stale pipeline accepted"});
    return {};
}
Result<void> Renderer::initialize() noexcept {
    if (mesh_pipeline_)
        return {};
    auto mesh = device_.create_pipeline(
        {std::as_bytes(std::span{shaders::mesh_vs}), std::as_bytes(std::span{shaders::mesh_ps}), false});
    if (!mesh)
        return std::unexpected(mesh.error());
    mesh_pipeline_ = *mesh;
    auto grid = device_.create_pipeline(
        {std::as_bytes(std::span{shaders::grid_vs}), std::as_bytes(std::span{shaders::grid_ps}), true});
    if (!grid)
        return std::unexpected(grid.error());
    grid_pipeline_ = *grid;
    const auto upload = [&](std::size_t slot, std::span<const rhi::Vertex> vertices,
                            std::span<const std::uint32_t> indices) -> Result<void> {
        auto v = device_.create_buffer(std::as_bytes(vertices), rhi::BufferUsage::vertex);
        if (!v)
            return std::unexpected(v.error());
        meshes_[slot].vertices = *v;
        auto i = device_.create_buffer(std::as_bytes(indices), rhi::BufferUsage::index);
        if (!i)
            return std::unexpected(i.error());
        meshes_[slot].indices = *i;
        meshes_[slot].count = static_cast<std::uint32_t>(indices.size());
        return {};
    };
    std::array<rhi::Vertex, 24> cube{};
    std::array<std::uint32_t, 36> cube_indices{};
    const Vec3 normals[]{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (std::uint32_t face = 0; face < 6; ++face) {
        const auto n = normals[face];
        auto u = normalize(cross(std::abs(n.z) > 0.5F ? Vec3{0, 1, 0} : Vec3{0, 0, 1}, n)), v = cross(n, u);
        const Vec3 points[]{n - u - v, n + u - v, n + u + v, n - u + v};
        for (std::uint32_t j = 0; j < 4; ++j)
            cube[face * 4 + j] = {points[j], n};
        const std::uint32_t order[]{0, 1, 2, 0, 2, 3};
        for (std::uint32_t j = 0; j < 6; ++j)
            cube_indices[face * 6 + j] = face * 4 + order[j];
    }
    if (auto ready = upload(0, cube, cube_indices); !ready)
        return ready;
    // Mesh generation and uploads happen at startup; recording only reads the resulting handles.
    constexpr std::uint32_t rings = 24, sectors = 48;
    std::vector<rhi::Vertex> sphere;
    std::vector<std::uint32_t> indices;
    sphere.reserve((rings + 1) * (sectors + 1));
    indices.reserve(rings * sectors * 6);
    for (std::uint32_t y = 0; y <= rings; ++y)
        for (std::uint32_t x = 0; x <= sectors; ++x) {
            float theta = pi * static_cast<float>(y) / rings, phi = 2 * pi * static_cast<float>(x) / sectors;
            Vec3 n{std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
            sphere.push_back({n, n});
        }
    for (std::uint32_t y = 0; y < rings; ++y)
        for (std::uint32_t x = 0; x < sectors; ++x) {
            auto a = y * (sectors + 1) + x, b = a + sectors + 1;
            for (auto i : {a, b, a + 1, a + 1, b, b + 1})
                indices.push_back(i);
        }
    if (auto ready = upload(1, sphere, indices); !ready)
        return ready;

    // The same vertex contract serves every built-in part. Temporary vectors are
    // startup storage; GPU recording only sees the uploaded, immutable meshes.
    std::vector<rhi::Vertex> parts;
    std::vector<std::uint32_t> triangles;
    const auto triangle = [&](Vec3 a, Vec3 b, Vec3 c) {
        const auto first = static_cast<std::uint32_t>(parts.size());
        const auto normal = normalize(cross(b - a, c - a));
        for (auto p : {a, b, c})
            parts.push_back({p, normal});
        for (auto i : {first, first + 1, first + 2})
            triangles.push_back(i);
    };
    constexpr std::uint32_t sides = 48;
    for (std::uint32_t i = 0; i < sides; ++i) {
        const float a = 2 * pi * static_cast<float>(i) / sides;
        const float b = 2 * pi * static_cast<float>(i + 1) / sides;
        Vec3 n0{std::cos(a), std::sin(a), 0}, n1{std::cos(b), std::sin(b), 0};
        auto first = static_cast<std::uint32_t>(parts.size());
        parts.push_back({n0 + Vec3{0, 0, -1}, n0});
        parts.push_back({n1 + Vec3{0, 0, -1}, n1});
        parts.push_back({n1 + Vec3{0, 0, 1}, n1});
        parts.push_back({n0 + Vec3{0, 0, 1}, n0});
        for (auto j : {0U, 1U, 2U, 0U, 2U, 3U})
            triangles.push_back(first + j);
        triangle({0, 0, 1}, n0 + Vec3{0, 0, 1}, n1 + Vec3{0, 0, 1});
        triangle({0, 0, -1}, n1 + Vec3{0, 0, -1}, n0 + Vec3{0, 0, -1});
    }
    if (auto ready = upload(2, parts, triangles); !ready)
        return ready;
    parts.clear();
    triangles.clear();
    const Vec3 a{-1, -1, -1}, b{1, -1, -1}, c{1, 1, -1}, d{-1, 1, -1}, e{1, -1, 1}, f{1, 1, 1};
    triangle(a, c, b);
    triangle(a, d, c);
    triangle(b, c, f);
    triangle(b, f, e);
    triangle(a, e, f);
    triangle(a, f, d);
    triangle(a, b, e);
    triangle(d, f, c);
    if (auto ready = upload(3, parts, triangles); !ready)
        return ready;
    parts.clear();
    triangles.clear();
    constexpr std::uint32_t half_rings = 12;
    for (std::uint32_t row = 0; row <= 2 * half_rings + 1; ++row) {
        const bool upper = row <= half_rings;
        const float theta =
            upper ? pi * 0.5F * static_cast<float>(row) / half_rings
                  : pi * 0.5F + pi * 0.5F * static_cast<float>(row - half_rings - 1) / half_rings;
        for (std::uint32_t sector = 0; sector <= sectors; ++sector) {
            const float phi = 2 * pi * static_cast<float>(sector) / sectors;
            Vec3 n{std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
            parts.push_back({n * 0.5F + Vec3{0, 0, upper ? 0.5F : -0.5F}, n});
        }
    }
    for (std::uint32_t row = 0; row < 2 * half_rings + 1; ++row)
        for (std::uint32_t sector = 0; sector < sectors; ++sector) {
            const auto top = row * (sectors + 1) + sector, bottom = top + sectors + 1;
            for (auto i : {top, bottom, top + 1, top + 1, bottom, bottom + 1})
                triangles.push_back(i);
        }
    if (auto ready = upload(4, parts, triangles); !ready)
        return ready;
    parts.clear();
    triangles.clear();
    triangle({-1, -1, 0}, {1, -1, 0}, {1, 1, 0});
    triangle({-1, -1, 0}, {1, 1, 0}, {-1, 1, 0});
    return upload(5, parts, triangles);
}
Result<void> Renderer::resize_viewport(rhi::Extent extent) noexcept {
    if (extent == extent_ || !extent.width || !extent.height)
        return {};
    auto next = device_.create_render_target(extent);
    if (!next)
        return std::unexpected(next.error());
    if (target_) {
        if (auto retired = device_.destroy_texture(target_); !retired) {
            const auto ignored = device_.destroy_texture(*next);
            (void)ignored;
            return retired;
        }
    }
    target_ = *next;
    extent_ = extent;
    return {};
}
Result<void> Renderer::record(rhi::CommandList list, const Scene &scene, const RenderBatch &batch,
                              const Camera &camera, EntityHandle selected, bool grid, bool lit,
                              std::span<const EntityHandle> selection) noexcept {
    if (!target_ || !mesh_pipeline_)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Renderer not initialized"});
    const float aspect = static_cast<float>(extent_.width) / static_cast<float>(extent_.height);
    const auto pack = [](Vec3 v, float w = 0) { return std::array<float, 4>{v.x, v.y, v.z, w}; };
    rhi::FrameConstants frame{};
    frame.view_projection = camera.projection(aspect) * camera.view();
    frame.camera = pack(camera.position, 1);
    frame.right = pack(camera.right());
    frame.up = pack(camera.up());
    frame.forward = pack(camera.forward());
    frame.light_direction = {0.4F, 0.5F, -0.8F, 0};
    frame.light_color = {0, 0, 0, 1};
    frame.grid_origin = {0, 0, 0, 1};
    frame.grid_u = {1, 0, 0, 0};
    frame.grid_v = {0, 1, 0, 0};
    frame.grid_normal = {0, 0, 1, 0};
    frame.sky_color = {0.075F, 0.12F, 0.21F, 1};
    frame.options = {aspect, std::tan(pi / 6), grid ? 1.0F : 0.0F, lit ? 1.0F : 0.0F};
    for (auto h : scene.actors()) {
        auto a = scene.actor(h);
        if (a->kind == ActorKind::light && a->visible) {
            const auto m = model_matrix({}, a->transform.rotation, {1, 1, 1});
            auto direction = normalize(Vec3{m.m[0], m.m[1], m.m[2]});
            frame.light_direction = pack(direction);
            frame.light_color = pack(a->color * std::clamp(a->intensity / 100000.0F, 0.0F, 8.0F), 1);
        }
        if (a->kind == ActorKind::sky) {
            frame.sky_color = pack(a->color, a->visible ? 1.0F : 0.0F);
        }
        if (a->kind == ActorKind::floor) {
            frame.options[2] = grid && a->visible ? 1.0F : 0.0F;
            const auto &t = a->transform;
            auto m = model_matrix({}, t.rotation, {1, 1, 1});
            frame.grid_origin = pack({t.x, t.y, t.z}, 1);
            frame.grid_u = pack(Vec3{m.m[0], m.m[1], m.m[2]} * (1 / t.scale.x));
            frame.grid_v = pack(Vec3{m.m[4], m.m[5], m.m[6]} * (1 / t.scale.y));
            frame.grid_normal = pack({m.m[8], m.m[9], m.m[10]});
        }
    }
    if (auto begin = device_.begin_geometry(list, target_, frame); !begin)
        return begin;
    draws_ = 0;
    rhi::DrawConstants draw{};
    draw.model = Mat4::identity();
    if (auto ready = device_.draw_geometry(list, grid_pipeline_, {}, {}, 3, draw); !ready)
        return ready;
    ++draws_;
    for (std::size_t i = 0; i < batch.x.size(); ++i) {
        auto kind = static_cast<ActorKind>(batch.kind[i]);
        if (!batch.visible[i] || !is_primitive(kind))
            continue;
        draw.model = model_matrix({batch.x[i], batch.y[i], batch.z[i]},
                                  {batch.rotation[0][i], batch.rotation[1][i], batch.rotation[2][i]},
                                  {batch.scale[0][i], batch.scale[1][i], batch.scale[2][i]});
        draw.color = {batch.color[0][i], batch.color[1][i], batch.color[2][i], 1};
        const bool highlighted =
            batch.handles[i] == selected ||
            std::find(selection.begin(), selection.end(), batch.handles[i]) != selection.end();
        draw.flags = {highlighted ? 1.0F : 0.0F, 0, 0, 0};
        const auto &mesh = meshes_[primitive_mesh_index(kind)];
        if (auto ready =
                device_.draw_geometry(list, mesh_pipeline_, mesh.vertices, mesh.indices, mesh.count, draw);
            !ready)
            return ready;
        ++draws_;
    }
    return device_.end_geometry(list, target_);
}
Result<void> Renderer::shutdown() noexcept {
    if (target_) {
        auto done = device_.destroy_texture(target_);
        if (!done)
            return done;
        target_ = {};
        extent_ = {};
    }
    for (auto &mesh : meshes_) {
        if (mesh.vertices) {
            auto done = device_.destroy_buffer(mesh.vertices);
            if (!done)
                return done;
            mesh.vertices = {};
        }
        if (mesh.indices) {
            auto done = device_.destroy_buffer(mesh.indices);
            if (!done)
                return done;
            mesh.indices = {};
        }
    }
    for (auto *pipeline : {&mesh_pipeline_, &grid_pipeline_})
        if (*pipeline) {
            auto done = device_.destroy_pipeline(*pipeline);
            if (!done)
                return done;
            *pipeline = {};
        }
    return {};
}
} // namespace souls
