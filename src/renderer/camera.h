#pragma once
#include <QMatrix4x4>
#include <QVector3D>
#include <QPointF>
#include <algorithm>
#include <cmath>

namespace renderer {

// Orbit camera: m_azimuth/m_elevation around a fixed m_target, m_radius-based zoom.
class Camera {
public:
    QVector3D position() const {
        const float el = std::clamp(m_elevation, kMinElevation, kMaxElevation);
        return {
            m_radius * std::sin(el) * std::cos(m_azimuth),
            m_radius * std::cos(el),
            m_radius * std::sin(el) * std::sin(m_azimuth)
        };
    }

    QMatrix4x4 viewMatrix() const {
        QMatrix4x4 view;
        view.lookAt(position(), m_target, QVector3D(0, 1, 0));
        return view;
    }

    QMatrix4x4 projMatrix(float aspect) const {
        QMatrix4x4 proj;
        proj.perspective(60.0f, aspect, 1e9f, 1e14f);
        return proj;
    }

    void beginDrag(const QPointF& pos) {
        m_dragging = true;
        m_lastPos = pos;
    }

    void drag(const QPointF& pos) {
        if (!m_dragging) return;
        const QPointF d = pos - m_lastPos;
        m_azimuth += float(d.x()) * m_orbitSpeed;
        m_elevation -= float(d.y()) * m_orbitSpeed;
        m_elevation = std::clamp(m_elevation, kMinElevation, kMaxElevation);
        m_lastPos = pos;
        m_moving = true;
    }

    void endDrag() {
        m_dragging = false;
        m_moving = false;
    }

    void zoom(float steps) {
        m_radius -= steps * float(m_zoomSpeed);
        m_radius = std::clamp(m_radius, m_minRadius, m_maxRadius);
        m_moving = true;
    }

    void reset() {
        m_radius = 2.2e11f;
        m_azimuth = 0.0f;
        m_elevation = 1.0f;
    }

    QVector3D m_target{0.f, 0.f, 0.f};
    float m_radius = 2.2e11f;
    float m_minRadius = 1e10f;
    float m_maxRadius = 1e12f;
    float m_azimuth = 0.0f;
    float m_elevation = 1.0f; // tilted view - edge-on (~pi/2) puts the camera in the disk plane, hiding it
    float m_orbitSpeed = 0.01f;
    double m_zoomSpeed = 2.5e10;

    bool m_dragging = false;
    bool m_moving = false;

private:
    static constexpr float kMinElevation = 0.01f;
    static constexpr float kMaxElevation = 3.1315926535f;
    QPointF m_lastPos{};
};

} // namespace renderer
