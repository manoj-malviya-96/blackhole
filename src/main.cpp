#include <QApplication>
#include <QMainWindow>
#include <QSurfaceFormat>
#include "blackhole_widget.h"

int main(int argc, char** argv) {
    // Request OpenGL 4.3 Core for compute shaders
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(4, 3);
    fmt.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);

    QMainWindow win;
    auto* w = new BlackHoleWidget(&win);
    win.setCentralWidget(w);
    win.resize(960, 720);
    win.setWindowTitle("Black Hole — Qt QOpenGLWidget (Compute + Grid)");
    win.show();

    return app.exec();
}