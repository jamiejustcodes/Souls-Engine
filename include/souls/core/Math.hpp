#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace souls {
inline constexpr float pi = 3.14159265358979323846F;
struct Vec3 {
    float x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 a, float s) noexcept {
    return {a.x * s, a.y * s, a.z * s};
}
inline float dot(Vec3 a, Vec3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalize(Vec3 a) noexcept {
    const float n = std::sqrt(dot(a, a));
    return n > 1e-6F ? a * (1 / n) : Vec3{};
}
// Column-major matrices, column vectors, world Z up; depth range [0,1].
struct Mat4 {
    std::array<float, 16> m{};
    static Mat4 identity() noexcept {
        Mat4 a;
        a.m[0] = a.m[5] = a.m[10] = a.m[15] = 1;
        return a;
    }
};
inline Mat4 operator*(const Mat4 &a, const Mat4 &b) noexcept {
    Mat4 c;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            for (int k = 0; k < 4; ++k)
                c.m[static_cast<std::size_t>(j * 4 + i)] +=
                    a.m[static_cast<std::size_t>(k * 4 + i)] * b.m[static_cast<std::size_t>(j * 4 + k)];
    return c;
}
inline Vec3 transform_point(const Mat4 &a, Vec3 p) noexcept {
    const auto &m = a.m;
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    return {(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) / w,
            (m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) / w,
            (m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]) / w};
}
inline Mat4 model_matrix(Vec3 p, Vec3 degrees, Vec3 scale) noexcept {
    const Vec3 r = degrees * (pi / 180);
    const float cx = std::cos(r.x), sx = std::sin(r.x), cy = std::cos(r.y), sy = std::sin(r.y),
                cz = std::cos(r.z), sz = std::sin(r.z);
    Mat4 m = Mat4::identity();
    m.m[0] = cz * cy * scale.x;
    m.m[1] = sz * cy * scale.x;
    m.m[2] = -sy * scale.x;
    m.m[4] = (cz * sy * sx - sz * cx) * scale.y;
    m.m[5] = (sz * sy * sx + cz * cx) * scale.y;
    m.m[6] = cy * sx * scale.y;
    m.m[8] = (cz * sy * cx + sz * sx) * scale.z;
    m.m[9] = (sz * sy * cx - cz * sx) * scale.z;
    m.m[10] = cy * cx * scale.z;
    m.m[12] = p.x;
    m.m[13] = p.y;
    m.m[14] = p.z;
    return m;
}
struct Camera {
    Vec3 position{6, -9, 5.5F};
    float yaw = 2.16F, pitch = -0.42F, speed = 4;
    Vec3 forward() const noexcept {
        return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    }
    Vec3 right() const noexcept {
        return normalize(cross(forward(), {0, 0, 1}));
    }
    Vec3 up() const noexcept {
        return cross(right(), forward());
    }
    Mat4 view() const noexcept {
        const auto r = right(), u = up(), f = forward();
        Mat4 m = Mat4::identity();
        m.m = {r.x,
               u.x,
               f.x,
               0,
               r.y,
               u.y,
               f.y,
               0,
               r.z,
               u.z,
               f.z,
               0,
               -dot(r, position),
               -dot(u, position),
               -dot(f, position),
               1};
        return m;
    }
    Mat4 projection(float aspect) const noexcept {
        Mat4 m;
        float y = 1 / std::tan(pi / 6);
        m.m[0] = y / aspect;
        m.m[5] = y;
        m.m[10] = 1000 / 999.9F;
        m.m[11] = 1;
        m.m[14] = -100 / 999.9F;
        return m;
    }
    Vec3 ray(float x, float y, float aspect) const noexcept {
        return normalize(forward() + right() * (x * aspect * std::tan(pi / 6)) +
                         up() * (y * std::tan(pi / 6)));
    }
    void focus(Vec3 p) noexcept {
        position = p - forward() * 8;
    }
};
} // namespace souls
