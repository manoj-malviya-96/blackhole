#include <QtCore/qresource.h>
#include <QApplication>
#include <QMainWindow>
#include <QSurfaceFormat>
#include "blackhole_widget.h"

int main(int argc, char** argv) {
    // Request OpenGL 4.3; macOS will silently cap to 4.1 and we use the fragment fallback.
    QSurfaceFormat fmt;
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    // Ensure the compiled resource is registered
    Q_INIT_RESOURCE(resources);

    QApplication app(argc, argv);
    QMainWindow win;
    auto* w = new BlackHoleWidget(&win);
    w->show();
    win.setCentralWidget(w);
    win.resize(960, 720);
    win.setWindowTitle("Black Hole — Qt QOpenGLWidget (Compute + Grid)");
    win.show();
    return app.exec();
}