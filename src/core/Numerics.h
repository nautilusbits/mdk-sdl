#pragma once

// System.Numerics replacement: same layout, same row-vector convention, same formulas as .NET's
// software paths, so ported maths gives the C# version's results.
//
//   v' = v * M        (Vector3::Transform(v, m): x*M11 + y*M21 + z*M31 + M41)
//   M  = A * B        (apply A, then B)

#include <cmath>
#include <limits>

namespace Mdk
{
    struct Vector2;
    struct Vector3;
    struct Vector4;
    struct Matrix4x4;
    struct Matrix3x2;
    struct Quaternion;

    struct Vector2
    {
        float X = 0;
        float Y = 0;

        constexpr Vector2() = default;
        constexpr Vector2(float x, float y) : X(x), Y(y) {}
        constexpr explicit Vector2(float v) : X(v), Y(v) {}

        static constexpr Vector2 Zero() { return {0, 0}; }
        static constexpr Vector2 One() { return {1, 1}; }
        static constexpr Vector2 UnitX() { return {1, 0}; }
        static constexpr Vector2 UnitY() { return {0, 1}; }

        float Length() const { return std::sqrt(LengthSquared()); }
        constexpr float LengthSquared() const { return X * X + Y * Y; }

        constexpr Vector2 operator-() const { return {-X, -Y}; }
        constexpr Vector2 operator+(Vector2 o) const { return {X + o.X, Y + o.Y}; }
        constexpr Vector2 operator-(Vector2 o) const { return {X - o.X, Y - o.Y}; }
        constexpr Vector2 operator*(Vector2 o) const { return {X * o.X, Y * o.Y}; }
        constexpr Vector2 operator/(Vector2 o) const { return {X / o.X, Y / o.Y}; }
        constexpr Vector2 operator*(float s) const { return {X * s, Y * s}; }
        constexpr Vector2 operator/(float s) const { return {X / s, Y / s}; }
        friend constexpr Vector2 operator*(float s, Vector2 v) { return {v.X * s, v.Y * s}; }
        Vector2& operator+=(Vector2 o) { X += o.X; Y += o.Y; return *this; }
        Vector2& operator-=(Vector2 o) { X -= o.X; Y -= o.Y; return *this; }
        Vector2& operator*=(float s) { X *= s; Y *= s; return *this; }
        Vector2& operator*=(Vector2 o) { X *= o.X; Y *= o.Y; return *this; }
        Vector2& operator/=(float s) { X /= s; Y /= s; return *this; }
        constexpr bool operator==(const Vector2&) const = default;

        static constexpr float Dot(Vector2 a, Vector2 b) { return a.X * b.X + a.Y * b.Y; }
        static Vector2 Normalize(Vector2 v) { return v / v.Length(); }
        static float Distance(Vector2 a, Vector2 b) { return (a - b).Length(); }
        static constexpr float DistanceSquared(Vector2 a, Vector2 b) { return (a - b).LengthSquared(); }
        static constexpr Vector2 Lerp(Vector2 a, Vector2 b, float t) { return a * (1.0f - t) + b * t; }
        static Vector2 Min(Vector2 a, Vector2 b);
        static Vector2 Max(Vector2 a, Vector2 b);
        static Vector2 Abs(Vector2 v) { return {std::fabs(v.X), std::fabs(v.Y)}; }
        static Vector2 Clamp(Vector2 v, Vector2 min, Vector2 max) { return Min(Max(v, min), max); }
        static Vector2 Round(Vector2 v) { return {std::nearbyint(v.X), std::nearbyint(v.Y)}; }
        static Vector2 Transform(Vector2 v, const Matrix3x2& m);
        static Vector2 Transform(Vector2 v, const Matrix4x4& m);
    };

    struct Vector3
    {
        float X = 0;
        float Y = 0;
        float Z = 0;

        constexpr Vector3() = default;
        constexpr Vector3(float x, float y, float z) : X(x), Y(y), Z(z) {}
        constexpr Vector3(Vector2 xy, float z) : X(xy.X), Y(xy.Y), Z(z) {}
        constexpr explicit Vector3(float v) : X(v), Y(v), Z(v) {}

        static constexpr Vector3 Zero() { return {0, 0, 0}; }
        static constexpr Vector3 One() { return {1, 1, 1}; }
        static constexpr Vector3 UnitX() { return {1, 0, 0}; }
        static constexpr Vector3 UnitY() { return {0, 1, 0}; }
        static constexpr Vector3 UnitZ() { return {0, 0, 1}; }

        float Length() const { return std::sqrt(LengthSquared()); }
        constexpr float LengthSquared() const { return X * X + Y * Y + Z * Z; }

