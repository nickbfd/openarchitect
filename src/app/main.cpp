#include "MainWindow.h"

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    // The 3D view uses an OpenGL 3.3 core profile, which also works on macOS.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setApplicationName("OpenArchitect");
    QApplication::setOrganizationName("OpenArchitect");
    QApplication::setApplicationVersion(OA_VERSION);

    MainWindow window;
    window.show();
    if (argc > 1) window.openFile(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
