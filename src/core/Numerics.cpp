#include "Numerics.h"

#include "DotNet.h"

namespace Mdk
{
    // ---- Vector2 ----

    Vector2 Vector2::Min(Vector2 a, Vector2 b) { return {MathF::Min(a.X, b.X), MathF::Min(a.Y, b.Y)}; }
    Vector2 Vector2::Max(Vector2 a, Vector2 b) { return {MathF::Max(a.X, b.X), MathF::Max(a.Y, b.Y)}; }

    Vector2 Vector2::Transform(Vector2 v, const Matrix3x2& m)
    {
        return {v.X * m.M11 + v.Y * m.M21 + m.M31, v.X * m.M12 + v.Y * m.M22 + m.M32};
    }

    Vector2 Vector2::Transform(Vector2 v, const Matrix4x4& m)
    {
        return {v.X * m.M11 + v.Y * m.M21 + m.M41, v.X * m.M12 + v.Y * m.M22 + m.M42};
    }

    // ---- Vector3 ----

    Vector3 Vector3::Min(Vector3 a, Vector3 b)
    {
        return {MathF::Min(a.X, b.X), MathF::Min(a.Y, b.Y), MathF::Min(a.Z, b.Z)};
    }

    Vector3 Vector3::Max(Vector3 a, Vector3 b)
    {
        return {MathF::Max(a.X, b.X), MathF::Max(a.Y, b.Y), MathF::Max(a.Z, b.Z)};
    }

    Vector3 Vector3::Transform(Vector3 v, const Matrix4x4& m)
    {
        return {
            v.X * m.M11 + v.Y * m.M21 + v.Z * m.M31 + m.M41,
            v.X * m.M12 + v.Y * m.M22 + v.Z * m.M32 + m.M42,
            v.X * m.M13 + v.Y * m.M23 + v.Z * m.M33 + m.M43,
        };
    }

    Vector3 Vector3::TransformNormal(Vector3 v, const Matrix4x4& m)
    {
        return {
            v.X * m.M11 + v.Y * m.M21 + v.Z * m.M31,
            v.X * m.M12 + v.Y * m.M22 + v.Z * m.M32,
            v.X * m.M13 + v.Y * m.M23 + v.Z * m.M33,
        };
    }

    Vector3 Vector3::Transform(Vector3 v, const Quaternion& r)
    {
        float x2 = r.X + r.X;
        float y2 = r.Y + r.Y;
        float z2 = r.Z + r.Z;

        float wx2 = r.W * x2;
        float wy2 = r.W * y2;
        float wz2 = r.W * z2;
        float xx2 = r.X * x2;
        float xy2 = r.X * y2;
        float xz2 = r.X * z2;
        float yy2 = r.Y * y2;
        float yz2 = r.Y * z2;
        float zz2 = r.Z * z2;

        return {
            v.X * (1.0f - yy2 - zz2) + v.Y * (xy2 - wz2) + v.Z * (xz2 + wy2),
            v.X * (xy2 + wz2) + v.Y * (1.0f - xx2 - zz2) + v.Z * (yz2 - wx2),
            v.X * (xz2 - wy2) + v.Y * (yz2 + wx2) + v.Z * (1.0f - xx2 - yy2),
        };
    }

    // ---- Vector4 ----

    Vector4 Vector4::Transform(Vector4 v, const Matrix4x4& m)
    {
        return {
            v.X * m.M11 + v.Y * m.M21 + v.Z * m.M31 + v.W * m.M41,
            v.X * m.M12 + v.Y * m.M22 + v.Z * m.M32 + v.W * m.M42,
            v.X * m.M13 + v.Y * m.M23 + v.Z * m.M33 + v.W * m.M43,
            v.X * m.M14 + v.Y * m.M24 + v.Z * m.M34 + v.W * m.M44,
        };
    }

    Vector4 Vector4::Transform(Vector3 v, const Matrix4x4& m) { return Transform(Vector4(v, 1.0f), m); }

    // ---- Quaternion ----

