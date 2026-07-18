#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome::pbhub {

enum class HubState : uint8_t {
  UNVERIFIED,
  RECOVERING,
  READY,
  UNSUPPORTED,
};

enum class FirmwareProbeResult : uint8_t {
  TRANSPORT_FAILURE,
  SUPPORTED,
  UNSUPPORTED,
};

class PbHubRecoveryClient {
 public:
  virtual ~PbHubRecoveryClient() = default;
  virtual void invalidate_applied_state() = 0;
  virtual bool restore_configuration() = 0;
  virtual bool replay_state() = 0;
};

class PbHubRecoveryBackend {
 public:
  virtual ~PbHubRecoveryBackend() = default;
  virtual FirmwareProbeResult probe_firmware() = 0;
  virtual bool restore_global_configuration() = 0;
};

class PbHubRecoveryCoordinator {
 public:
  bool register_client(PbHubRecoveryClient *client) {
    if (client == nullptr || this->state_ != HubState::UNVERIFIED)
      return false;
    for (auto *registered : this->clients_) {
      if (registered == client)
        return false;
    }
    this->clients_.push_back(client);
    return true;
  }

  HubState state() const { return this->state_; }
  bool is_ready() const { return this->state_ == HubState::READY; }
  size_t client_count() const { return this->clients_.size(); }

  void transport_failed() {
    if (this->state_ == HubState::UNSUPPORTED)
      return;
    this->state_ = HubState::UNVERIFIED;
    this->invalidate_clients_();
  }

  bool attempt_recovery(PbHubRecoveryBackend &backend) {
    if (this->state_ == HubState::READY)
      return true;
    if (this->state_ == HubState::UNSUPPORTED)
      return false;

    this->state_ = HubState::RECOVERING;
    this->invalidate_clients_();

    const auto probe = backend.probe_firmware();
    if (this->state_ != HubState::RECOVERING)
      return this->fail_recovery_();
    if (probe == FirmwareProbeResult::UNSUPPORTED) {
      this->state_ = HubState::UNSUPPORTED;
      return false;
    }
    if (probe != FirmwareProbeResult::SUPPORTED)
      return this->fail_recovery_();

    if (!backend.restore_global_configuration() || this->state_ != HubState::RECOVERING)
      return this->fail_recovery_();

    for (auto *client : this->clients_) {
      if (!client->restore_configuration() || this->state_ != HubState::RECOVERING)
        return this->fail_recovery_();
    }
    for (auto *client : this->clients_) {
      if (!client->replay_state() || this->state_ != HubState::RECOVERING)
        return this->fail_recovery_();
    }

    this->state_ = HubState::READY;
    return true;
  }

 private:
  void invalidate_clients_() {
    for (auto *client : this->clients_)
      client->invalidate_applied_state();
  }

  bool fail_recovery_() {
    this->state_ = HubState::UNVERIFIED;
    this->invalidate_clients_();
    return false;
  }

  HubState state_{HubState::UNVERIFIED};
  std::vector<PbHubRecoveryClient *> clients_;
};

}  // namespace esphome::pbhub
