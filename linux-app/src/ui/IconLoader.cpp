#include "IconLoader.hpp"

#include <QPainter>
#include <QSvgRenderer>

#include <unordered_map>

namespace wiremic::ui {

QPixmap IconLoader::Render(const QString& resourcePath, int size,
                            QColor tint) {
  static std::unordered_map<std::string, QPixmap> cache;
  const std::string key = resourcePath.toStdString() + ":" +
                           std::to_string(size) + ":" +
                           std::to_string(tint.isValid() ? tint.rgba() : 0u);

  auto it = cache.find(key);
  if (it != cache.end()) return it->second;

  QSvgRenderer renderer(resourcePath);
  QPixmap pixmap(size, size);
  pixmap.fill(Qt::transparent);

  if (renderer.isValid()) {
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.end();

    if (tint.isValid()) {
      QPixmap tinted(pixmap.size());
      tinted.fill(Qt::transparent);
      QPainter tintPainter(&tinted);
      tintPainter.drawPixmap(0, 0, pixmap);
      tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
      tintPainter.fillRect(tinted.rect(), tint);
      tintPainter.end();
      pixmap = tinted;
    }
  }

  cache[key] = pixmap;
  return pixmap;
}

}
