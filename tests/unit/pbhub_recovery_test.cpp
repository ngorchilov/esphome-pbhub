#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "components/pbhub/pbhub_recovery.h"

using namespace esphome::pbhub;

static int failures = 0;

#define CHECK(...)                                                                                                     \
  do {                                                                                                                 \
    if (!(__VA_ARGS__)) {                                                                                              \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #__VA_ARGS__ << '\n';                           \
      failures++;                                                                                                      \
    }                                                                                                                  \
  } while (false)

struct FakeBackend : PbHubRecoveryBackend {
  explicit FakeBackend(std::vector<std::string> &events) : events(events) {}

  FirmwareProbeResult probe_firmware() override {
    this->events.push_back("probe");
    return this->probe_result;
  }

  bool restore_global_configuration() override {
    this->events.push_back("global");
    return this->global_result;
  }

  std::vector<std::string> &events;
  FirmwareProbeResult probe_result{FirmwareProbeResult::SUPPORTED};
  bool global_result{true};
};

struct FakeClient : PbHubRecoveryClient {
  FakeClient(std::string name, std::vector<std::string> &events) : name(std::move(name)), events(events) {}

  void invalidate_applied_state() override {
    this->events.push_back("invalidate " + this->name);
    this->invalidations++;
  }

  bool restore_configuration() override {
    this->events.push_back("config " + this->name);
    if (this->transport_failure_during_configuration && this->coordinator != nullptr)
      this->coordinator->transport_failed();
    return this->configuration_result;
  }

  bool replay_state() override {
    this->events.push_back("replay " + this->name);
    if (this->transport_failure_during_replay && this->coordinator != nullptr)
      this->coordinator->transport_failed();
    return this->replay_result;
  }

  void recovery_complete() override {
    this->events.push_back("complete " + this->name);
    this->completions++;
    if (this->transport_failure_during_completion && this->coordinator != nullptr)
      this->coordinator->transport_failed();
  }

  std::string name;
  std::vector<std::string> &events;
  bool configuration_result{true};
  bool replay_result{true};
  PbHubRecoveryCoordinator *coordinator{nullptr};
  bool transport_failure_during_configuration{false};
  bool transport_failure_during_replay{false};
  bool transport_failure_during_completion{false};
  int invalidations{0};
  int completions{0};
};

static size_t event_index(const std::vector<std::string> &events, const std::string &event) {
  for (size_t i = 0; i < events.size(); i++) {
    if (events[i] == event)
      return i;
  }
  return events.size();
}

