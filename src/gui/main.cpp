#include "app.hpp"
#include "window.hpp"

#include <string>

#include <QApplication>
#include <QIcon>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace {

// PNG зібрані в exe. Окремий qico не потрібен: Qt Gui читає PNG сам.
QIcon application_icon() {
  QIcon icon;
  const int sizes[] = {16, 20, 24, 32, 48, 64, 128, 256};
  for (const int size : sizes) {
    icon.addFile(QString(":/icons/rtx-vsr-%1.png").arg(size));
  }
  return icon;
}

}  // namespace

int run_gui(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("rtx-vsr");
  const QIcon icon = application_icon();
  QApplication::setWindowIcon(icon);
  Window window;
  window.setWindowIcon(icon);
  window.show();
  if (argc > 1 && std::string(argv[1]) == "--self-check") {
    QTimer::singleShot(0, &app, [&app, &window] {
      const bool ok = window.findChild<QWidget*>("start") != nullptr && window.findChild<QWidget*>("gpu") != nullptr &&
                      window.findChild<QWidget*>("profile") != nullptr &&
                      window.findChild<QWidget*>("benchmark") != nullptr &&
                      !window.windowIcon().isNull() && window.windowIcon().availableSizes().contains(QSize(16, 16)) &&
                      window.windowIcon().availableSizes().contains(QSize(256, 256));
      app.exit(ok ? 0 : 1);
    });
  }
  return app.exec();
}
