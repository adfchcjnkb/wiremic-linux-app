#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QFile>
#include <QTimer>

#include <cstdlib>
#include <exception>
#include <iostream>

#include "MainWindow.hpp"

namespace {

QFile* g_logFile = nullptr;

void LogMessageHandler(QtMsgType type, const QMessageLogContext& context,
                        const QString& message) {
  const char* level = "DEBUG";
  switch (type) {
    case QtDebugMsg: level = "DEBUG"; break;
    case QtInfoMsg: level = "INFO"; break;
    case QtWarningMsg: level = "WARNING"; break;
    case QtCriticalMsg: level = "CRITICAL"; break;
    case QtFatalMsg: level = "FATAL"; break;
  }

  const QString line =
      QStringLiteral("[%1] %2: %3 (%4:%5)")
          .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),
               level, message,
               context.file ? context.file : "?")
          .arg(context.line);

  fprintf(stderr, "%s\n", qPrintable(line));
  fflush(stderr);

  if (g_logFile && g_logFile->isOpen()) {
    QTextStream stream(g_logFile);
    stream << line << "\n";
    stream.flush();
  }

  if (type == QtFatalMsg) std::abort();
}

bool InitLogFile() {
  const QString logDir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      "/logs";
  QDir().mkpath(logDir);

  const QString logPath = logDir + "/wiremic.log";
  g_logFile = new QFile(logPath);
  return g_logFile->open(QIODevice::WriteOnly | QIODevice::Append |
                          QIODevice::Text);
}

}  // namespace

int main(int argc, char** argv) {
  // Initialize logging
  const bool logFileReady = InitLogFile();
  qInstallMessageHandler(LogMessageHandler);

  if (!logFileReady) {
    qWarning("Failed to open log file, logging to stderr only");
  }

  try {
    qInfo("WireMic starting up");

    // Create application
    QApplication app(argc, argv);
    app.setOrganizationName("WireMic");
    app.setApplicationName("WireMic");
    app.setApplicationDisplayName("WireMic");
    
    // Set icon
    QIcon appIcon(":/WireMic/resources/icons/app_icon.svg");
    if (!appIcon.isNull()) {
      app.setWindowIcon(appIcon);
    } else {
      qWarning("Failed to load app icon");
    }
    
    app.setStyle("Fusion");

    // Create and show main window
    wiremic::ui::MainWindow window;
    window.show();

    // Process events
    const int result = app.exec();
    qInfo("WireMic exiting normally (code %d)", result);
    return result;

  } catch (const std::exception& e) {
    qCritical("Unhandled exception during startup: %s", e.what());
    QMessageBox::critical(
        nullptr, "WireMic failed to start",
        QString("WireMic could not start due to an internal error:\n\n%1\n\n"
                "Check the log file for details.")
            .arg(e.what()));
    return 1;

  } catch (...) {
    qCritical("Unhandled unknown exception during startup");
    QMessageBox::critical(nullptr, "WireMic failed to start",
                           "WireMic could not start due to an unknown "
                           "internal error. Check the log file for details.");
    return 1;
  }
}