    Quaternion Quaternion::operator*(const Quaternion& v) const
    {
        // Hamilton product as in .NET's Quaternion.Multiply.
        float q1x = X, q1y = Y, q1z = Z, q1w = W;
        float q2x = v.X, q2y = v.Y, q2z = v.Z, q2w = v.W;

        float cx = q1y * q2z - q1z * q2y;
        float cy = q1z * q2x - q1x * q2z;
        float cz = q1x * q2y - q1y * q2x;
        float dot = q1x * q2x + q1y * q2y + q1z * q2z;

        return {q1x * q2w + q2x * q1w + cx, q1y * q2w + q2y * q1w + cy, q1z * q2w + q2z * q1w + cz, q1w * q2w - dot};
    }

    Quaternion Quaternion::Normalize(Quaternion q)
    {
        float inv = 1.0f / q.Length();
        return {q.X * inv, q.Y * inv, q.Z * inv, q.W * inv};
    }

    Quaternion Quaternion::Slerp(Quaternion a, Quaternion b, float t)
    {
        constexpr float SlerpEpsilon = 1e-6f;

        float cosOmega = a.X * b.X + a.Y * b.Y + a.Z * b.Z + a.W * b.W;
        bool flip = false;
        if (cosOmega < 0.0f)
        {
            flip = true;
            cosOmega = -cosOmega;
        }

        float s1;
        float s2;
        if (cosOmega > 1.0f - SlerpEpsilon)
        {
            // Too close: linear.
            s1 = 1.0f - t;
            s2 = flip ? -t : t;
        }
        else
        {
            float omega = std::acos(cosOmega);
            float invSinOmega = 1.0f / std::sin(omega);
            s1 = std::sin((1.0f - t) * omega) * invSinOmega;
            s2 = flip ? -std::sin(t * omega) * invSinOmega : std::sin(t * omega) * invSinOmega;
        }

        return {s1 * a.X + s2 * b.X, s1 * a.Y + s2 * b.Y, s1 * a.Z + s2 * b.Z, s1 * a.W + s2 * b.W};
    }

    Quaternion Quaternion::CreateFromYawPitchRoll(float yaw, float pitch, float roll)
    {
        float sr = std::sin(roll * 0.5f), cr = std::cos(roll * 0.5f);
        float sp = std::sin(pitch * 0.5f), cp = std::cos(pitch * 0.5f);
        float sy = std::sin(yaw * 0.5f), cy = std::cos(yaw * 0.5f);

        return {
            cy * sp * cr + sy * cp * sr,
            sy * cp * cr - cy * sp * sr,
            cy * cp * sr - sy * sp * cr,
            cy * cp * cr + sy * sp * sr,
        };
    }

    Quaternion Quaternion::CreateFromAxisAngle(Vector3 axis, float angle)
    {
        float half = angle * 0.5f;
        float s = std::sin(half);
        return {axis.X * s, axis.Y * s, axis.Z * s, std::cos(half)};
    }

    Quaternion Quaternion::CreateFromRotationMatrix(const Matrix4x4& m)
    {
        float trace = m.M11 + m.M22 + m.M33;
        Quaternion q;
        if (trace > 0.0f)
        {
            float s = std::sqrt(trace + 1.0f);
            q.W = s * 0.5f;
            s = 0.5f / s;
            q.X = (m.M23 - m.M32) * s;
            q.Y = (m.M31 - m.M13) * s;
            q.Z = (m.M12 - m.M21) * s;
            return q;
        }

        if (m.M11 >= m.M22 && m.M11 >= m.M33)
        {
            float s = std::sqrt(1.0f + m.M11 - m.M22 - m.M33);
            float invS = 0.5f / s;
            q.X = 0.5f * s;
            q.Y = (m.M12 + m.M21) * invS;
            q.Z = (m.M13 + m.M31) * invS;
            q.W = (m.M23 - m.M32) * invS;
            return q;
        }

        if (m.M22 > m.M33)
        {
            float s = std::sqrt(1.0f + m.M22 - m.M11 - m.M33);
            float invS = 0.5f / s;
            q.X = (m.M21 + m.M12) * invS;
            q.Y = 0.5f * s;
            q.Z = (m.M32 + m.M23) * invS;
            q.W = (m.M31 - m.M13) * invS;
            return q;
        }

        float s = std::sqrt(1.0f + m.M33 - m.M11 - m.M22);
        float invS = 0.5f / s;
        q.X = (m.M31 + m.M13) * invS;
        q.Y = (m.M32 + m.M23) * invS;
        q.Z = 0.5f * s;
        q.W = (m.M12 - m.M21) * invS;
        return q;
    }

