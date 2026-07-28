#pragma once

#include <QColor>
#include <QPixmap>
#include <QString>

namespace wiremic::ui {

class IconLoader {
 public:
  static QPixmap Render(const QString& resourcePath, int size,
                         QColor tint = QColor());
};

}
