#include <QApplication>
#include <QCoreApplication>
#include "ui/mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Holographic Optical Tweezer Control");
#ifdef HOT_APP_VERSION
    QCoreApplication::setApplicationVersion(QString::fromLatin1(HOT_APP_VERSION));
#endif
    
    MainWindow window;
    window.show();
    
    return app.exec();
}
