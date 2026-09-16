#pragma once
#include <QOpenGLWidget>
#include <QTimer>
#include <vector>
#include "renderer/camera.h"
#include "renderer/renderer.h"
#include "scene.h"

// Thin Qt widget: owns the scene/camera state and forwards Qt lifecycle
// and input events to Renderer/Camera. No raw GL calls live here.
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
    Renderer renderer_;
    Camera cam_;
    std::vector<SceneObject> objects_;

    QTimer timer_;
    bool paused_ = false;
};
