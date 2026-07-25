#include "ControlClient.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstring>

namespace wiremic::android {

namespace {

constexpr int kConnectTimeoutSec = 8;

std::string ComputeFingerprint(X509* cert) {
  if (!cert) return {};
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digestLength = 0;
  if (X509_digest(cert, EVP_sha256(), digest.data(), &digestLength) == 0) {
    return {};
  }
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out = "sha256:";
  out.reserve(7 + digestLength * 2);
  for (unsigned int i = 0; i < digestLength; ++i) {
    out.push_back(kHex[(digest[i] >> 4) & 0xF]);
    out.push_back(kHex[digest[i] & 0xF]);
  }
  return out;
}

bool LoadCertificateIntoContext(SSL_CTX* ctx,
                                 const security::Certificate& certificate) {
  BIO* certBio = BIO_new_mem_buf(certificate.certificatePem.data(),
                                  static_cast<int>(certificate.certificatePem.size()));
  X509* cert = certBio ? PEM_read_bio_X509(certBio, nullptr, nullptr, nullptr)
                        : nullptr;
  bool certOk = cert && SSL_CTX_use_certificate(ctx, cert) == 1;
  if (cert) X509_free(cert);
  if (certBio) BIO_free(certBio);
  if (!certOk) return false;

  BIO* keyBio = BIO_new_mem_buf(certificate.privateKeyPem.data(),
                                 static_cast<int>(certificate.privateKeyPem.size()));
  EVP_PKEY* key = keyBio ? PEM_read_bio_PrivateKey(keyBio, nullptr, nullptr, nullptr)
                         : nullptr;
  bool keyOk = key && SSL_CTX_use_PrivateKey(ctx, key) == 1;
  if (key) EVP_PKEY_free(key);
  if (keyBio) BIO_free(keyBio);
  return keyOk;
}

}  // namespace

ControlClient::ControlClient(security::CertificateManager& certificateManager)
    : certificateManager_(certificateManager) {}

ControlClient::~ControlClient() {
  disconnectFromDevice();
  if (thread_.joinable()) thread_.join();
}

void ControlClient::setResponseCallback(ResponseCallback callback) {
  responseCallback_ = std::move(callback);
}
void ControlClient::setTimeoutCallback(TimeoutCallback callback) {
  timeoutCallback_ = std::move(callback);
}
void ControlClient::setConnectionLostCallback(ConnectionLostCallback callback) {
  connectionLostCallback_ = std::move(callback);
}
void ControlClient::setRemoteDisconnectCallback(RemoteDisconnectCallback callback) {
  remoteDisconnectCallback_ = std::move(callback);
}
void ControlClient::setErrorCallback(ErrorCallback callback) {
  errorCallback_ = std::move(callback);
}

std::string ControlClient::peerCertificateFingerprint() const {
  return peerFingerprint_;
}

void ControlClient::connectToDevice(const std::string& host, uint16_t port,
                                     protocol::ConnectRequest request) {
  disconnectFromDevice();
  if (thread_.joinable()) thread_.join();

  running_ = true;
  pendingRequestId_ = request.requestId;
  thread_ = std::thread(&ControlClient::run, this, host, port, std::move(request));
}

void ControlClient::closeConnection() {
  if (ssl_) {
    SSL_shutdown(ssl_);
    SSL_free(ssl_);
    ssl_ = nullptr;
  }
  if (sslContext_) {
    SSL_CTX_free(sslContext_);
    sslContext_ = nullptr;
  }
  if (socketFd_ >= 0) {
    close(socketFd_);
    socketFd_ = -1;
  }
}

void ControlClient::disconnectFromDevice(protocol::DisconnectReason reason) {
  if (sessionActive_ && ssl_) {
    protocol::DisconnectMessage message;
    message.reason = reason;
    sendFramed(protocol::ToJson(message));
  }
  running_ = false;
  sessionActive_ = false;
}

bool ControlClient::sendFramed(const std::string& json) {
  if (!ssl_) return false;
  const auto framed = protocol::FrameMessage(json);
  const int written = SSL_write(ssl_, framed.data(), static_cast<int>(framed.size()));
  return written > 0;
}

void ControlClient::run(std::string host, uint16_t port,
                         protocol::ConnectRequest request) {
  socketFd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (socketFd_ < 0) {
    if (errorCallback_) errorCallback_("socket() failed");
    running_ = false;
    return;
  }

  struct sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
    struct hostent* resolved = gethostbyname(host.c_str());
    if (!resolved) {
      if (errorCallback_) errorCallback_("failed to resolve host");
      closeConnection();
      running_ = false;
      return;
    }
    std::memcpy(&address.sin_addr, resolved->h_addr_list[0],
                static_cast<size_t>(resolved->h_length));
  }

