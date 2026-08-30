#include "mainwindow.h"

#include <QApplication>
#include <QDir>
#include <QLocale>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("XOR File Processor");
    QApplication::setApplicationVersion("1.0.0");
    QLocale::setDefault(QLocale::c());

    QDir::setCurrent(QCoreApplication::applicationDirPath());

    MainWindow window;
    window.show();
    return app.exec();
}
