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

void CloseLogFile() {
  if (!g_logFile) return;
  if (g_logFile->isOpen()) g_logFile->close();
  delete g_logFile;
  g_logFile = nullptr;
}

void ReportFatal(const QString& message) {
  if (qApp) {
    QMessageBox::critical(nullptr, "WireMic failed to start", message);
  } else {
    fprintf(stderr, "WireMic failed to start: %s\n", qPrintable(message));
  }
  qInstallMessageHandler(nullptr);
  CloseLogFile();
}

}

int main(int argc, char** argv) {
  try {
    QApplication app(argc, argv);
    app.setOrganizationName("WireMic");
    app.setApplicationName("WireMic");
    app.setApplicationDisplayName("WireMic");

    const bool logFileReady = InitLogFile();
    qInstallMessageHandler(LogMessageHandler);
    if (!logFileReady) {
      qWarning("Failed to open log file, logging to stderr only");
    }
    qInfo("WireMic starting up");

    QIcon appIcon(":/WireMic/resources/icons/app_icon.svg");
    if (!appIcon.isNull()) {
      app.setWindowIcon(appIcon);
    } else {
      qWarning("Failed to load app icon");
    }

    app.setStyle("Fusion");

    wiremic::ui::MainWindow window;
    window.show();

    const int result = app.exec();
    qInfo("WireMic exiting normally (code %d)", result);

    qInstallMessageHandler(nullptr);
    CloseLogFile();
    return result;

  } catch (const std::exception& e) {
    qCritical("Unhandled exception during startup: %s", e.what());
    ReportFatal(
        QString("WireMic could not start due to an internal error:\n\n%1\n\n"
                "Check the log file for details.")
            .arg(e.what()));
    return 1;

  } catch (...) {
    qCritical("Unhandled unknown exception during startup");
    ReportFatal("WireMic could not start due to an unknown internal error. "
                 "Check the log file for details.");
    return 1;
  }
}
