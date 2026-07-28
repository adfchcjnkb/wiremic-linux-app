#pragma once

#include <openssl/ssl.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "CertificateManager.hpp"
#include "MessageFraming.hpp"
#include "Protocol.hpp"

namespace wiremic::android {

class ControlClient {
 public:
  using ResponseCallback = std::function<void(protocol::ConnectResponse)>;
  using TimeoutCallback = std::function<void()>;
  using ConnectionLostCallback = std::function<void()>;
  using RemoteDisconnectCallback = std::function<void(protocol::DisconnectReason)>;
  using ErrorCallback = std::function<void(std::string)>;

  explicit ControlClient(security::CertificateManager& certificateManager);
  ~ControlClient();

  void connectToDevice(const std::string& host, uint16_t port,
                        protocol::ConnectRequest request);
  void disconnectFromDevice(
      protocol::DisconnectReason reason = protocol::DisconnectReason::UserRequested);

  void setResponseCallback(ResponseCallback callback);
  void setTimeoutCallback(TimeoutCallback callback);
  void setConnectionLostCallback(ConnectionLostCallback callback);
  void setRemoteDisconnectCallback(RemoteDisconnectCallback callback);
  void setErrorCallback(ErrorCallback callback);

  [[nodiscard]] std::string peerCertificateFingerprint() const;

 private:
  void run(std::string host, uint16_t port, protocol::ConnectRequest request);
  bool sendFramed(const std::string& json);
  void closeConnection();

  security::CertificateManager& certificateManager_;
  std::atomic<bool> running_{false};
  std::atomic<bool> sessionActive_{false};
  std::thread thread_;

  int socketFd_{-1};
  SSL_CTX* sslContext_{nullptr};
  SSL* ssl_{nullptr};
  protocol::MessageFramer framer_;
  std::string peerFingerprint_;
  std::string pendingRequestId_;

  ResponseCallback responseCallback_;
  TimeoutCallback timeoutCallback_;
  ConnectionLostCallback connectionLostCallback_;
  RemoteDisconnectCallback remoteDisconnectCallback_;
  ErrorCallback errorCallback_;
};

}