    // ---- Matrix3x2 ----

    Matrix3x2 Matrix3x2::CreateRotation(float radians)
    {
        // .NET snaps exact quarter turns (within 0.001 degrees) to 0/1 values.
        constexpr float RotationEpsilon = 0.001f * 3.14159265358979323846f / 180.0f;
        constexpr float Pi = 3.14159265358979323846f;

        radians = std::remainder(radians, Pi * 2.0f);

        float c;
        float s;
        if (radians > -RotationEpsilon && radians < RotationEpsilon)
        {
            c = 1;
            s = 0;
        }
        else if (radians > Pi / 2 - RotationEpsilon && radians < Pi / 2 + RotationEpsilon)
        {
            c = 0;
            s = 1;
        }
        else if (radians < -Pi + RotationEpsilon || radians > Pi - RotationEpsilon)
        {
            c = -1;
            s = 0;
        }
        else if (radians > -Pi / 2 - RotationEpsilon && radians < -Pi / 2 + RotationEpsilon)
        {
            c = 0;
            s = -1;
        }
        else
        {
            c = std::cos(radians);
            s = std::sin(radians);
        }

        Matrix3x2 m;
        m.M11 = c;
        m.M12 = s;
        m.M21 = -s;
        m.M22 = c;
        return m;
    }

    Matrix3x2 Matrix3x2::operator*(const Matrix3x2& o) const
    {
        Matrix3x2 m;
        m.M11 = M11 * o.M11 + M12 * o.M21;
        m.M12 = M11 * o.M12 + M12 * o.M22;
        m.M21 = M21 * o.M11 + M22 * o.M21;
        m.M22 = M21 * o.M12 + M22 * o.M22;
        m.M31 = M31 * o.M11 + M32 * o.M21 + o.M31;
        m.M32 = M31 * o.M12 + M32 * o.M22 + o.M32;
        return m;
    }

    // ---- Matrix4x4 ----

    Matrix4x4 Matrix4x4::operator*(const Matrix4x4& b) const
    {
        const Matrix4x4& a = *this;
        Matrix4x4 m;
        m.M11 = a.M11 * b.M11 + a.M12 * b.M21 + a.M13 * b.M31 + a.M14 * b.M41;
        m.M12 = a.M11 * b.M12 + a.M12 * b.M22 + a.M13 * b.M32 + a.M14 * b.M42;
        m.M13 = a.M11 * b.M13 + a.M12 * b.M23 + a.M13 * b.M33 + a.M14 * b.M43;
        m.M14 = a.M11 * b.M14 + a.M12 * b.M24 + a.M13 * b.M34 + a.M14 * b.M44;
        m.M21 = a.M21 * b.M11 + a.M22 * b.M21 + a.M23 * b.M31 + a.M24 * b.M41;
        m.M22 = a.M21 * b.M12 + a.M22 * b.M22 + a.M23 * b.M32 + a.M24 * b.M42;
        m.M23 = a.M21 * b.M13 + a.M22 * b.M23 + a.M23 * b.M33 + a.M24 * b.M43;
        m.M24 = a.M21 * b.M14 + a.M22 * b.M24 + a.M23 * b.M34 + a.M24 * b.M44;
        m.M31 = a.M31 * b.M11 + a.M32 * b.M21 + a.M33 * b.M31 + a.M34 * b.M41;
        m.M32 = a.M31 * b.M12 + a.M32 * b.M22 + a.M33 * b.M32 + a.M34 * b.M42;
        m.M33 = a.M31 * b.M13 + a.M32 * b.M23 + a.M33 * b.M33 + a.M34 * b.M43;
        m.M34 = a.M31 * b.M14 + a.M32 * b.M24 + a.M33 * b.M34 + a.M34 * b.M44;
        m.M41 = a.M41 * b.M11 + a.M42 * b.M21 + a.M43 * b.M31 + a.M44 * b.M41;
        m.M42 = a.M41 * b.M12 + a.M42 * b.M22 + a.M43 * b.M32 + a.M44 * b.M42;
        m.M43 = a.M41 * b.M13 + a.M42 * b.M23 + a.M43 * b.M33 + a.M44 * b.M43;
        m.M44 = a.M41 * b.M14 + a.M42 * b.M24 + a.M43 * b.M34 + a.M44 * b.M44;
        return m;
    }