        constexpr Vector3 operator-() const { return {-X, -Y, -Z}; }
        constexpr Vector3 operator+(Vector3 o) const { return {X + o.X, Y + o.Y, Z + o.Z}; }
        constexpr Vector3 operator-(Vector3 o) const { return {X - o.X, Y - o.Y, Z - o.Z}; }
        constexpr Vector3 operator*(Vector3 o) const { return {X * o.X, Y * o.Y, Z * o.Z}; }
        constexpr Vector3 operator/(Vector3 o) const { return {X / o.X, Y / o.Y, Z / o.Z}; }
        constexpr Vector3 operator*(float s) const { return {X * s, Y * s, Z * s}; }
        constexpr Vector3 operator/(float s) const { return {X / s, Y / s, Z / s}; }
        friend constexpr Vector3 operator*(float s, Vector3 v) { return {v.X * s, v.Y * s, v.Z * s}; }
        Vector3& operator+=(Vector3 o) { X += o.X; Y += o.Y; Z += o.Z; return *this; }
        Vector3& operator-=(Vector3 o) { X -= o.X; Y -= o.Y; Z -= o.Z; return *this; }
        Vector3& operator*=(float s) { X *= s; Y *= s; Z *= s; return *this; }
        Vector3& operator*=(Vector3 o) { X *= o.X; Y *= o.Y; Z *= o.Z; return *this; }
        Vector3& operator/=(float s) { X /= s; Y /= s; Z /= s; return *this; }
        constexpr bool operator==(const Vector3&) const = default;

        static constexpr float Dot(Vector3 a, Vector3 b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
        static constexpr Vector3 Cross(Vector3 a, Vector3 b)
        {
            return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
        }
        static Vector3 Normalize(Vector3 v) { return v / v.Length(); }
        static float Distance(Vector3 a, Vector3 b) { return (a - b).Length(); }
        static constexpr float DistanceSquared(Vector3 a, Vector3 b) { return (a - b).LengthSquared(); }
        static constexpr Vector3 Lerp(Vector3 a, Vector3 b, float t) { return a * (1.0f - t) + b * t; }
        static Vector3 Min(Vector3 a, Vector3 b);
        static Vector3 Max(Vector3 a, Vector3 b);
        static Vector3 Abs(Vector3 v) { return {std::fabs(v.X), std::fabs(v.Y), std::fabs(v.Z)}; }
        static Vector3 Clamp(Vector3 v, Vector3 min, Vector3 max) { return Min(Max(v, min), max); }
        static Vector3 Transform(Vector3 v, const Matrix4x4& m);
        static Vector3 Transform(Vector3 v, const Quaternion& q);
        static Vector3 TransformNormal(Vector3 v, const Matrix4x4& m);
        static Vector3 Reflect(Vector3 v, Vector3 n) { return v - n * (2.0f * Dot(v, n)); }

        // C#'s new Vector2(v.X, v.Y) shortcut.
        constexpr Vector2 XY() const { return {X, Y}; }
    };

    struct Vector4
    {
        float X = 0;
        float Y = 0;
        float Z = 0;
        float W = 0;

        constexpr Vector4() = default;
        constexpr Vector4(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}
        constexpr Vector4(Vector3 xyz, float w) : X(xyz.X), Y(xyz.Y), Z(xyz.Z), W(w) {}
        constexpr Vector4(Vector2 xy, float z, float w) : X(xy.X), Y(xy.Y), Z(z), W(w) {}
        constexpr explicit Vector4(float v) : X(v), Y(v), Z(v), W(v) {}

        static constexpr Vector4 Zero() { return {0, 0, 0, 0}; }
        static constexpr Vector4 One() { return {1, 1, 1, 1}; }
        static constexpr Vector4 UnitW() { return {0, 0, 0, 1}; }

        float Length() const { return std::sqrt(LengthSquared()); }
        constexpr float LengthSquared() const { return X * X + Y * Y + Z * Z + W * W; }

        constexpr Vector4 operator-() const { return {-X, -Y, -Z, -W}; }
        constexpr Vector4 operator+(Vector4 o) const { return {X + o.X, Y + o.Y, Z + o.Z, W + o.W}; }
        constexpr Vector4 operator-(Vector4 o) const { return {X - o.X, Y - o.Y, Z - o.Z, W - o.W}; }
        constexpr Vector4 operator*(Vector4 o) const { return {X * o.X, Y * o.Y, Z * o.Z, W * o.W}; }
        constexpr Vector4 operator*(float s) const { return {X * s, Y * s, Z * s, W * s}; }
        constexpr Vector4 operator/(float s) const { return {X / s, Y / s, Z / s, W / s}; }
        friend constexpr Vector4 operator*(float s, Vector4 v) { return v * s; }
        Vector4& operator+=(Vector4 o) { X += o.X; Y += o.Y; Z += o.Z; W += o.W; return *this; }
        Vector4& operator*=(float s) { X *= s; Y *= s; Z *= s; W *= s; return *this; }
        constexpr bool operator==(const Vector4&) const = default;

        static constexpr float Dot(Vector4 a, Vector4 b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W; }
        static constexpr Vector4 Lerp(Vector4 a, Vector4 b, float t) { return a * (1.0f - t) + b * t; }
        static Vector4 Transform(Vector4 v, const Matrix4x4& m);
        static Vector4 Transform(Vector3 v, const Matrix4x4& m);

