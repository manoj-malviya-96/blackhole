#include "blackhole_widget.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

BlackHoleWidget::BlackHoleWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);

    // Single BH at origin (mass ~ 4.3e6 solar masses)
    objects_.push_back({
        QVector4D(0, 0, 0, 5e10f),   // pos + radius
        QVector4D(1, 1, 1, 1),       // color
        4.3e6 * physics::kSolarMass, // mass
        QVector3D(0, 0, 0),          // velocity (unused)
        0.9                          // spin (a/M) - near-extremal, for a visible frame-drag swirl
    });

    connect(&timer_, &QTimer::timeout, this, [this]() {
        if (!paused_) {
            // future dynamics here (object movement, etc.)
        }
        update();
    });
    timer_.start(16); // ~60 fps
}

BlackHoleWidget::~BlackHoleWidget() {
    makeCurrent();
    renderer_.shutdown();
    doneCurrent();
}

void BlackHoleWidget::initializeGL() { renderer_.initialize(); }

void BlackHoleWidget::resizeGL(int w, int h) { renderer_.resize(w, h); }

void BlackHoleWidget::paintGL() { renderer_.render(cam_, objects_, width(), height()); }

void BlackHoleWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        cam_.beginDrag(e->position());
    }
}

void BlackHoleWidget::mouseMoveEvent(QMouseEvent* e) { cam_.drag(e->position()); }

void BlackHoleWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        cam_.endDrag();
    }
}

void BlackHoleWidget::wheelEvent(QWheelEvent* e) {
    const QPoint numDeg = e->angleDelta() / 120;
    if (!numDeg.isNull()) {
        cam_.zoom(float(numDeg.y()));
    }
}

void BlackHoleWidget::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Space:
        paused_ = !paused_;
        break;
    case Qt::Key_R:
        cam_.reset();
        break;
    case Qt::Key_Escape:
        window()->close();
        break;
    default:
        QOpenGLWidget::keyPressEvent(e);
    }
}
