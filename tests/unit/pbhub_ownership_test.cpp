#include <array>
#include <cstring>
#include <iostream>

#include "components/pbhub/pbhub_ownership.h"

using namespace esphome::pbhub;

static int failures = 0;

#define CHECK(...)                                                                                                     \
  do {                                                                                                                 \
    if (!(__VA_ARGS__)) {                                                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #__VA_ARGS__ << '\n';                           \
      failures++;                                                                                                      \
    }                                                                                                                  \
  } while (false)

int main() {
  const protocol::Endpoint endpoint{2, 1};
  const protocol::Endpoint signal_a{2, 0};

  CHECK(can_own_endpoint(signal_a, EndpointOwner::ADC));
  CHECK(!can_own_endpoint(endpoint, EndpointOwner::ADC));
  CHECK(!can_own_endpoint(signal_a, EndpointOwner::RGB));
  CHECK(can_own_endpoint(endpoint, EndpointOwner::RGB));
  for (const auto owner : {EndpointOwner::PWM, EndpointOwner::SERVO, EndpointOwner::DIGITAL_INPUT,
                           EndpointOwner::DIGITAL_OUTPUT}) {
    CHECK(can_own_endpoint(signal_a, owner));
    CHECK(can_own_endpoint(endpoint, owner));
  }
  CHECK(!can_own_endpoint(signal_a, EndpointOwner::NONE));
  CHECK(!can_own_endpoint({6, 0}, EndpointOwner::PWM));

  {
    EndpointClaimRegistry registry;
    CHECK(!registry.has_conflict());
    CHECK(registry.find(endpoint) == nullptr);
    CHECK(!registry.owns(endpoint, EndpointOwner::PWM));

    CHECK(registry.claim(endpoint, EndpointOwner::PWM, "pwm"));
    CHECK(!registry.has_conflict());
    CHECK(registry.owns(endpoint, EndpointOwner::PWM));
    CHECK(!registry.owns(endpoint, EndpointOwner::ADC));
    CHECK(!registry.owns(endpoint, EndpointOwner::NONE));

    const auto *claim = registry.find(endpoint);
    CHECK(claim != nullptr);
    CHECK(claim != nullptr && claim->owner == EndpointOwner::PWM);
    CHECK(claim != nullptr && std::strcmp(claim->owner_id, "pwm") == 0);

    CHECK(!registry.claim(endpoint, EndpointOwner::SERVO, "servo"));
    CHECK(registry.has_conflict());
    CHECK(registry.owns(endpoint, EndpointOwner::PWM));
    CHECK(!registry.owns(endpoint, EndpointOwner::SERVO));
    claim = registry.find(endpoint);
    CHECK(claim != nullptr && claim->owner == EndpointOwner::PWM);
    CHECK(claim != nullptr && std::strcmp(claim->owner_id, "pwm") == 0);
  }

  {
    EndpointClaimRegistry registry;
    CHECK(!registry.claim({6, 0}, EndpointOwner::PWM, "invalid-channel"));
    CHECK(!registry.claim({0, 2}, EndpointOwner::PWM, "invalid-index"));
    CHECK(registry.has_conflict());
    CHECK(registry.find({6, 0}) == nullptr);
    CHECK(registry.find({0, 2}) == nullptr);
    CHECK(!registry.owns({6, 0}, EndpointOwner::PWM));
    CHECK(!registry.owns({0, 2}, EndpointOwner::PWM));
  }

  {
    EndpointClaimRegistry registry;
    CHECK(!registry.claim(endpoint, EndpointOwner::NONE, "none"));
    CHECK(registry.has_conflict());
    CHECK(registry.find(endpoint) == nullptr);
  }

  {
    EndpointClaimRegistry registry;
    CHECK(!registry.claim(endpoint, EndpointOwner::PWM, nullptr));
    CHECK(registry.has_conflict());
    CHECK(registry.find(endpoint) == nullptr);
  }

  {
    EndpointClaimRegistry registry;
    CHECK(!registry.claim(endpoint, static_cast<EndpointOwner>(0xFF), "unknown"));
    CHECK(registry.has_conflict());
    CHECK(registry.find(endpoint) == nullptr);
  }

  {
    EndpointClaimRegistry first;
    EndpointClaimRegistry second;
    CHECK(first.claim(endpoint, EndpointOwner::PWM, "first"));
    CHECK(second.claim(signal_a, EndpointOwner::ADC, "second"));
    CHECK(first.owns(endpoint, EndpointOwner::PWM));
    CHECK(!first.owns(endpoint, EndpointOwner::ADC));
    CHECK(second.owns(signal_a, EndpointOwner::ADC));
    CHECK(!second.owns(signal_a, EndpointOwner::PWM));
    CHECK(!first.has_conflict());
    CHECK(!second.has_conflict());
  }

  {
    EndpointClaimRegistry adc_registry;
    EndpointClaimRegistry rgb_registry;
    CHECK(!adc_registry.claim(endpoint, EndpointOwner::ADC, "adc-on-b"));
    CHECK(!rgb_registry.claim(signal_a, EndpointOwner::RGB, "rgb-on-a"));
    CHECK(adc_registry.has_conflict());
    CHECK(rgb_registry.has_conflict());
    CHECK(adc_registry.find(endpoint) == nullptr);
    CHECK(rgb_registry.find(signal_a) == nullptr);
  }

  {
    constexpr std::array<const char *, protocol::ENDPOINT_COUNT> OWNER_IDS{{
        "channel-0-signal-a", "channel-0-signal-b", "channel-1-signal-a", "channel-1-signal-b",
        "channel-2-signal-a", "channel-2-signal-b", "channel-3-signal-a", "channel-3-signal-b",
        "channel-4-signal-a", "channel-4-signal-b", "channel-5-signal-a", "channel-5-signal-b",
    }};
    EndpointClaimRegistry registry;
    size_t owner_index = 0;
    for (uint8_t channel = 0; channel < protocol::CHANNEL_COUNT; channel++) {
      for (uint8_t index = 0; index < protocol::SIGNAL_COUNT; index++) {
        const protocol::Endpoint current{channel, index};
        CHECK(registry.claim(current, EndpointOwner::DIGITAL_OUTPUT, OWNER_IDS[owner_index++]));
        CHECK(registry.owns(current, EndpointOwner::DIGITAL_OUTPUT));
        CHECK(!registry.owns(current, EndpointOwner::DIGITAL_INPUT));
      }
    }
    CHECK(owner_index == protocol::ENDPOINT_COUNT);
    CHECK(!registry.has_conflict());
  }

  if (failures != 0)
    std::cerr << failures << " ownership checks failed\n";
  return failures == 0 ? 0 : 1;
}