  struct timeval connectTimeout {};
  connectTimeout.tv_sec = kConnectTimeoutSec;
  setsockopt(socketFd_, SOL_SOCKET, SO_SNDTIMEO, &connectTimeout,
             sizeof(connectTimeout));

  if (connect(socketFd_, reinterpret_cast<struct sockaddr*>(&address),
              sizeof(address)) < 0) {
    if (timeoutCallback_) timeoutCallback_();
    closeConnection();
    running_ = false;
    return;
  }

  struct timeval ioTimeout {};
  ioTimeout.tv_sec = 0;
  ioTimeout.tv_usec = 200 * 1000;
  setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO, &ioTimeout, sizeof(ioTimeout));

  sslContext_ = SSL_CTX_new(TLS_client_method());
  if (!sslContext_ ||
      !LoadCertificateIntoContext(sslContext_,
                                   certificateManager_.localCertificate())) {
    if (errorCallback_) errorCallback_("failed to load client certificate");
    closeConnection();
    running_ = false;
    return;
  }
  SSL_CTX_set_verify(sslContext_, SSL_VERIFY_NONE, nullptr);

  ssl_ = SSL_new(sslContext_);
  SSL_set_fd(ssl_, socketFd_);

  if (SSL_connect(ssl_) != 1) {
    if (errorCallback_) errorCallback_("TLS handshake failed");
    closeConnection();
    running_ = false;
    return;
  }

  X509* peerCert = SSL_get_peer_certificate(ssl_);
  peerFingerprint_ = ComputeFingerprint(peerCert);
  if (peerCert) X509_free(peerCert);

  if (!sendFramed(protocol::ToJson(request))) {
    if (errorCallback_) errorCallback_("failed to send connect request");
    closeConnection();
    running_ = false;
    return;
  }

  char buffer[4096];
  auto lastKeepAlive = std::chrono::steady_clock::now();
  uint64_t keepAliveSequence = 0;
  int missedKeepAlives = 0;
  bool awaitingResponse = true;
  const auto requestStart = std::chrono::steady_clock::now();

  while (running_) {
    const auto now = std::chrono::steady_clock::now();

    if (awaitingResponse &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - requestStart)
                .count() >= protocol::kConnectRequestTimeoutMs) {
      if (timeoutCallback_) timeoutCallback_();
      break;
    }

    if (sessionActive_) {
      const auto sinceKeepAlive =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - lastKeepAlive)
              .count();
      if (sinceKeepAlive >= protocol::kKeepaliveIntervalMs) {
        if (missedKeepAlives >= protocol::kKeepaliveMissedLimit) {
          if (connectionLostCallback_) connectionLostCallback_();
          break;
        }
        protocol::KeepAlive keepAlive;
        keepAlive.sequence = ++keepAliveSequence;
        sendFramed(protocol::ToJson(keepAlive));
        ++missedKeepAlives;
        lastKeepAlive = now;
      }
    }

    const int received = SSL_read(ssl_, buffer, sizeof(buffer));
    if (received > 0) {
      framer_.Feed(buffer, static_cast<size_t>(received));
      while (auto message = framer_.NextMessage()) {
        const auto type = protocol::PeekMessageType(*message);

        if (type == protocol::ControlMessageType::ConnectResponse) {
          auto response = protocol::ParseConnectResponse(*message);
          if (response && response->requestId == pendingRequestId_) {
            awaitingResponse = false;
            if (response->accepted) {
              sessionActive_ = true;
              lastKeepAlive = now;
            }
            if (responseCallback_) responseCallback_(*response);
          }
        } else if (type == protocol::ControlMessageType::KeepAliveAck) {
          missedKeepAlives = 0;
        } else if (type == protocol::ControlMessageType::Disconnect) {
          auto disconnect = protocol::ParseDisconnect(*message);
          sessionActive_ = false;
          if (remoteDisconnectCallback_) {
            remoteDisconnectCallback_(
                disconnect ? disconnect->reason
                           : protocol::DisconnectReason::RemoteShutdown);
          }
          running_ = false;
        }
      }
    } else {
      const int sslError = SSL_get_error(ssl_, received);
      if (sslError != SSL_ERROR_WANT_READ && sslError != SSL_ERROR_WANT_WRITE) {
        if (sessionActive_ && connectionLostCallback_) connectionLostCallback_();
        break;
      }
    }
  }

  sessionActive_ = false;
  closeConnection();
}

}  // namespace wiremic::android
