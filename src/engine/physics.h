#pragma once
#include <QVector3D>
#include <QVector4D>
#include <vector>

namespace engine {

namespace physics {
constexpr double kSpeedOfLight = 299792458.0;
constexpr double kGravitationalConstant = 6.67430e-11;
constexpr double kSolarMass = 1.98847e30;

inline double schwarzschildRadius(double massKg) {
    return 2.0 * kGravitationalConstant * massKg / (kSpeedOfLight * kSpeedOfLight);
}
} // namespace physics

struct SceneObject {
    QVector4D posRadius; // xyz position (meters), w = radius (meters)
    QVector4D color;     // rgb, a
    double mass;         // kilograms
    QVector3D velocity;  // m/s
    double spin = 0.0;   // dimensionless Kerr parameter a/M, [-1, 1]
};

struct DiskParams {
    float r1 = 0.0f;
    float r2 = 0.0f;
    float spin = 0.0f;
};

struct GridMesh {
    std::vector<QVector3D> vertices;
    std::vector<unsigned int> indices;
};

} // namespace engine
