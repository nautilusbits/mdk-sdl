#pragma once

#include <optional>
#include <span>
#include <vector>

#include "core/Numerics.h"

namespace Mdk::Game::Mods
{
    // How a model part moves from its rest pose to a pose, as one affine transform: the
    // least-squares fit of the posed points to the rest ones. MDK animates parts by moving their
    // vertices (rigid matrix tracks, or per-vertex deltas); a replaced part (a mod's mesh) follows the
    // original part through this fit. Rigid motion is fitted exactly; deformation approximately.
    // The points are the part's vertices and, per triangle, a point off its plane (so flat parts fit).
    //   rest p, posed q (centred):  A = (Σ q pᵀ)(Σ p pᵀ)⁻¹,  t = q̄ − A p̄      posed ≈ A · rest + t
    //   apex of a triangle: centroid + normal / √|normal|   (a length of the triangle's size)
    // Degenerate parts (a point, a line) only move.
    class PartFit
    {
    public:
        PartFit(std::vector<Vector3> vertices, std::vector<int> triangles);

        // The transform (System.Numerics' row vectors: Vector3.Transform(rest, fit)) taking the rest
        // pose to `posed` (the part's vertices in a pose).
        Matrix4x4 Fit(std::span<const Vector3> posed);

    private:
        // The covariance's determinant below this share of its scale is degenerate.
        static constexpr float Degenerate = 1e-6f;
        static constexpr int TriangleCorners = 3;

        std::vector<int> _triangles;
        // The model's own vertices: its rest pose, shared by unanimated objects.
        std::vector<Vector3> _vertices;
        int _vertexCount = 0;
        Vector3 _restCentre;
        // The rest points, centred.
        std::vector<Vector3> _rest;
        // (Σ p pᵀ)⁻¹, or null when degenerate.
        std::optional<Matrix4x4> _inverse;
        // The posed points (scratch: no allocation per fit).
        std::vector<Vector3> _posed;

        // The vertices, then each triangle's apex.
        void Points(std::span<const Vector3> vertices, std::span<Vector3> points) const;

        static Vector3 Centre(std::span<const Vector3> points);

        // a bᵀ in the 3 x 3 corner.
        static Matrix4x4 Outer(Vector3 a, Vector3 b)
        {
            return Matrix4x4(
                a.X * b.X, a.X * b.Y, a.X * b.Z, 0.f,
                a.Y * b.X, a.Y * b.Y, a.Y * b.Z, 0.f,
                a.Z * b.X, a.Z * b.Y, a.Z * b.Z, 0.f,
                0.f, 0.f, 0.f, 0.f);
        }
    };
}
