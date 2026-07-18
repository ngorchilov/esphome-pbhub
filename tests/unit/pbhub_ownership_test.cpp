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

    CHECK(!registry.claim(endpoint, EndpointOwner::ADC, "adc"));
    CHECK(registry.has_conflict());
    CHECK(registry.owns(endpoint, EndpointOwner::PWM));
    CHECK(!registry.owns(endpoint, EndpointOwner::ADC));
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
    CHECK(second.claim(endpoint, EndpointOwner::ADC, "second"));
    CHECK(first.owns(endpoint, EndpointOwner::PWM));
    CHECK(!first.owns(endpoint, EndpointOwner::ADC));
    CHECK(second.owns(endpoint, EndpointOwner::ADC));
    CHECK(!second.owns(endpoint, EndpointOwner::PWM));
    CHECK(!first.has_conflict());
    CHECK(!second.has_conflict());
  }

  {
    constexpr std::array<const char *, protocol::ENDPOINT_COUNT> OWNER_IDS{{
        "endpoint-0", "endpoint-1", "endpoint-10", "endpoint-11", "endpoint-20", "endpoint-21",
        "endpoint-30", "endpoint-31", "endpoint-40", "endpoint-41", "endpoint-50", "endpoint-51",
    }};
    EndpointClaimRegistry registry;
    size_t owner_index = 0;
    for (uint8_t channel = 0; channel < protocol::CHANNEL_COUNT; channel++) {
      for (uint8_t index = 0; index < 2; index++) {
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