    Matrix4x4 Matrix4x4::operator*(float s) const
    {
        return {M11 * s, M12 * s, M13 * s, M14 * s, M21 * s, M22 * s, M23 * s, M24 * s,
                M31 * s, M32 * s, M33 * s, M34 * s, M41 * s, M42 * s, M43 * s, M44 * s};
    }

    Matrix4x4 Matrix4x4::operator+(const Matrix4x4& o) const
    {
        return {M11 + o.M11, M12 + o.M12, M13 + o.M13, M14 + o.M14, M21 + o.M21, M22 + o.M22, M23 + o.M23, M24 + o.M24,
                M31 + o.M31, M32 + o.M32, M33 + o.M33, M34 + o.M34, M41 + o.M41, M42 + o.M42, M43 + o.M43, M44 + o.M44};
    }

    Matrix4x4 Matrix4x4::CreateTranslation(Vector3 p)
    {
        Matrix4x4 m;
        m.M41 = p.X;
        m.M42 = p.Y;
        m.M43 = p.Z;
        return m;
    }

    Matrix4x4 Matrix4x4::CreateScale(float x, float y, float z)
    {
        Matrix4x4 m;
        m.M11 = x;
        m.M22 = y;
        m.M33 = z;
        return m;
    }

    Matrix4x4 Matrix4x4::CreateRotationX(float radians)
    {
        float c = std::cos(radians);
        float s = std::sin(radians);
        Matrix4x4 m;
        m.M22 = c;
        m.M23 = s;
        m.M32 = -s;
        m.M33 = c;
        return m;
    }

    Matrix4x4 Matrix4x4::CreateRotationY(float radians)
    {
        float c = std::cos(radians);
        float s = std::sin(radians);
        Matrix4x4 m;
        m.M11 = c;
        m.M13 = -s;
        m.M31 = s;
        m.M33 = c;
        return m;
    }

    Matrix4x4 Matrix4x4::CreateRotationZ(float radians)
    {
        float c = std::cos(radians);
        float s = std::sin(radians);
        Matrix4x4 m;
        m.M11 = c;
        m.M12 = s;
        m.M21 = -s;
        m.M22 = c;
        return m;
    }

    Matrix4x4 Matrix4x4::CreateFromAxisAngle(Vector3 axis, float angle)
    {
        float x = axis.X, y = axis.Y, z = axis.Z;
        float sa = std::sin(angle), ca = std::cos(angle);
        float xx = x * x, yy = y * y, zz = z * z;
        float xy = x * y, xz = x * z, yz = y * z;

        Matrix4x4 m;
        m.M11 = xx + ca * (1.0f - xx);
        m.M12 = xy - ca * xy + sa * z;
        m.M13 = xz - ca * xz - sa * y;
        m.M21 = xy - ca * xy - sa * z;
        m.M22 = yy + ca * (1.0f - yy);
        m.M23 = yz - ca * yz + sa * x;
        m.M31 = xz - ca * xz + sa * y;
        m.M32 = yz - ca * yz - sa * x;
        m.M33 = zz + ca * (1.0f - zz);
        return m;
    }

    Matrix4x4 Matrix4x4::CreateFromQuaternion(Quaternion q)
    {
        float xx = q.X * q.X, yy = q.Y * q.Y, zz = q.Z * q.Z;
        float xy = q.X * q.Y, wz = q.Z * q.W, xz = q.Z * q.X;
        float wy = q.Y * q.W, yz = q.Y * q.Z, wx = q.X * q.W;

        Matrix4x4 m;
        m.M11 = 1.0f - 2.0f * (yy + zz);
        m.M12 = 2.0f * (xy + wz);
        m.M13 = 2.0f * (xz - wy);
        m.M21 = 2.0f * (xy - wz);
        m.M22 = 1.0f - 2.0f * (zz + xx);
        m.M23 = 2.0f * (yz + wx);
        m.M31 = 2.0f * (xz + wy);
        m.M32 = 2.0f * (yz - wx);
        m.M33 = 1.0f - 2.0f * (yy + xx);
        return m;
    }

