#include "ControlClient.hpp"

#include <QCryptographicHash>

namespace wiremic::network {

namespace {

QSslCertificate CertificateFromPem(const std::string& pem) {
  return QSslCertificate(QByteArray::fromStdString(pem), QSsl::Pem);
}

QSslKey KeyFromPem(const std::string& pem) {
  return QSslKey(QByteArray::fromStdString(pem), QSsl::Ec, QSsl::Pem,
                 QSsl::PrivateKey);
}

}

QString ControlClient::peerCertificateFingerprint() const {
  const auto certificate = socket_.peerCertificate();
  if (certificate.isNull()) return {};
  return QStringLiteral("sha256:") +
         certificate.digest(QCryptographicHash::Sha256).toHex();
}

ControlClient::ControlClient(security::CertificateManager& certificateManager,
                              QObject* parent)
    : QObject(parent), certificateManager_(certificateManager) {
  connect(&socket_, &QSslSocket::encrypted, this, &ControlClient::onEncrypted);
  connect(&socket_, &QSslSocket::readyRead, this,
          &ControlClient::onReadyRead);
  connect(&socket_,
          QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
          this, &ControlClient::onSslErrors);
  connect(&socket_, &QSslSocket::disconnected, this,
          &ControlClient::onDisconnected);
  connect(&socket_,
          QOverload<QAbstractSocket::SocketError>::of(
              &QAbstractSocket::errorOccurred),
          this, [this](QAbstractSocket::SocketError) {
            emit errorOccurred(QStringLiteral("client socket error: %1")
                                    .arg(socket_.errorString()));
          });

  timeoutTimer_.setSingleShot(true);
  connect(&timeoutTimer_, &QTimer::timeout, this, &ControlClient::onTimeout);

  connect(&keepAliveTimer_, &QTimer::timeout, this,
          &ControlClient::onKeepAliveTimer);
}

ControlClient::~ControlClient() {
  lossReported_ = true;
  sessionActive_ = false;
  socket_.disconnect(this);
  socket_.abort();
}

void ControlClient::connectToDevice(const QString& host, quint16 port,
                                     protocol::ConnectRequest request) {
  if (!QSslSocket::supportsSsl() ||
      QSslSocket::activeBackend() != QStringLiteral("openssl")) {
    emit errorOccurred(QStringLiteral(
        "No functional TLS backend available (active backend: %1) — "
        "refusing to connect insecurely")
                            .arg(QSslSocket::activeBackend()));
    emit timedOut(QString::fromStdString(request.requestId));
    return;
  }

  pendingRequest_ = std::move(request);
  sessionActive_ = false;
  lossReported_ = false;
  framer_.Reset();
  keepAliveSequence_ = 0;
  lastAckedSequence_ = 0;
  missedKeepAlives_ = 0;

  const auto& localCert = certificateManager_.localCertificate();
  socket_.setLocalCertificate(CertificateFromPem(localCert.certificatePem));
  socket_.setPrivateKey(KeyFromPem(localCert.privateKeyPem));
  socket_.setPeerVerifyMode(QSslSocket::VerifyNone);

  timeoutTimer_.start(protocol::kConnectRequestTimeoutMs);
  socket_.connectToHostEncrypted(host, port);
}

void ControlClient::disconnectFromDevice(protocol::DisconnectReason reason) {
  keepAliveTimer_.stop();
  timeoutTimer_.stop();
  sessionActive_ = false;
  lossReported_ = true;

  if (socket_.state() == QAbstractSocket::ConnectedState) {
    protocol::DisconnectMessage message;
    message.reason = reason;
    const auto framed = protocol::FrameMessage(protocol::ToJson(message));
    socket_.write(framed.data(), static_cast<qint64>(framed.size()));
    socket_.flush();
  }
  socket_.disconnectFromHost();
}

void ControlClient::onEncrypted() {
  const auto json = protocol::ToJson(pendingRequest_);
  const auto framed = protocol::FrameMessage(json);
  socket_.write(framed.data(), static_cast<qint64>(framed.size()));
}

void ControlClient::onReadyRead() {
  if (!socket_.isEncrypted()) {
    emit errorOccurred(QStringLiteral(
        "Rejected data on a socket that never completed a TLS handshake"));
    socket_.abort();
    return;
  }

  const QByteArray data = socket_.readAll();
  framer_.Feed(data.constData(), static_cast<size_t>(data.size()));

  try {
    while (auto message = framer_.NextMessage()) {
      const auto type = protocol::PeekMessageType(*message);

      if (type == protocol::ControlMessageType::ConnectResponse) {
        auto response = protocol::ParseConnectResponse(*message);
        if (!response) {
          emit errorOccurred(QStringLiteral("Received malformed response"));
          continue;
        }
        if (response->requestId == pendingRequest_.requestId) {
          timeoutTimer_.stop();
          if (response->accepted) {
            sessionActive_ = true;
            missedKeepAlives_ = 0;
            keepAliveTimer_.start(protocol::kKeepaliveIntervalMs);
          }
        }
        emit responseReceived(*response);
        continue;
      }

      if (type == protocol::ControlMessageType::KeepAliveAck) {
        auto ack = protocol::ParseKeepAliveAck(*message);
        if (ack) {
          lastAckedSequence_ = ack->sequence;
          missedKeepAlives_ = 0;
        }
        continue;
      }

      if (type == protocol::ControlMessageType::Disconnect) {
        auto disconnect = protocol::ParseDisconnect(*message);
        sessionActive_ = false;
        lossReported_ = true;
        keepAliveTimer_.stop();
        emit remoteDisconnected(disconnect
                                     ? disconnect->reason
                                     : protocol::DisconnectReason::RemoteShutdown);
        continue;
      }

      emit errorOccurred(QStringLiteral("Received unexpected control message"));
    }
  } catch (const std::exception& e) {
    emit errorOccurred(
        QStringLiteral("Malformed control message, closing connection: %1")
            .arg(e.what()));
    socket_.abort();
  }
}

void ControlClient::onKeepAliveTimer() {
  if (!sessionActive_) return;

  if (missedKeepAlives_ >= protocol::kKeepaliveMissedLimit) {
    keepAliveTimer_.stop();
    sessionActive_ = false;
    lossReported_ = true;
    emit connectionLost(QString::fromStdString(pendingRequest_.requestId));
    return;
  }

  ++keepAliveSequence_;
  ++missedKeepAlives_;

  protocol::KeepAlive keepAlive;
  keepAlive.sequence = keepAliveSequence_;
  const auto framed = protocol::FrameMessage(protocol::ToJson(keepAlive));
  socket_.write(framed.data(), static_cast<qint64>(framed.size()));
}

void ControlClient::onSslErrors(const QList<QSslError>& errors) {
  socket_.ignoreSslErrors(errors);
}

void ControlClient::onDisconnected() {
  const bool wasStreaming = sessionActive_;

  timeoutTimer_.stop();
  keepAliveTimer_.stop();
  sessionActive_ = false;

  if (wasStreaming && !lossReported_) {
    lossReported_ = true;
    emit connectionLost(QString::fromStdString(pendingRequest_.requestId));
  }
}

QString ControlClient::peerFingerprint() const {
  return peerCertificateFingerprint();
}

void ControlClient::onTimeout() {
  emit timedOut(QString::fromStdString(pendingRequest_.requestId));
  socket_.abort();
}

}
