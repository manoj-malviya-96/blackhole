#pragma once
#include <QOpenGLWidget>
#include <QTimer>
#include "engine/engine.h"
#include "renderer/camera.h"
#include "renderer/renderer.h"

// Thin Qt widget: owns the engine and camera, forwards Qt lifecycle
// and input events to Renderer/Camera/Engine. No raw GL calls live here.
class BlackHoleWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit BlackHoleWidget(QWidget* parent = nullptr);
    ~BlackHoleWidget() override;

    QSize minimumSizeHint() const override { return {640, 480}; }
    QSize sizeHint() const override { return {960, 720}; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    engine::Engine engine_;
    renderer::Camera cam_;
    renderer::Renderer renderer_;

    QTimer timer_;
    bool paused_ = false;
};
