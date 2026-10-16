#include "broan.h"
#include <cassert>
#include <iostream>
#include <limits>

using namespace esphome;
using namespace esphome::broan;

std::vector<uint8_t> frame(uint8_t sender, uint8_t target, std::vector<uint8_t> payload) {
  std::vector<uint8_t> result{1, target, sender, 1, static_cast<uint8_t>(payload.size())};
  result.insert(result.end(), payload.begin(), payload.end());
  result.push_back(packet_checksum(sender, target, payload));
  result.push_back(4);
  return result;
}
void enqueue(BroanComponent &component, const std::vector<uint8_t> &bytes) {
  component.rx.insert(component.rx.end(), bytes.begin(), bytes.end());
}
void receive(BroanComponent &component, uint8_t sender, uint8_t target, std::vector<uint8_t> payload) {
  enqueue(component, frame(sender, target, payload));
  component.loop();
}
std::vector<uint8_t> value(uint8_t high, uint8_t low, float number) {
  uint8_t bytes[4];
  std::memcpy(bytes, &number, 4);
  return {0x21, high, low, 4, bytes[0], bytes[1], bytes[2], bytes[3]};
}
struct Fixture {
  BroanComponent component;
  sensor::Sensor exhaust, supply, power, filter, numeric_fault;
  text_sensor::TextSensor fault, last_fault;
  binary_sensor::BinarySensor fault_active, communication;
  GPIOPin direction;
  Fixture() {
    test_now = 100;
    component.set_passive_mode(true);
    component.set_sensor_timeout(1000);
    component.set_communication_timeout(500);
    component.set_exhaust_rpm_sensor(&exhaust);
    component.set_supply_rpm_sensor(&supply);
    component.set_power_sensor(&power);
    component.set_filter_life_sensor(&filter);
    component.set_fault_code_sensor(&numeric_fault);
    component.set_fault_code_text_sensor(&fault);
    component.set_last_fault_text_sensor(&last_fault);
    component.set_fault_active_binary_sensor(&fault_active);
    component.set_communication_ok_binary_sensor(&communication);
    component.set_flow_control_pin(&direction);
    component.setup();
    component.loop();
  }
};
#include "test_probe.h"
#include "test_probe_tolerant.h"
#include "test_probe_timing.h"
#include "test_probe_release.h"
#include "test_probe_shared.h"
#include "test_probe_uart.h"

