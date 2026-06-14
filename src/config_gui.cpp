#include "windowing/mainwindow.hpp"
#include <QApplication>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);

  Windowing::MainWindow w;
  w.show();

  return app.exec();
}
