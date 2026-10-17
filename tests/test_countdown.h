#pragma once

void run_countdown_tests() {
  for (uint8_t reg : {0x03, 0x04}) {
    Fixture f;
    sensor::Sensor countdown;
    if (reg == 3) f.component.set_bath_boost_remaining_sensor(&countdown);
    else f.component.set_turbo_remaining_sensor(&countdown);
    const auto packet = [reg](uint32_t seconds) {
      return std::vector<uint8_t>{0x21, reg, 0x30, 4,
          static_cast<uint8_t>(seconds), static_cast<uint8_t>(seconds >> 8),
          static_cast<uint8_t>(seconds >> 16), static_cast<uint8_t>(seconds >> 24)};
    };
    assert(std::isnan(countdown.state));
    receive(f.component, 0x12, 0x10, packet(1200));
    assert(std::isnan(countdown.state));
    for (uint32_t seconds : {1200U, 1199U, 1198U, 0U, 86400U}) {
      receive(f.component, 0x10, 0x12, packet(seconds));
      assert(countdown.state == seconds);
    }
    for (uint32_t seconds : {86401U, 4294966U, UINT32_MAX}) {
      auto invalid = packet(seconds);
      invalid.insert(invalid.end(), {0x02, 0x20, 1, 9});
      receive(f.component, 0x10, 0x12, invalid);
      assert(std::isnan(countdown.state));
      assert(f.component.m_vecFields[BaseMode].m_value.m_nValue == 9);
      receive(f.component, 0x10, 0x12, packet(60));
      assert(countdown.state == 60);
    }
    test_now += 999;
    f.component.loop();
    assert(countdown.state == 60);
    test_now += 1;
    f.component.loop();
    assert(std::isnan(countdown.state));
    f.component.set_sensor_timeout(600000);
    test_now = UINT32_MAX - 100;
    receive(f.component, 0x10, 0x12, packet(1200));
    test_now += 9999;
    f.component.loop();
    assert(countdown.state == 1200);
    test_now += 1;
    f.component.loop();
    assert(std::isnan(countdown.state));
    receive(f.component, 0x10, 0x12, packet(0));
    assert(countdown.state == 0);
    receive(f.component, 0x10, 0x12, {0x21, reg, 0x30, 1, 99});
    assert(countdown.state == 0);
    assert(f.component.tx.empty());
  }
}