int main() {
  run_probe_tests();
  run_tolerant_probe_tests();
  run_probe_timing_tests();
  run_probe_release_tests();
  run_shared_probe_tests();
  run_uart_probe_tests();
  {
    Fixture f;
    sensor::Sensor humidity, minutes;
    text_sensor::TextSensor base, active;
    binary_sensor::BinarySensor enabled, available;
    f.component.set_humidity_target_sensor(&humidity);
    f.component.set_intermittent_on_time_sensor(&minutes);
    f.component.set_base_mode_text_sensor(&base);
    f.component.set_active_mode_text_sensor(&active);
    f.component.set_humidity_control_enabled_binary_sensor(&enabled);
    f.component.set_fault_data_available_binary_sensor(&available);
    f.component.loop();
    assert(available.has_state && !available.state && !enabled.has_state);
    receive(f.component, 0x10, 0x12, value(0x0C, 0x22, 60));
    receive(f.component, 0x10, 0x12, {0x21, 0x02, 0x22, 4, 0x84, 3, 0, 0, 0x0F, 0x22, 1, 1});
    assert(humidity.state == 60 && minutes.state == 15 && enabled.state);
    receive(f.component, 0x10, 0x12, {0x21, 0x02, 0x20, 1, 17, 0x07, 0x20, 1, 2});
    assert(base.state == "Smart" && active.state == "High");
    receive(f.component, 0x10, 0x12, {0x21, 0x02, 0x20, 1, 9, 0x07, 0x20, 1, 1});
    assert(base.state == "Continuous Low" && active.state == "Low");
    receive(f.component, 0x10, 0x12, {0x21, 0x02, 0x20, 1, 11, 0x07, 0x20, 1, 4});
    assert(base.state == "Continuous Medium" && active.state == "Medium");
    receive(f.component, 0x10, 0x12, {0x21, 0x02, 0x20, 1, 10, 0x07, 0x20, 1, 2});
    assert(base.state == "Continuous High" && active.state == "High");
    receive(f.component, 0x10, 0x12, {0x21, 0x02, 0x20, 1, 8, 0x07, 0x20, 1, 5});
    assert(base.state == "Intermittent" && active.state == "Bath Override");
    receive(f.component, 0x10, 0x12, {0x21, 0x02, 0x20, 1, 8, 0x07, 0x20, 1, 0});
    assert(base.state == "Intermittent" && active.state == "Idle");
    receive(f.component, 0x10, 0x12, {0x21, 0x02, 0x20, 1, 222, 0x07, 0x20, 1, 222});
    assert(base.state == "222" && active.state == "222");
    receive(f.component, 0x10, 0x12, {0x21, 0x17, 0, 4, 255, 255, 255, 255});
    assert(available.state && !f.fault_active.state);
    test_now += 1000;
    f.component.loop();
    assert(!available.state && !f.fault_active.has_state && !enabled.has_state);
    assert(std::isnan(humidity.state) && std::isnan(minutes.state));
    assert(base.state == "Unknown" && active.state == "Unknown");
    receive(f.component, 0x10, 0x12, {0x21, 0x0F, 0x22, 1, 0});
    assert(enabled.has_state && !enabled.state);
    assert(f.component.tx.empty() && f.component.flushes == 0 && f.direction.writes == 0);
  }
  {
    Fixture f;
    f.component.set_register_discovery(true);
    f.component.set_log_unknown_registers(true);
    test_logs.clear();
    receive(f.component, 0x10, 0x12, {0x21, 0x12, 0, 4, 255, 255, 255, 255});
    assert(test_logs.back().find("unmapped len=4 raw=FF.FF.FF.FF") != std::string::npos);
    const size_t first_count = test_logs.size();
    test_now += 100;
    receive(f.component, 0x10, 0x12, {0x21, 0x12, 0, 4, 255, 255, 255, 255});
    assert(test_logs.size() == first_count && f.component.get_unknown_registers() == 2);
    test_now += 200;
    receive(f.component, 0x10, 0x12, {0x21, 0x12, 0, 1, 0});
    assert(test_logs.back().find("unmapped len=1 raw=00") != std::string::npos);
    receive(f.component, 0x12, 0x10, {0x20, 0x17, 0});
    test_now = 60400;
    f.component.loop();
    bool summary = false, missing = false;
    for (const auto &line : test_logs) {
      summary |= line.find("Register 1200 requests=0 responses=3 len=1 age=60.0s max_gap=0.2s raw=00") != std::string::npos;
      missing |= line.find("Register 1700 requests=1 responses=0 (never observed)") != std::string::npos;
    }
    assert(summary && missing && f.component.tx.empty());
  }
  {
    Fixture f;
    sensor::Sensor base, active;
    f.component.set_base_mode_code_sensor(&base);
    f.component.set_active_mode_code_sensor(&active);
    // Four-byte modes remain supported; subsequent byte values must clear upper bytes.
    receive(f.component, 0x10, 0x12, {0x21, 0x02, 0x20, 4, 0x11, 1, 0, 0});
    assert(base.state == 273);
    // Actual VTTOUCH response from the October 15 field capture.
    receive(f.component, 0x10, 0x12, {
      0x21, 0x04, 0x30, 4, 0, 0, 0, 0, 0x03, 0x30, 4, 0, 0, 0, 0,
      0x08, 0x20, 1, 0, 0x07, 0x20, 1, 2, 0x06, 0x20, 1, 0x0A,
      0x02, 0x20, 1, 0x11, 0x00, 0x20, 1, 0x11});
    assert(base.state == 17 && active.state == 2);
    // Other malformed integer lengths still reject the entire response.
    receive(f.component, 0x10, 0x12, {
      0x21, 0x02, 0x20, 1, 9, 0x08, 0x30, 1, 0});
    assert(base.state == 17);
  }
  {
    Fixture f;
    assert(std::isnan(f.exhaust.state) && f.fault.state == "Unknown" && !f.fault_active.has_state);
    receive(f.component, 0x11, 0x10, {0x20, 0x04, 0x10});
    receive(f.component, 0x10, 0x11, value(0x04, 0x10, 1250));
    assert(f.exhaust.state == 1250);
    // A response is self describing even if its request was dropped.
    receive(f.component, 0x10, 0x19, value(0x03, 0x10, 1400));
    receive(f.component, 0x10, 0x12, value(0x23, 0x50, 42.5));
    assert(f.supply.state == 1400 && f.power.state == 42.5);
    assert(f.component.get_valid_packets() == 4);
  }
  {
    Fixture f;
    auto packet = frame(0x10, 0x11, value(0x04, 0x10, 1200));
    for (size_t i = 0; i < packet.size(); ++i) {
      enqueue(f.component, {packet[i]}); f.component.loop();
      if (i + 1 < packet.size()) assert(std::isnan(f.exhaust.state));
    }
    assert(f.exhaust.state == 1200);
    enqueue(f.component, packet); enqueue(f.component, packet); f.component.loop();
    assert(f.component.get_valid_packets() == 3);
  }
  {
    Fixture f;
    auto packet = frame(0x10, 0x11, value(0x04, 0x10, 900));
    auto bad = packet; bad[bad.size() - 2] ^= 0x80;
    enqueue(f.component, bad); enqueue(f.component, packet); f.component.loop();
    assert(f.component.get_checksum_failures() > 0 && f.exhaust.state == 900);
    receive(f.component, 0x10, 0x11, {0x21, 0xFF, 0xEE, 2, 1, 2});
    assert(f.component.get_unknown_registers() == 1);
    unsigned count = f.exhaust.updates;
    receive(f.component, 0x10, 0x11, {0x21, 0x04, 0x10, 255, 0});
    receive(f.component, 0x10, 0x11, {0x21, 0x04});
    receive(f.component, 0x10, 0x11, {0x21, 0x04, 0x10, 5, 0, 0, 0, 0, 0});
    receive(f.component, 0x10, 0x11, {0x21, 0x01, 0x00, 1, 0});
    assert(f.exhaust.updates == count);
    // Commanded values must never be published as measured values.
    auto write = value(0x04, 0x10, 700); write[0] = 0x40;
    receive(f.component, 0x11, 0x10, write);
    receive(f.component, 0x19, 0x11, value(0x04, 0x10, 800));
    assert(f.exhaust.state == 900);
  }
  {
    Fixture f;
    enqueue(f.component, {1, 0x11, 0x10, 1, 255, 0x21});
    enqueue(f.component, frame(0x10, 0x11, value(0x04, 0x10, 1100)));
    f.component.loop();
    test_now += 251; f.component.loop();
    assert(f.exhaust.state == 1100);
    receive(f.component, 0x11, 0x10, {0x20, 0x04, 0x10});
    test_now += 1100; f.component.loop();
    assert(std::isnan(f.exhaust.state));
    receive(f.component, 0x10, 0x11, value(0x04, 0x10, 1100));
    assert(f.exhaust.state == 1100);
  }
  {
    Fixture f;
    receive(f.component, 0x10, 0x11, {0x21, 0x08, 0x30, 4, 0x40, 0xFA, 0x01, 0}); // 129600 seconds
    assert(f.filter.state == 1.5f);
    receive(f.component, 0x10, 0x11, {0x21, 0x17, 0, 4, 38, 0, 0, 0});
    assert(f.fault.state == "E38" && f.numeric_fault.state == 38 && f.fault_active.state);
    std::string retained = f.last_fault.state;
    receive(f.component, 0x10, 0x11, {0x21, 0x17, 0, 4, 255, 255, 255, 255});
    assert(f.fault.state == "OK" && !f.fault_active.state && f.last_fault.state == retained);
    test_now += 1000; f.component.loop();
    assert(f.fault.state == "Unknown" && !f.fault_active.has_state && f.last_fault.state == retained);
  }
  {
    Fixture f;
    receive(f.component, 0x10, 0x11, value(0x04, 0x10, 0));
    assert(f.exhaust.state == 0);
    test_now += 499; f.component.loop(); assert(f.communication.state);
    test_now += 1; f.component.loop(); assert(!f.communication.state);
    test_now += 500;
    receive(f.component, 0x11, 0x10, {0x20, 0x23, 0x50});
    assert(f.communication.state && std::isnan(f.exhaust.state));
    receive(f.component, 0x10, 0x11, value(0x04, 0x10, 0));
    assert(f.exhaust.state == 0);
    test_now = std::numeric_limits<uint32_t>::max() - 500;
    receive(f.component, 0x10, 0x11, value(0x04, 0x10, 1234));
    test_now += 1000; f.component.loop(); assert(std::isnan(f.exhaust.state));
  }
  {
    Fixture f;
    receive(f.component, 0x10, 0x12, {0x02, 'P', 'i', 'n', 'g'});
    receive(f.component, 0x10, 0x12, {0x04});
    receive(f.component, 0x10, 0x12, {0x05});
    receive(f.component, 0x10, 0x12, {0x41, 0x00, 0x20});
    f.component.setFanMode("max"); f.component.setFanSpeed(50);
    f.component.setFanSpeedCFM(BroanFanMode::Max, BroanCFMMode::Both, 100);
    f.component.resetFilter(); f.component.setHumidityControl(true);
    f.component.setHumiditySetpoint(45); f.component.setCurrentHumidity(80);
    f.component.setIntermittentPeriod(600);
    test_now += 60001; f.component.loop();
    assert(f.component.tx.empty() && f.component.flushes == 0 && f.direction.writes == 0 && f.direction.setups == 0);
  }
  {
    BroanComponent active;
    test_now = 100;
    active.setup();
    receive(active, 0x10, 0x12, {0x02, 'P', 'i', 'n', 'g'});
    assert(active.tx == frame(0x12, 0x10, {0x03, 'P', 'i', 'n', 'g'}));
    active.tx.clear();
    active.setFanMode("max");
    receive(active, 0x10, 0x12, {0x04});
    assert(active.tx.size() > 8); // ACK followed by queued write
    active.tx.clear();
    receive(active, 0x10, 0x12, {0x41, 0x00, 0x20});
    assert(!active.tx.empty()); // poll queue remains active
  }
  std::cout << "All component regression scenarios passed\n";
}
