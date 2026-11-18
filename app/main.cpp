#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("心晴 · 情绪陪伴智能体"));
    MainWindow win;
    win.show();
    return app.exec();
}
