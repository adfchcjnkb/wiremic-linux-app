#include <QApplication>
#include <QIcon>

#include "MainWindow.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setOrganizationName("WireMic");
  app.setApplicationName("WireMic");
  app.setApplicationDisplayName("WireMic");
  app.setWindowIcon(QIcon(":/WireMic/resources/icons/app_icon.svg"));
  app.setStyle("Fusion");

  wiremic::ui::MainWindow window;
  window.show();

  return app.exec();
}
