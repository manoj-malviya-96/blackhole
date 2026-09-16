#pragma once
#include <QVector3D>
#include <QVector4D>

namespace physics {
constexpr double kSpeedOfLight = 299792458.0;
constexpr double kGravitationalConstant = 6.67430e-11;
constexpr double kSolarMass = 1.98847e30;

inline double schwarzschildRadius(double massKg) {
    return 2.0 * kGravitationalConstant * massKg / (kSpeedOfLight * kSpeedOfLight);
}
}

struct SceneObject {
    QVector4D posRadius; // xyz position (meters), w = radius (meters)
    QVector4D color;     // rgb, a
    double mass;          // kilograms
    QVector3D velocity;  // not used yet
};