    Matrix4x4 Matrix4x4::CreateFromYawPitchRoll(float yaw, float pitch, float roll)
    {
        return CreateFromQuaternion(Quaternion::CreateFromYawPitchRoll(yaw, pitch, roll));
    }

    Matrix4x4 Matrix4x4::CreateLookAt(Vector3 position, Vector3 target, Vector3 up)
    {
        Vector3 zaxis = Vector3::Normalize(position - target);
        Vector3 xaxis = Vector3::Normalize(Vector3::Cross(up, zaxis));
        Vector3 yaxis = Vector3::Cross(zaxis, xaxis);

        Matrix4x4 m;
        m.M11 = xaxis.X; m.M12 = yaxis.X; m.M13 = zaxis.X; m.M14 = 0;
        m.M21 = xaxis.Y; m.M22 = yaxis.Y; m.M23 = zaxis.Y; m.M24 = 0;
        m.M31 = xaxis.Z; m.M32 = yaxis.Z; m.M33 = zaxis.Z; m.M34 = 0;
        m.M41 = -Vector3::Dot(xaxis, position);
        m.M42 = -Vector3::Dot(yaxis, position);
        m.M43 = -Vector3::Dot(zaxis, position);
        m.M44 = 1;
        return m;
    }

    Matrix4x4 Matrix4x4::CreatePerspectiveFieldOfView(float fov, float aspect, float nearPlane, float farPlane)
    {
        float yScale = 1.0f / std::tan(fov * 0.5f);
        float xScale = yScale / aspect;
        float negFarRange = std::isinf(farPlane) && farPlane > 0 ? -1.0f : farPlane / (nearPlane - farPlane);

        Matrix4x4 m(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        m.M11 = xScale;
        m.M22 = yScale;
        m.M33 = negFarRange;
        m.M34 = -1.0f;
        m.M43 = nearPlane * negFarRange;
        return m;
    }

    Matrix4x4 Matrix4x4::CreatePerspectiveOffCenter(float left, float right, float bottom, float top, float nearPlane, float farPlane)
    {
        float negFarRange = std::isinf(farPlane) && farPlane > 0 ? -1.0f : farPlane / (nearPlane - farPlane);

        Matrix4x4 m(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        m.M11 = 2.0f * nearPlane / (right - left);
        m.M22 = 2.0f * nearPlane / (top - bottom);
        m.M31 = (left + right) / (right - left);
        m.M32 = (top + bottom) / (top - bottom);
        m.M33 = negFarRange;
        m.M34 = -1.0f;
        m.M43 = nearPlane * negFarRange;
        return m;
    }

    Matrix4x4 Matrix4x4::CreateOrthographic(float width, float height, float nearPlane, float farPlane)
    {
        Matrix4x4 m;
        m.M11 = 2.0f / width;
        m.M22 = 2.0f / height;
        m.M33 = 1.0f / (nearPlane - farPlane);
        m.M43 = nearPlane / (nearPlane - farPlane);
        return m;
    }

    Matrix4x4 Matrix4x4::CreateOrthographicOffCenter(float left, float right, float bottom, float top, float nearPlane, float farPlane)
    {
        Matrix4x4 m;
        m.M11 = 2.0f / (right - left);
        m.M22 = 2.0f / (top - bottom);
        m.M33 = 1.0f / (nearPlane - farPlane);
        m.M41 = (left + right) / (left - right);
        m.M42 = (top + bottom) / (bottom - top);
        m.M43 = nearPlane / (nearPlane - farPlane);
        return m;
    }

    bool Matrix4x4::Invert(const Matrix4x4& matrix, Matrix4x4& result)
    {
        // .NET's software fallback (cofactors).
        float a = matrix.M11, b = matrix.M12, c = matrix.M13, d = matrix.M14;
        float e = matrix.M21, f = matrix.M22, g = matrix.M23, h = matrix.M24;
        float i = matrix.M31, j = matrix.M32, k = matrix.M33, l = matrix.M34;
        float m = matrix.M41, n = matrix.M42, o = matrix.M43, p = matrix.M44;

        float kp_lo = k * p - l * o;
        float jp_ln = j * p - l * n;
        float jo_kn = j * o - k * n;
        float ip_lm = i * p - l * m;
        float io_km = i * o - k * m;
        float in_jm = i * n - j * m;

        float a11 = +(f * kp_lo - g * jp_ln + h * jo_kn);
        float a12 = -(e * kp_lo - g * ip_lm + h * io_km);
        float a13 = +(e * jp_ln - f * ip_lm + h * in_jm);
        float a14 = -(e * jo_kn - f * io_km + g * in_jm);

        float det = a * a11 + b * a12 + c * a13 + d * a14;

        if (std::fabs(det) < std::numeric_limits<float>::epsilon())
        {
            float nan = std::numeric_limits<float>::quiet_NaN();
            result = Matrix4x4(nan, nan, nan, nan, nan, nan, nan, nan, nan, nan, nan, nan, nan, nan, nan, nan);
            return false;
        }

        float invDet = 1.0f / det;

        result.M11 = a11 * invDet;
        result.M21 = a12 * invDet;
        result.M31 = a13 * invDet;
        result.M41 = a14 * invDet;

        result.M12 = -(b * kp_lo - c * jp_ln + d * jo_kn) * invDet;
        result.M22 = +(a * kp_lo - c * ip_lm + d * io_km) * invDet;
        result.M32 = -(a * jp_ln - b * ip_lm + d * in_jm) * invDet;
        result.M42 = +(a * jo_kn - b * io_km + c * in_jm) * invDet;

        float gp_ho = g * p - h * o;
        float fp_hn = f * p - h * n;
        float fo_gn = f * o - g * n;
        float ep_hm = e * p - h * m;
        float eo_gm = e * o - g * m;
        float en_fm = e * n - f * m;

        result.M13 = +(b * gp_ho - c * fp_hn + d * fo_gn) * invDet;
        result.M23 = -(a * gp_ho - c * ep_hm + d * eo_gm) * invDet;
        result.M33 = +(a * fp_hn - b * ep_hm + d * en_fm) * invDet;
        result.M43 = -(a * fo_gn - b * eo_gm + c * en_fm) * invDet;

        float gl_hk = g * l - h * k;
        float fl_hj = f * l - h * j;
        float fk_gj = f * k - g * j;
        float el_hi = e * l - h * i;
        float ek_gi = e * k - g * i;
        float ej_fi = e * j - f * i;

        result.M14 = -(b * gl_hk - c * fl_hj + d * fk_gj) * invDet;
        result.M24 = +(a * gl_hk - c * el_hi + d * ek_gi) * invDet;
        result.M34 = -(a * fl_hj - b * el_hi + d * ej_fi) * invDet;
        result.M44 = +(a * fk_gj - b * ek_gi + c * ej_fi) * invDet;

        return true;
    }

    Matrix4x4 Matrix4x4::Transpose(const Matrix4x4& m)
    {
        return {m.M11, m.M21, m.M31, m.M41, m.M12, m.M22, m.M32, m.M42,
                m.M13, m.M23, m.M33, m.M43, m.M14, m.M24, m.M34, m.M44};
    }

    Matrix4x4 Matrix4x4::Lerp(const Matrix4x4& a, const Matrix4x4& b, float t)
    {
        return a + (Matrix4x4(b.M11 - a.M11, b.M12 - a.M12, b.M13 - a.M13, b.M14 - a.M14,
                              b.M21 - a.M21, b.M22 - a.M22, b.M23 - a.M23, b.M24 - a.M24,
                              b.M31 - a.M31, b.M32 - a.M32, b.M33 - a.M33, b.M34 - a.M34,
                              b.M41 - a.M41, b.M42 - a.M42, b.M43 - a.M43, b.M44 - a.M44) * t);
    }
}
