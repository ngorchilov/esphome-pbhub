#pragma once

#include <array>
#include <cstdint>

#include "pbhub_protocol.h"

namespace esphome::pbhub {

enum class EndpointOwner : uint8_t {
  NONE,
  PWM,
  SERVO,
  ADC,
  RGB,
  DIGITAL_INPUT,
  DIGITAL_OUTPUT,
};

constexpr bool is_valid_endpoint_owner(EndpointOwner owner) {
  switch (owner) {
    case EndpointOwner::PWM:
    case EndpointOwner::SERVO:
    case EndpointOwner::ADC:
    case EndpointOwner::RGB:
    case EndpointOwner::DIGITAL_INPUT:
    case EndpointOwner::DIGITAL_OUTPUT:
      return true;
    case EndpointOwner::NONE:
      return false;
  }
  return false;
}

struct EndpointClaim {
  EndpointOwner owner{EndpointOwner::NONE};
  const char *owner_id{nullptr};
};

class EndpointClaimRegistry {
 public:
  bool claim(protocol::Endpoint endpoint, EndpointOwner owner, const char *owner_id) {
    uint8_t ordinal;
    if (!protocol::endpoint_ordinal(endpoint, ordinal) || !is_valid_endpoint_owner(owner) || owner_id == nullptr) {
      this->conflict_ = true;
      return false;
    }

    auto &claim = this->claims_[ordinal];
    if (claim.owner != EndpointOwner::NONE) {
      this->conflict_ = true;
      return false;
    }

    claim = {owner, owner_id};
    return true;
  }

  bool owns(protocol::Endpoint endpoint, EndpointOwner owner) const {
    if (!is_valid_endpoint_owner(owner))
      return false;
    const auto *claim = this->find(endpoint);
    return claim != nullptr && claim->owner == owner;
  }

  const EndpointClaim *find(protocol::Endpoint endpoint) const {
    uint8_t ordinal;
    if (!protocol::endpoint_ordinal(endpoint, ordinal))
      return nullptr;
    const auto &claim = this->claims_[ordinal];
    return claim.owner == EndpointOwner::NONE ? nullptr : &claim;
  }

  bool has_conflict() const { return this->conflict_; }

 protected:
  std::array<EndpointClaim, protocol::ENDPOINT_COUNT> claims_{};
  bool conflict_{false};
};

}  // namespace esphome::pbhub
