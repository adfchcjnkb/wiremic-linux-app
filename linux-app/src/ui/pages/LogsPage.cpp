#include "LogsPage.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

#include "../components/GlassPanel.hpp"

namespace wiremic::ui {

LogsPage::LogsPage(QWidget* parent) : QWidget(parent) {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(18);

  auto* titleLabel = new QLabel("Logs", this);
  titleLabel->setStyleSheet(
      "color: rgb(245,246,250); font-size: 28px; font-weight: 700;");
  rootLayout->addWidget(titleLabel);

  auto* card = new GlassPanel(this);
  auto* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(4, 4, 4, 4);

  scrollArea_ = new QScrollArea(card);
  scrollArea_->setWidgetResizable(true);
  scrollArea_->setFrameShape(QFrame::NoFrame);
  scrollArea_->viewport()->setStyleSheet("background: transparent;");

  auto* container = new QWidget();
  container->setStyleSheet("background: transparent;");
  logLayout_ = new QVBoxLayout(container);
  logLayout_->setContentsMargins(14, 14, 14, 14);
  logLayout_->setSpacing(6);
  logLayout_->addStretch();

  scrollArea_->setWidget(container);
  cardLayout->addWidget(scrollArea_);

  rootLayout->addWidget(card, 1);
}

void LogsPage::appendLog(const QString& timestamp, const QString& message) {
  auto* row = new QWidget();
  auto* layout = new QHBoxLayout(row);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(12);

  auto* timeLabel = new QLabel(timestamp, row);
  timeLabel->setStyleSheet(
      "color: rgb(108,112,134); font-size: 11px; font-family: monospace;");
  timeLabel->setFixedWidth(60);

  auto* messageLabel = new QLabel(message, row);
  messageLabel->setStyleSheet("color: rgb(245,246,250); font-size: 12px;");
  messageLabel->setWordWrap(true);

  layout->addWidget(timeLabel);
  layout->addWidget(messageLabel, 1);

  logLayout_->insertWidget(logLayout_->count() - 1, row);

  QScrollBar* bar = scrollArea_->verticalScrollBar();
  QMetaObject::invokeMethod(
      this, [bar]() { bar->setValue(bar->maximum()); }, Qt::QueuedConnection);
}

}
