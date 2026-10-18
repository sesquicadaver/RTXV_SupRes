#include "app.hpp"
#include "window.hpp"

#include <string>

#include <QApplication>
#include <QTimer>
#include <QWidget>

int run_gui(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("rtx-vsr");
  Window window;
  window.show();
  if (argc > 1 && std::string(argv[1]) == "--self-check") {
    QTimer::singleShot(0, &app, [&app, &window] {
      const bool ok = window.findChild<QWidget*>("start") != nullptr && window.findChild<QWidget*>("gpu") != nullptr &&
                      window.findChild<QWidget*>("profile") != nullptr &&
                      window.findChild<QWidget*>("benchmark") != nullptr;
      app.exit(ok ? 0 : 1);
    });
  }
  return app.exec();
}
