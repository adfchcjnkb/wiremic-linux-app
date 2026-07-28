#include "AboutPage.hpp"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include "../Theme.hpp"
#include "../components/GlassPanel.hpp"

namespace wiremic::ui {

namespace {

QLabel* MakeBadge() {
  QPixmap pixmap(60, 60);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QPainterPath path;
  path.addRoundedRect(pixmap.rect(), 18, 18);
  QLinearGradient gradient(0, 0, 60, 60);
  gradient.setColorAt(0.0, theme::kAccentStart);
  gradient.setColorAt(1.0, theme::kAccentEnd);
  painter.fillPath(path, gradient);
  painter.setPen(Qt::white);
  QFont f = painter.font();
  f.setPointSize(24);
  f.setBold(true);
  painter.setFont(f);
  painter.drawText(pixmap.rect(), Qt::AlignCenter, "W");
  painter.end();

  auto* label = new QLabel();
  label->setPixmap(pixmap);
  return label;
}

void AddRow(QGridLayout* grid, int row, const QString& label,
            const QString& value) {
  auto* labelWidget = new QLabel(label);
  labelWidget->setStyleSheet("color: rgb(164,168,186); font-size: 12px;");
  auto* valueWidget = new QLabel(value);
  valueWidget->setStyleSheet("color: rgb(245,246,250); font-size: 12px;");
  grid->addWidget(labelWidget, row, 0);
  grid->addWidget(valueWidget, row, 1);
}

}

AboutPage::AboutPage(QWidget* parent) : QWidget(parent) {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(18);

  auto* titleLabel = new QLabel("About", this);
  titleLabel->setStyleSheet(
      "color: rgb(245,246,250); font-size: 28px; font-weight: 700;");
  rootLayout->addWidget(titleLabel);

  auto* card = new GlassPanel(this);
  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(26, 26, 26, 26);
  cardLayout->setSpacing(16);

  auto* headerLayout = new QHBoxLayout();
  headerLayout->addWidget(MakeBadge());
  auto* nameColumn = new QVBoxLayout();
  auto* nameLabel = new QLabel("WireMic");
  nameLabel->setStyleSheet(
      "color: rgb(245,246,250); font-size: 22px; font-weight: 700;");
  auto* versionLabel = new QLabel("Version 1.0.0");
  versionLabel->setStyleSheet("color: rgb(164,168,186); font-size: 12px;");
  nameColumn->addWidget(nameLabel);
  nameColumn->addWidget(versionLabel);
  headerLayout->addLayout(nameColumn);
  headerLayout->addStretch();
  cardLayout->addLayout(headerLayout);

  auto* description = new QLabel(
      "WireMic turns any Android phone or another computer on your local "
      "network into a wireless microphone for this machine. Audio is "
      "streamed with the Opus codec over an encrypted, authenticated "
      "connection with no internet server involved.");
  description->setWordWrap(true);
  description->setStyleSheet("color: rgb(164,168,186); font-size: 13px;");
  cardLayout->addWidget(description);

  auto* grid = new QGridLayout();
  grid->setHorizontalSpacing(28);
  grid->setVerticalSpacing(10);
  AddRow(grid, 0, "License", "MIT");
  AddRow(grid, 1, "Audio Codec", "Opus (restricted low-delay)");
  AddRow(grid, 2, "Audio Backend", "PipeWire / PulseAudio");
  AddRow(grid, 3, "Transport", "TLS 1.3 + ChaCha20-Poly1305");
  cardLayout->addLayout(grid);
  cardLayout->addStretch();

  rootLayout->addWidget(card, 1);
}

}