int main() {
  {
    std::vector<std::string> events;
    PbHubRecoveryCoordinator coordinator;
    FakeBackend backend(events);
    FakeClient first("first", events);
    FakeClient second("second", events);

    CHECK(coordinator.state() == HubState::UNVERIFIED);
    CHECK(!coordinator.register_client(nullptr));
    CHECK(coordinator.register_client(&first));
    CHECK(coordinator.register_client(&second));
    CHECK(!coordinator.register_client(&first));
    CHECK(coordinator.client_count() == 2);
    CHECK(coordinator.attempt_recovery(backend));
    CHECK(coordinator.state() == HubState::READY);
    CHECK(coordinator.is_ready());
    CHECK(event_index(events, "probe") < event_index(events, "global"));
    CHECK(event_index(events, "global") < event_index(events, "config first"));
    CHECK(event_index(events, "config first") < event_index(events, "config second"));
    CHECK(event_index(events, "config second") < event_index(events, "replay first"));
    CHECK(event_index(events, "replay first") < event_index(events, "replay second"));
    CHECK(first.completions == 0);
    coordinator.notify_recovery_complete();
    CHECK(first.completions == 1);
    CHECK(second.completions == 1);
    CHECK(event_index(events, "replay second") < event_index(events, "complete first"));
    CHECK(event_index(events, "complete first") < event_index(events, "complete second"));

    const int first_invalidations = first.invalidations;
    coordinator.transport_failed();
    CHECK(coordinator.state() == HubState::UNVERIFIED);
    CHECK(!coordinator.is_ready());
    CHECK(first.invalidations == first_invalidations + 1);
    coordinator.notify_recovery_complete();
    CHECK(first.completions == 1);
  }

  {
    std::vector<std::string> events;
    PbHubRecoveryCoordinator coordinator;
    FakeBackend backend(events);
    FakeClient client("client", events);
    CHECK(coordinator.register_client(&client));
    backend.probe_result = FirmwareProbeResult::TRANSPORT_FAILURE;
    CHECK(!coordinator.attempt_recovery(backend));
    CHECK(coordinator.state() == HubState::UNVERIFIED);
    CHECK(event_index(events, "probe") < events.size());
    CHECK(event_index(events, "global") == events.size());
    CHECK(event_index(events, "config client") == events.size());
    CHECK(event_index(events, "replay client") == events.size());
  }

  {
    std::vector<std::string> events;
    PbHubRecoveryCoordinator coordinator;
    FakeBackend backend(events);
    FakeClient client("client", events);
    CHECK(coordinator.register_client(&client));
    backend.probe_result = FirmwareProbeResult::UNSUPPORTED;
    CHECK(!coordinator.attempt_recovery(backend));
    CHECK(coordinator.state() == HubState::UNSUPPORTED);
    CHECK(!coordinator.attempt_recovery(backend));
    CHECK(event_index(events, "global") == events.size());
    CHECK(event_index(events, "config client") == events.size());
  }

  {
    std::vector<std::string> events;
    PbHubRecoveryCoordinator coordinator;
    FakeBackend backend(events);
    FakeClient first("first", events);
    FakeClient second("second", events);
    first.configuration_result = false;
    CHECK(coordinator.register_client(&first));
    CHECK(coordinator.register_client(&second));
    CHECK(!coordinator.attempt_recovery(backend));
    CHECK(coordinator.state() == HubState::UNVERIFIED);
    CHECK(event_index(events, "config first") < events.size());
    CHECK(event_index(events, "config second") == events.size());
    CHECK(event_index(events, "replay first") == events.size());
    CHECK(first.invalidations >= 2);
    CHECK(second.invalidations >= 2);
  }

  {
    std::vector<std::string> events;
    PbHubRecoveryCoordinator coordinator;
    FakeBackend backend(events);
    FakeClient first("first", events);
    FakeClient second("second", events);
    first.replay_result = false;
    CHECK(coordinator.register_client(&first));
    CHECK(coordinator.register_client(&second));
    CHECK(!coordinator.attempt_recovery(backend));
    CHECK(coordinator.state() == HubState::UNVERIFIED);
    CHECK(event_index(events, "config second") < event_index(events, "replay first"));
    CHECK(event_index(events, "replay second") == events.size());
  }

  {
    std::vector<std::string> events;
    PbHubRecoveryCoordinator coordinator;
    FakeBackend backend(events);
    FakeClient client("client", events);
    client.coordinator = &coordinator;
    client.transport_failure_during_configuration = true;
    CHECK(coordinator.register_client(&client));
    CHECK(!coordinator.attempt_recovery(backend));
    CHECK(coordinator.state() == HubState::UNVERIFIED);
    CHECK(event_index(events, "config client") < events.size());
    CHECK(event_index(events, "replay client") == events.size());
  }

  {
    std::vector<std::string> events;
    PbHubRecoveryCoordinator coordinator;
    FakeBackend backend(events);
    FakeClient client("client", events);
    client.coordinator = &coordinator;
    client.transport_failure_during_replay = true;
    CHECK(coordinator.register_client(&client));
    CHECK(!coordinator.attempt_recovery(backend));
    CHECK(coordinator.state() == HubState::UNVERIFIED);
    CHECK(event_index(events, "replay client") < events.size());
  }

  {
    std::vector<std::string> events;
    PbHubRecoveryCoordinator coordinator;
    FakeBackend backend(events);
    FakeClient first("first", events);
    FakeClient second("second", events);
    first.coordinator = &coordinator;
    first.transport_failure_during_completion = true;
    CHECK(coordinator.register_client(&first));
    CHECK(coordinator.register_client(&second));
    CHECK(coordinator.attempt_recovery(backend));
    coordinator.notify_recovery_complete();
    CHECK(coordinator.state() == HubState::UNVERIFIED);
    CHECK(first.completions == 1);
    CHECK(second.completions == 0);
  }

  if (failures != 0)
    std::cerr << failures << " recovery checks failed\n";
  return failures == 0 ? 0 : 1;
}
