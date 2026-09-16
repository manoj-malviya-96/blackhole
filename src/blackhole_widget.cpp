#include "blackhole_widget.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

BlackHoleWidget::BlackHoleWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);

    connect(&timer_, &QTimer::timeout, this, [this]() {
        if (!paused_) {
            engine_.step(0.016);
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

void BlackHoleWidget::paintGL() { renderer_.render(cam_, engine_, width(), height()); }

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
