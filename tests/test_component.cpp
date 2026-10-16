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
int main() {
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
