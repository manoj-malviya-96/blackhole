#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QVector>
#include <QTimer>
#include <QImage>
#include <vector>

class BlackHoleWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_3_Core {
    Q_OBJECT
public:
    explicit BlackHoleWidget(QWidget* parent = nullptr);
    ~BlackHoleWidget() override;

    QSize minimumSizeHint() const override { return {640, 480}; }
    QSize sizeHint() const override { return {960, 720}; }

protected:
    // Qt GL lifecycle
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
        QVector4D posRadius;   // xyz position (meters), w = radius (meters)
        QVector4D color;       // rgb,a
        double    mass;        // kilograms
        QVector3D velocity;    // not used here
    };

    // Camera
    struct Camera {
        QVector3D target{0.f, 0.f, 0.f};
        float radius    = 6.34e10f;
        float minRadius = 1e10f;
        float maxRadius = 1e12f;
        float azimuth   = 0.0f;
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
    QOpenGLShaderProgram gridProg_;
    QOpenGLShaderProgram quadProg_;
    QOpenGLShaderProgram computeProg_;
    QOpenGLShaderProgram lensProg_; // fallback fragment shader

    // UBOs
    GLuint cameraUBO_ = 0;
    GLuint diskUBO_   = 0;
    GLuint objectsUBO_ = 0;

    // Fullscreen quad + texture
    GLuint quadVAO_ = 0;
    GLuint quadVBO_ = 0;
    GLuint quadEBO_ = 0;
    GLuint outputTex_ = 0;

    // Grid
    GLuint gridVAO_ = 0;
    GLuint gridVBO_ = 0;
    GLuint gridEBO_ = 0;
    int gridIndexCount_ = 0;

    // Matrices
    QMatrix4x4 view_;
    QMatrix4x4 proj_;
    QMatrix4x4 viewProj_;

    // Scene
    std::vector<ObjectData> objects_;
    Camera cam_;

    // Timing
    QTimer timer_;
    bool paused_ = false;

    // Params
    int computeW_ = 200;
    int computeH_ = 150;
    int hiComputeW_ = 640;
    int hiComputeH_ = 360;

    int gridSize_ = 25;
    float spacing_ = 1e10f;

    // Feature detection
    bool useCompute_ = true;
};