#pragma once

#include <QObject>
#include <QSslKey>
#include <QSslSocket>
#include <QTimer>

#include "CertificateManager.hpp"
#include "MessageFraming.hpp"
#include "Protocol.hpp"

namespace wiremic::network {

class ControlClient : public QObject {
  Q_OBJECT

 public:
  explicit ControlClient(security::CertificateManager& certificateManager,
                          QObject* parent = nullptr);
  ~ControlClient() override;

  void connectToDevice(const QString& host, quint16 port,
                        protocol::ConnectRequest request);
  void disconnectFromDevice(
      protocol::DisconnectReason reason =
          protocol::DisconnectReason::UserRequested);
  [[nodiscard]] QString peerCertificateFingerprint() const;
  QString peerFingerprint() const;

 signals:
  void responseReceived(protocol::ConnectResponse response);
  void timedOut(QString requestId);
  void connectionLost(QString requestId);
  void remoteDisconnected(protocol::DisconnectReason reason);
  void errorOccurred(QString message);

 private slots:
  void onEncrypted();
  void onReadyRead();
  void onSslErrors(const QList<QSslError>& errors);
  void onDisconnected();
  void onTimeout();
  void onKeepAliveTimer();

 private:
  security::CertificateManager& certificateManager_;
  QSslSocket socket_;
  protocol::MessageFramer framer_;
  protocol::ConnectRequest pendingRequest_;
  QTimer timeoutTimer_;
  QTimer keepAliveTimer_;
  uint64_t keepAliveSequence_{0};
  uint64_t lastAckedSequence_{0};
  int missedKeepAlives_{0};
  bool sessionActive_{false};
  // Set whenever the teardown is one we caused (or already reported), so
  // onDisconnected() knows not to report the socket closing as a surprise loss.
  bool lossReported_{false};
};

}  // namespace wiremic::network
