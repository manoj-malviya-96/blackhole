#pragma once
#include <QMatrix4x4>
#include <QVector3D>
#include <QPointF>
#include <algorithm>
#include <cmath>

// Orbit camera: azimuth/elevation around a fixed target, radius-based zoom.
class Camera {
public:
    QVector3D position() const {
        const float el = std::clamp(elevation, kMinElevation, kMaxElevation);
        return {
            radius * std::sin(el) * std::cos(azimuth),
            radius * std::cos(el),
            radius * std::sin(el) * std::sin(azimuth)
        };
    }

    QMatrix4x4 viewMatrix() const {
        QMatrix4x4 view;
        view.lookAt(position(), target, QVector3D(0, 1, 0));
        return view;
    }

    QMatrix4x4 projMatrix(float aspect) const {
        QMatrix4x4 proj;
        proj.perspective(60.0f, aspect, 1e9f, 1e14f);
        return proj;
    }

    void beginDrag(const QPointF& pos) {
        dragging = true;
        lastPos = pos;
    }

    void drag(const QPointF& pos) {
        if (!dragging) return;
        const QPointF d = pos - lastPos;
        azimuth += float(d.x()) * orbitSpeed;
        elevation -= float(d.y()) * orbitSpeed;
        elevation = std::clamp(elevation, kMinElevation, kMaxElevation);
        lastPos = pos;
        moving = true;
    }

    void endDrag() {
        dragging = false;
        moving = false;
    }

    void zoom(float steps) {
        radius -= steps * float(zoomSpeed);
        radius = std::clamp(radius, minRadius, maxRadius);
        moving = true;
    }

    void reset() {
        radius = 6.34e10f;
        azimuth = 0.0f;
        elevation = 1.5707963f;
    }

    QVector3D target{0.f, 0.f, 0.f};
    float radius = 6.34e10f;
    float minRadius = 1e10f;
    float maxRadius = 1e12f;
    float azimuth = 0.0f;
    float elevation = 1.57079632679f; // ~pi/2
    float orbitSpeed = 0.01f;
    double zoomSpeed = 2.5e10;

    bool dragging = false;
    bool moving = false;

private:
    static constexpr float kMinElevation = 0.01f;
    static constexpr float kMaxElevation = 3.1315926535f;
    QPointF lastPos{};
};
