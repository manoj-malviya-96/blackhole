#include "blackhole_widget.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

BlackHoleWidget::BlackHoleWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);

    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (!m_paused) {
            m_engine.step(0.016);
        }
        update();
    });
    m_timer.start(16); // ~60 fps
}

BlackHoleWidget::~BlackHoleWidget() {
    makeCurrent();
    m_renderer.shutdown();
    doneCurrent();
}

void BlackHoleWidget::initializeGL() { m_renderer.initialize(); }

void BlackHoleWidget::resizeGL(int w, int h) { m_renderer.resize(w, h); }

void BlackHoleWidget::paintGL() { m_renderer.render(m_cam, m_engine, width(), height()); }

void BlackHoleWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_cam.beginDrag(e->position());
    }
}

void BlackHoleWidget::mouseMoveEvent(QMouseEvent* e) { m_cam.drag(e->position()); }

void BlackHoleWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        m_cam.endDrag();
    }
}

void BlackHoleWidget::wheelEvent(QWheelEvent* e) {
    const QPoint numDeg = e->angleDelta() / 120;
    if (!numDeg.isNull()) {
        m_cam.zoom(float(numDeg.y()));
    }
}

void BlackHoleWidget::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
    case Qt::Key_Space:
        m_paused = !m_paused;
        break;
    case Qt::Key_R:
        m_cam.reset();
        break;
    case Qt::Key_Escape:
        window()->close();
        break;
    default:
        QOpenGLWidget::keyPressEvent(e);
    }
}
