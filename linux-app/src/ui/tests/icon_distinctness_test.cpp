#include <QApplication>
#include <QCryptographicHash>

#include <iostream>
#include <set>
#include <vector>

#include "Check.hpp"
#include "IconLoader.hpp"

using namespace wiremic::ui;

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  const std::vector<QString> icons = {
      ":/WireMic/resources/icons/icon_dashboard.svg",
      ":/WireMic/resources/icons/icon_devices.svg",
      ":/WireMic/resources/icons/icon_connected.svg",
      ":/WireMic/resources/icons/icon_settings.svg",
      ":/WireMic/resources/icons/icon_logs.svg",
      ":/WireMic/resources/icons/icon_about.svg",
      ":/WireMic/resources/icons/icon_microphone.svg",
      ":/WireMic/resources/icons/icon_phone.svg",
      ":/WireMic/resources/icons/icon_desktop.svg",
  };

  std::set<QByteArray> hashes;

  for (const auto& path : icons) {
    QPixmap pixmap = IconLoader::Render(path, 36);
    WIREMIC_CHECK(!pixmap.isNull());

    QImage image = pixmap.toImage();
    bool hasVisiblePixel = false;
    for (int y = 0; y < image.height() && !hasVisiblePixel; ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (qAlpha(image.pixel(x, y)) > 10) {
          hasVisiblePixel = true;
          break;
        }
      }
    }
    WIREMIC_CHECK(hasVisiblePixel);

    QByteArray bytes(reinterpret_cast<const char*>(image.constBits()),
                      static_cast<int>(image.sizeInBytes()));
    const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    hashes.insert(hash);

    std::cout << "ICON_RENDERED_NON_EMPTY: " << path.toStdString() << "\n";
  }

  WIREMIC_CHECK(hashes.size() == icons.size());
  std::cout << "ALL_ICONS_VISUALLY_DISTINCT_OK (" << hashes.size() << "/"
            << icons.size() << ")\n";

  std::cout << "ICON_DISTINCTNESS_TESTS_PASSED\n";
  return 0;
}
