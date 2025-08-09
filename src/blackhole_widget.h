#pragma once

#include <QMatrix4x4>
#include <QOpenGLFunctions_4_1_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QTimer>
#include <QVector>
#include <vector>

class BlackHoleWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_1_Core {
    Q_OBJECT
public:
    explicit BlackHoleWidget(QWidget* parent = nullptr);
    ~BlackHoleWidget() override;

    [[nodiscard]] QSize minimumSizeHint() const override { return {640, 480}; }
    [[nodiscard]] QSize sizeHint() const override { return {960, 720}; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // Input
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    // Scene data
    struct ObjectData {
        QVector4D posRadius; // xyz position (meters), w = radius (meters)
        QVector4D color;     // rgb,a
        double mass;         // kilograms
        QVector3D velocity;  // not used here
    };

    // Camera
    struct Camera {
        QVector3D target{0.f, 0.f, 0.f};
        float radius = 6.34e10f;
        float minRadius = 1e10f;
        float maxRadius = 1e12f;
        float azimuth = 0.0f;
        float elevation = 1.57079632679f; // ~pi/2
        float orbitSpeed = 0.01f;
        double zoomSpeed = 2.5e10;

        bool dragging = false;
        QPointF lastPos{};
        bool moving = false;

        QVector3D position() const {
            const float el = std::clamp(elevation, 0.01f, 3.1315926535f);
            return {
                radius * std::sin(el) * std::cos(azimuth),
                radius * std::cos(el),
                radius * std::sin(el) * std::sin(azimuth)
            };
        }
    };

    // Math/physics constants
    static constexpr double C_ = 299792458.0;
    static constexpr double G_ = 6.67430e-11;

    // GL helpers
    void createQuad();
    void destroyGL();
    void ensureOutputTex(int w, int h);
    void rebuildGrid(); // CPU grid generation with Schwarzschild-like warp
    void uploadCameraUBO();
    void uploadDiskUBO();
    void uploadObjectsUBO(); // reserved (not used by shader yet)
    void dispatchCompute();
    void drawGrid();
    void drawFullscreenQuad();         // uses computed texture
    void drawFullscreenLensFallback(); // fragment shader fallback

    // Projection helper
    void updateCameraMatrices();
    QPointF projectToScreen(const QVector3D& p, bool& clipped) const;

    // Programs
    QOpenGLShaderProgram m_gridProg;
    QOpenGLShaderProgram m_quadProg;
    QOpenGLShaderProgram m_computeProg;
    QOpenGLShaderProgram m_lensProg; // fallback fragment shader

    // UBOs
    GLuint m_cameraUBO = 0;
    GLuint m_diskUBO = 0;
    GLuint m_objectsUBO = 0;

    // Fullscreen quad + texture
    GLuint m_quadVAO = 0;
    GLuint m_quadVBO = 0;
    GLuint m_quadEBO = 0;
    GLuint m_outputTex = 0;

    // Grid
    GLuint m_gridVAO = 0;
    GLuint m_gridVBO = 0;
    GLuint m_gridEBO = 0;
    int m_gridIndexCount = 0;

    // Matrices
    QMatrix4x4 m_view;
    QMatrix4x4 m_proj;
    QMatrix4x4 m_viewProj;

    // Scene
    std::vector<ObjectData> m_objects;
    Camera m_cam;

    // Timing
    QTimer m_timer;
    bool m_paused = false;

    // Params
    int m_computeW = 200;
    int m_computeH = 150;
    int m_hiComputeW = 640;
    int m_hiComputeH = 360;

    int m_gridSize = 25;
    float m_spacing = 1e10f;

    // Feature detection
    bool m_useCompute = true;
};