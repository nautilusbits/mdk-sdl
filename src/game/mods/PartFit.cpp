#include "game/mods/PartFit.h"

#include <algorithm>

#include "core/DotNet.h"

namespace Mdk::Game::Mods
{
    namespace
    {
        // new Matrix4x4(): all zeros (C++'s default is the identity).
        constexpr Matrix4x4 Zero{0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};

        // Matrix4x4.GetDeterminant (.NET's formula).
        float Determinant(const Matrix4x4& m)
        {
            const auto a = m.M11, b = m.M12, c = m.M13, d = m.M14;
            const auto e = m.M21, f = m.M22, g = m.M23, h = m.M24;
            const auto i = m.M31, j = m.M32, k = m.M33, l = m.M34;
            const auto mm = m.M41, n = m.M42, o = m.M43, p = m.M44;

            const auto kp_lo = k * p - l * o;
            const auto jp_ln = j * p - l * n;
            const auto jo_kn = j * o - k * n;
            const auto ip_lm = i * p - l * mm;
            const auto io_km = i * o - k * mm;
            const auto in_jm = i * n - j * mm;

            return a * (f * kp_lo - g * jp_ln + h * jo_kn) -
                b * (e * kp_lo - g * ip_lm + h * io_km) +
                c * (e * jp_ln - f * ip_lm + h * in_jm) -
                d * (e * jo_kn - f * io_km + g * in_jm);
        }
    }

    PartFit::PartFit(std::vector<Vector3> vertices, std::vector<int> triangles)
        : _triangles(std::move(triangles)), _vertices(std::move(vertices)), _vertexCount(static_cast<int>(_vertices.size()))
    {
        _posed.resize(_vertices.size() + _triangles.size() / TriangleCorners);
        _rest.resize(_posed.size());
        Points(_vertices, _rest);
        _restCentre = Centre(_rest);
        for (auto& r : _rest)
        {
            r -= _restCentre;
        }

        auto covariance = Zero;
        for (const auto& p : _rest)
        {
            covariance = covariance + Outer(p, p);
        }

        covariance.M44 = 1.f;
        const auto scale = covariance.M11 + covariance.M22 + covariance.M33;
        const auto determinant = Determinant(covariance);
        Matrix4x4 inverse;
        if (!_rest.empty() && MathF::Abs(determinant) > Degenerate * scale * scale * scale && Matrix4x4::Invert(covariance, inverse))
        {
            _inverse = inverse;
        }
    }

    Matrix4x4 PartFit::Fit(std::span<const Vector3> posed)
    {
        // C#: posed is the model's own array. Poses are copies here: the rest pose by value.
        if (_rest.empty() || std::ssize(posed) != _vertexCount || std::equal(posed.begin(), posed.end(), _vertices.begin()))
        {
            return Matrix4x4::Identity();
        }

        Points(posed, _posed);
        const auto centre = Centre(_posed);
        if (!_inverse)
        {
            return Matrix4x4::CreateTranslation(centre - _restCentre);
        }

        // Σ q pᵀ in row-vector form: p · M = q  ⇒  M = C⁻¹ (Σ p qᵀ).
        auto cross = Zero;
        for (size_t i = 0; i < _rest.size(); i++)
        {
            cross = cross + Outer(_rest[i], _posed[i] - centre);
        }

        cross.M44 = 1.f;
        auto linear = *_inverse * cross;
        linear.M44 = 1.f;
        const auto translation = centre - Vector3::Transform(_restCentre, linear);
        linear.M41 = translation.X;
        linear.M42 = translation.Y;
        linear.M43 = translation.Z;
        return linear;
    }

    void PartFit::Points(std::span<const Vector3> vertices, std::span<Vector3> points) const
    {
        std::copy(vertices.begin(), vertices.end(), points.begin());
        for (size_t t = 0; t < _triangles.size() / TriangleCorners; t++)
        {
            const auto a = vertices[_triangles[t * TriangleCorners]];
            const auto b = vertices[_triangles[t * TriangleCorners + 1]];
            const auto c = vertices[_triangles[t * TriangleCorners + 2]];
            const auto normal = Vector3::Cross(b - a, c - a);
            const auto length = normal.Length();
            const auto centroid = (a + b + c) / static_cast<float>(TriangleCorners);
            points[vertices.size() + t] = length > 0.f ? centroid + normal / MathF::Sqrt(length) : centroid;
        }
    }

    Vector3 PartFit::Centre(std::span<const Vector3> points)
    {
        auto sum = Vector3::Zero();
        for (const auto& p : points)
        {
            sum += p;
        }

        return points.empty() ? sum : sum / static_cast<float>(points.size());
    }
}
