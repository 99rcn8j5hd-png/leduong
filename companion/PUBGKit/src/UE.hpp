// UE.hpp - kiểu dữ liệu UE4 + math, không phụ thuộc gì bên ngoài
#pragma once
#include <math.h>
#include <stdint.h>

#define ZK_PI 3.14159265358979323846f
static inline float zkDeg2Rad(float d) { return d * (ZK_PI / 180.0f); }
static inline float zkRad2Deg(float r) { return r * (180.0f / ZK_PI); }

struct FVector {
    float X = 0, Y = 0, Z = 0;
    FVector() = default;
    FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}
    FVector operator-(const FVector& o) const { return FVector(X - o.X, Y - o.Y, Z - o.Z); }
    FVector operator+(const FVector& o) const { return FVector(X + o.X, Y + o.Y, Z + o.Z); }
    FVector operator*(float s) const { return FVector(X * s, Y * s, Z * s); }
    float Dot(const FVector& o) const { return X * o.X + Y * o.Y + Z * o.Z; }
    float Size2D() const { return sqrtf(X * X + Y * Y); }
    float Size() const { return sqrtf(X * X + Y * Y + Z * Z); }
    bool Finite() const { return isfinite(X) && isfinite(Y) && isfinite(Z); }
    bool NearZero() const { return fabsf(X) < 0.01f && fabsf(Y) < 0.01f && fabsf(Z) < 0.01f; }
};

struct FVector2D {
    float X = 0, Y = 0;
};

struct FRotator {
    float Pitch = 0, Yaw = 0, Roll = 0;
};

struct FMinimalViewInfo {
    FVector   Location;
    FRotator  Rotation;
    float     FOV = 90.0f;
};

// Chuẩn hoá góc về (-180, 180]
static inline float zkNormYaw(float y) {
    if (!isfinite(y)) return 0;
    y=fmodf(y,360.0f);
    if (y > 180.0f) y-=360.0f;
    if (y <= -180.0f) y+=360.0f;
    return y;
}

// World -> Screen. FOV của UE là FOV ngang.
static inline bool worldToScreen(const FVector& w, const FMinimalViewInfo& cam,
                                 float sw, float sh, FVector2D& out) {
    if (!w.Finite() || !cam.Location.Finite() || !isfinite(sw) || !isfinite(sh) || sw<=0 || sh<=0 ||
        !isfinite(cam.Rotation.Pitch) || !isfinite(cam.Rotation.Yaw) || !isfinite(cam.Rotation.Roll)) return false;
    const float p = zkDeg2Rad(cam.Rotation.Pitch);
    const float y = zkDeg2Rad(cam.Rotation.Yaw);
    const float cp = cosf(p), sp = sinf(p);
    const float cy = cosf(y), sy = sinf(y);

    FVector fwd(cp * cy, cp * sy, sp);
    FVector right(-sy, cy, 0.0f);
    FVector up = FVector(fwd.Y * right.Z - fwd.Z * right.Y,
                         fwd.Z * right.X - fwd.X * right.Z,
                         fwd.X * right.Y - fwd.Y * right.X);

    FVector d = w - cam.Location;
    const float zf = d.Dot(fwd);
    if (zf < 1.0f) return false;                 // sau lưng / quá gần

    const float fov = (cam.FOV > 5.0f && cam.FOV < 170.0f) ? cam.FOV : 90.0f;
    const float f = (sw * 0.5f) / tanf(zkDeg2Rad(fov) * 0.5f);
    const float xf = d.Dot(right);
    const float yf = d.Dot(up);

    out.X = sw * 0.5f + (xf * f) / zf;
    out.Y = sh * 0.5f - (yf * f) / zf;
    return isfinite(out.X) && isfinite(out.Y);
}

// Góc cần xoay để nhìn từ `from` sang `to` (độ, hệ UE)
static inline FRotator zkCalcAngle(const FVector& from, const FVector& to) {
    FVector d = to - from;
    FRotator r;
    r.Pitch = zkRad2Deg(atan2f(d.Z, d.Size2D()));
    r.Yaw   = zkRad2Deg(atan2f(d.Y, d.X));
    r.Roll  = 0.0f;
    return r;
}

// Delta góc a -> b (đã chuẩn hoá)
static inline FRotator zkAngleDelta(const FRotator& a, const FRotator& b) {
    FRotator d;
    d.Pitch = b.Pitch - a.Pitch;
    d.Yaw   = zkNormYaw(b.Yaw - a.Yaw);
    d.Roll  = 0.0f;
    return d;
}

// Bù đạn rơi: trả về điểm ngắm đã bù (đơn vị UE ~ cm)
static inline FVector zkPredict(const FVector& camLoc, const FVector& target,
                                float speed, uint32_t pingMs) {
    if (speed <= 1.0f) return target;
    const float dist = (target - camLoc).Size();
    const float t = dist / speed + (pingMs / 1000.0f) * 0.5f;
    FVector p = target;
    p.Z += 0.5f * 980.0f * t * t;   // gravity UE mặc định 980
    return p;
}