        constexpr Vector3 XYZ() const { return {X, Y, Z}; }
    };

    struct Quaternion
    {
        float X = 0;
        float Y = 0;
        float Z = 0;
        float W = 0;

        constexpr Quaternion() = default;
        constexpr Quaternion(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}
        constexpr Quaternion(Vector3 v, float w) : X(v.X), Y(v.Y), Z(v.Z), W(w) {}

        static constexpr Quaternion Identity() { return {0, 0, 0, 1}; }
        constexpr bool operator==(const Quaternion&) const = default;

        float Length() const { return std::sqrt(X * X + Y * Y + Z * Z + W * W); }
        Quaternion operator*(const Quaternion& o) const;

        static Quaternion Normalize(Quaternion q);
        static Quaternion Slerp(Quaternion a, Quaternion b, float t);
        static Quaternion CreateFromYawPitchRoll(float yaw, float pitch, float roll);
        static Quaternion CreateFromAxisAngle(Vector3 axis, float angle);
        static Quaternion CreateFromRotationMatrix(const Matrix4x4& m);
        static Quaternion Conjugate(Quaternion q) { return {-q.X, -q.Y, -q.Z, q.W}; }
    };

    struct Matrix3x2
    {
        float M11 = 1, M12 = 0;
        float M21 = 0, M22 = 1;
        float M31 = 0, M32 = 0;

        static constexpr Matrix3x2 Identity() { return {}; }
        static Matrix3x2 CreateRotation(float radians);
        static Matrix3x2 CreateTranslation(Vector2 p) { Matrix3x2 m; m.M31 = p.X; m.M32 = p.Y; return m; }
        static Matrix3x2 CreateScale(Vector2 s) { Matrix3x2 m; m.M11 = s.X; m.M22 = s.Y; return m; }
        Matrix3x2 operator*(const Matrix3x2& o) const;
        constexpr bool operator==(const Matrix3x2&) const = default;
    };

    struct Matrix4x4
    {
        float M11 = 1, M12 = 0, M13 = 0, M14 = 0;
        float M21 = 0, M22 = 1, M23 = 0, M24 = 0;
        float M31 = 0, M32 = 0, M33 = 1, M34 = 0;
        float M41 = 0, M42 = 0, M43 = 0, M44 = 1;

        constexpr Matrix4x4() = default;
        constexpr Matrix4x4(float m11, float m12, float m13, float m14,
                            float m21, float m22, float m23, float m24,
                            float m31, float m32, float m33, float m34,
                            float m41, float m42, float m43, float m44)
            : M11(m11), M12(m12), M13(m13), M14(m14),
              M21(m21), M22(m22), M23(m23), M24(m24),
              M31(m31), M32(m32), M33(m33), M34(m34),
              M41(m41), M42(m42), M43(m43), M44(m44) {}

        static constexpr Matrix4x4 Identity() { return {}; }
        constexpr bool operator==(const Matrix4x4&) const = default;

        Vector3 Translation() const { return {M41, M42, M43}; }
        void SetTranslation(Vector3 t) { M41 = t.X; M42 = t.Y; M43 = t.Z; }
        bool IsIdentity() const { return *this == Identity(); }

        Matrix4x4 operator*(const Matrix4x4& o) const;
        Matrix4x4 operator*(float s) const;
        Matrix4x4 operator+(const Matrix4x4& o) const;

        static Matrix4x4 CreateTranslation(Vector3 p);
        static Matrix4x4 CreateTranslation(float x, float y, float z) { return CreateTranslation({x, y, z}); }
        static Matrix4x4 CreateScale(float s) { return CreateScale(s, s, s); }
        static Matrix4x4 CreateScale(Vector3 s) { return CreateScale(s.X, s.Y, s.Z); }
        static Matrix4x4 CreateScale(float x, float y, float z);
        static Matrix4x4 CreateRotationX(float radians);
        static Matrix4x4 CreateRotationY(float radians);
        static Matrix4x4 CreateRotationZ(float radians);
        static Matrix4x4 CreateFromAxisAngle(Vector3 axis, float angle);
        static Matrix4x4 CreateFromQuaternion(Quaternion q);
        static Matrix4x4 CreateFromYawPitchRoll(float yaw, float pitch, float roll);
        static Matrix4x4 CreateLookAt(Vector3 position, Vector3 target, Vector3 up);
        static Matrix4x4 CreatePerspectiveFieldOfView(float fov, float aspect, float nearPlane, float farPlane);
        static Matrix4x4 CreatePerspectiveOffCenter(float left, float right, float bottom, float top, float nearPlane, float farPlane);
        static Matrix4x4 CreateOrthographic(float width, float height, float nearPlane, float farPlane);
        static Matrix4x4 CreateOrthographicOffCenter(float left, float right, float bottom, float top, float nearPlane, float farPlane);
        static bool Invert(const Matrix4x4& m, Matrix4x4& result);
        static Matrix4x4 Transpose(const Matrix4x4& m);
        static Matrix4x4 Lerp(const Matrix4x4& a, const Matrix4x4& b, float t);
    };
}
