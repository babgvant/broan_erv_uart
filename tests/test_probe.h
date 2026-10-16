#pragma once

struct ProbeFixture {
  BroanComponent component;
  GPIOPin direction;
  text_sensor::TextSensor status;
  sensor::Sensor airflow;
  uint8_t source_address;
  bool uart_direction;
  explicit ProbeFixture(uint32_t now = 100, bool shared = false, bool tolerant = false, bool uart = false)
      : source_address(shared ? 0x12 : 0x13), uart_direction(uart) {
    test_now = now;
    component.set_experimental_query(true);
    component.set_query_source_address(source_address);
    component.set_query_allow_shared_address(shared);
    component.set_query_tolerant_mode(tolerant);
    component.set_query_uart_direction(uart);
    if (!uart) component.set_flow_control_pin(&direction);
    component.set_query_status_text_sensor(&status);
    component.set_supply_cfm_sensor(&airflow);
    component.setup();
    component.loop();
  }
  void handoff() {
    const uint8_t sender = source_address == 0x12 ? 0x10 : 0x12;
    const uint8_t target = source_address == 0x12 ? 0x12 : 0x10;
    receive(component, sender, target, {0x04});
    receive(component, target, sender, {0x05});
  }
  void send(uint16_t opcode = 0x0510) {
    const unsigned starts = test_fast_loop_starts, stops = test_fast_loop_stops;
    assert(component.request_probe(opcode));
    assert(test_fast_loop_starts == starts + 1);
    handoff();
    test_now += 5;
    component.loop();
    assert(component.tx == frame(source_address, 0x10,
        {0x20, static_cast<uint8_t>(opcode >> 8), static_cast<uint8_t>(opcode)}));
    assert(component.flushes == 1 && direction.writes == (uart_direction ? 0 : 3));
    assert(test_fast_loop_stops == stops + 1);
  }
};

void run_probe_tests() {
  {
    ProbeFixture f;
    sensor::Sensor rpm;
    f.component.set_supply_rpm_sensor(&rpm);
    f.send(0x0310);
    receive(f.component, 0x10, 0x12, {0x04});
    receive(f.component, 0x12, 0x10, {0x05});
    receive(f.component, 0x12, 0x10, {0x20, 0x09, 0x50});
    test_now += 100;
    receive(f.component, 0x10, 0x13, value(0x03, 0x10, 1200));
    assert(f.status.state == "Reply received; observing bus recovery");
    assert(rpm.state == 1200 && std::isnan(f.airflow.state));
    assert(f.component.tx == frame(0x13, 0x10, {0x20, 0x03, 0x10}));
  }
  {
    ProbeFixture f;
    f.send();
    receive(f.component, 0x10, 0x12, {0x21, 0x09, 0x50, 4, 0});
    assert(f.status.state.find("Locked: other traffic") == 0);
  }
  {
    ProbeFixture f;
    f.send();
    test_now += 20;
    receive(f.component, 0x10, 0x12, {0x04});
    receive(f.component, 0x12, 0x10, {0x05});
    receive(f.component, 0x12, 0x10, {0x20, 0x12, 0, 0x1A, 0, 0x01, 0xE0, 0x0A, 0xF0});
    assert(f.status.state == "Read transmitted; waiting for response");
    test_now += 118;
    receive(f.component, 0x10, 0x13, {0x21, 0x05, 0x10, 4, 0x18, 1, 0x34, 0x42});
    assert(f.status.state == "Reply received; observing bus recovery");
    assert(std::abs(f.airflow.state - 45.001068f) < 0.0001f);
    std::vector<uint8_t> normal{0x21, 0x0A, 0xF0, 120};
    normal.resize(124, 0);
    const auto temperature = value(0x01, 0xE0, 8.388311f);
    normal.insert(normal.end(), temperature.begin() + 1, temperature.end());
    normal.insert(normal.end(), {0x1A, 0, 4, 0, 0, 0, 0, 0x12, 0, 4, 255, 255, 255, 255});
    receive(f.component, 0x10, 0x12, normal);
    test_now += 1000;
    receive(f.component, 0x10, 0x12, {0x04});
    assert(f.status.state == "Reply received; bus traffic resumed; 60s cooldown");
    assert(f.component.tx.size() == 10 && f.component.flushes == 1);
  }
  {
    ProbeFixture f;
    f.send();
    receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x50, 0});
    receive(f.component, 0x10, 0x12, {0x41, 0x00, 0x50});
    test_now += 499;
    receive(f.component, 0x10, 0x12, {0x04});
    assert(f.status.state == "Read transmitted; waiting for response");
    test_now += 1;
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    assert(f.status.state.find("Locked: probe response timeout") == 0);
    assert(!f.component.request_probe(0x0510) && f.component.tx.size() == 10);
  }
  {
    ProbeFixture f;
    f.send();
    receive(f.component, 0x10, 0x14, value(0x05, 0x10, 45));
    assert(f.status.state.find("Locked: other traffic") == 0);
  }
  {
    ProbeFixture f;
    f.send();
    receive(f.component, 0x13, 0x10, {0x20, 0x04, 0x10});
    assert(f.status.state.find("Locked: probe source address already in use") == 0);
  }
  {
    ProbeFixture f;
    f.component.set_query_source_address(0x12);
    assert(!f.component.request_probe(0x0510) && f.component.tx.empty());
  }
  {
    ProbeFixture f;
    f.component.set_query_allow_shared_address(true);
    assert(!f.component.request_probe(0x0510) && f.component.tx.empty());
  }
  {
    ProbeFixture f(100, true);
    receive(f.component, 0x12, 0x10, {0x20, 0x09, 0x50});
    assert(f.component.request_probe(0x0510));
    receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x50, 0});
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(f.status.state.find("Locked: overlapping Touch transactions") == 0);
    assert(f.component.tx.empty() && !f.component.request_probe(0x0510));
  }
  {
    ProbeFixture f(100, true);
    receive(f.component, 0x12, 0x10, {0x20, 0x09, 0x50, 0x08, 0x50});
    assert(f.component.request_probe(0x0510));
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(f.component.tx.empty());
    receive(f.component, 0x10, 0x12, {0x21, 0x09, 0x50, 4, 40, 0, 0, 0});
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(f.component.tx.empty());
    receive(f.component, 0x10, 0x12, {0x21, 0x08, 0x50, 1, 1, 0x09, 0x50, 4, 40, 0, 0, 0});
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(f.component.tx == frame(0x12, 0x10, {0x20, 0x05, 0x10}));
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    assert(f.status.state == "Reply received; observing bus recovery" && f.airflow.state == 45);
    test_now += 600;
    receive(f.component, 0x12, 0x10, {0x20, 0x09, 0x50});
    receive(f.component, 0x10, 0x12, {0x21, 0x08, 0x50, 1, 1});
    test_now += 400;
    f.component.loop();
    assert(f.status.state.find("Locked: unmatched shared-address reply") == 0);
  }
  {
    ProbeFixture f(100, true);
    receive(f.component, 0x12, 0x10, {
      0x40, 0x04, 0x50, 4, 0, 0, 0x70, 0x42, 0x05, 0x50, 4, 0, 0, 0xB8, 0x41});
    receive(f.component, 0x10, 0x12, {0x41, 0x05, 0x50, 0x04, 0x50});
    f.send();
    assert(f.component.tx.size() == 10);
  }
  {
    ProbeFixture f(100, true);
    receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x50, 0});
    assert(f.component.request_probe(0x0510));
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(f.component.tx.empty());
    receive(f.component, 0x10, 0x12, {0x41, 0x01, 0x30});
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(f.component.tx.empty());
    receive(f.component, 0x10, 0x12, {0x41, 0x00, 0x50});
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(f.component.tx == frame(0x12, 0x10, {0x20, 0x05, 0x10}));
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    test_now += 4600;
    receive(f.component, 0x12, 0x10, {0x20, 0x09, 0x50});
    receive(f.component, 0x10, 0x12, {0x21, 0x09, 0x50, 4, 40, 0, 0, 0});
    test_now += 400;
    f.component.loop();
    assert(f.status.state == "Shared-address reply received; Touch recovered; 60s cooldown");
    test_now += 60000;
    assert(f.component.request_probe(0x0310) && f.component.tx.size() == 10);
  }
  {
    ProbeFixture f(100, true);
    f.send();
    receive(f.component, 0x12, 0x10, {0x20, 0x05, 0x10});
    assert(f.status.state.find("Locked: Touch requested probe register") == 0);
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    assert(f.status.state.find("Locked: Touch requested probe register") == 0 && f.component.tx.size() == 10);
  }
  {
    ProbeFixture f(100, true);
    f.send();
    receive(f.component, 0x10, 0x12, {0x04});
    assert(f.status.state == "Read transmitted; waiting for response" && f.component.flushes == 1);
  }
  {
    ProbeFixture f(100, true);
    f.send();
    test_now += 500;
    f.component.loop();
    assert(f.status.state.find("Locked: probe response timeout") == 0);
  }
  {
    Fixture f;
    assert(!f.component.request_probe(0x0510));
    f.component.set_experimental_query(true);
    assert(!f.component.request_probe(0x0510));
    receive(f.component, 0x10, 0x12, {0x04});
    test_now += 100000;
    f.component.loop();
    assert(f.component.tx.empty() && f.direction.writes == 0);
  }
  {
    ProbeFixture f;
    for (auto opcode : {0x0020, 0x0050, 0x0130, 0x0222, 0xFFFF})
      assert(!f.component.request_probe(opcode));
    f.component.setFanMode("max");
    f.component.setFanSpeed(50);
    f.component.setFanSpeedCFM(BroanFanMode::Max, BroanCFMMode::Both, 100);
    f.component.resetFilter();
    f.component.setHumidityControl(true);
    f.component.setHumiditySetpoint(60);
    f.component.setCurrentHumidity(80);
    f.component.setIntermittentPeriod(900);
    receive(f.component, 0x10, 0x12, {0x02, 'P', 'i', 'n', 'g'});
    receive(f.component, 0x10, 0x12, {0x04});
    test_now += 60001;
    f.component.loop();
    assert(f.component.tx.empty() && f.component.flushes == 0 && f.direction.writes == 1);
    assert(f.component.request_probe(0x0510));
    assert(!f.component.request_probe(0x0310));
    receive(f.component, 0x10, 0x12, {0x05});
    test_now += 5;
    f.component.loop();
    assert(f.component.tx.empty());
    f.handoff();
    test_now += 4;
    f.component.loop();
    assert(f.component.tx.empty());
    test_now += 1;
    f.component.loop();
    assert(f.component.tx == frame(0x13, 0x10, {0x20, 0x05, 0x10}));
    receive(f.component, 0x13, 0x10, {0x20, 0x05, 0x10});
    test_now += 2;
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    assert(f.airflow.state == 45 && f.status.state == "Reply received; observing bus recovery");
    test_now += 600;
    receive(f.component, 0x12, 0x10, {0x20, 0x05, 0x10});
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    test_now += 400;
    f.component.loop();
    assert(f.status.state == "Reply received; bus traffic resumed; 60s cooldown");
    assert(!f.component.request_probe(0x0310));
    test_now += 60000;
    assert(f.component.request_probe(0x0310));
    assert(f.component.tx.size() == 10 && f.component.flushes == 1);
  }
  {
    ProbeFixture f;
    const unsigned stops = test_fast_loop_stops;
    assert(f.component.request_probe(0x0510));
    f.handoff();
    receive(f.component, 0x10, 0x12, {0x04});
    test_now += 5;
    f.component.loop();
    assert(f.component.tx.empty());
    f.handoff();
    test_now += 16;
    f.component.loop();
    assert(f.component.tx.empty());
    test_now += 30000;
    f.component.loop();
    assert(f.status.state == "No eligible handoff in 30s; nothing transmitted");
    assert(test_fast_loop_stops == stops + 1);
    assert(f.component.request_probe(0x0510));
  }
  {
    ProbeFixture f;
    const unsigned stops = test_fast_loop_stops;
    assert(f.component.request_probe(0x0510));
    f.handoff();
    enqueue(f.component, {1, 0x12});
    test_now += 5;
    f.component.loop();
    test_now += 5;
    f.component.loop();
    assert(f.component.tx.empty());
    test_now += 251;
    f.component.loop();
    assert(f.status.state.find("Locked: framing errors") == 0);
    assert(test_fast_loop_stops == stops + 1);
    assert(!f.component.request_probe(0x0510));
  }
  {
    ProbeFixture f;
    receive(f.component, 0x10, 0x13, {0x04});
    assert(f.status.state.find("Locked: unexpected traffic to probe address") == 0);
    assert(!f.component.request_probe(0x0510) && f.component.tx.empty());
  }
  {
    ProbeFixture f;
    receive(f.component, 0x13, 0x10, {0x20, 0x05, 0x10});
    assert(f.status.state.find("Locked: probe source address already in use") == 0);
    assert(!f.component.request_probe(0x0510) && f.component.tx.empty());
  }
  {
    ProbeFixture f;
    f.send();
    test_now += 500;
    f.component.loop();
    assert(f.status.state.find("Locked: probe response timeout") == 0);
    receive(f.component, 0x10, 0x12, {0x04});
    f.handoff();
    test_now += 60000;
    f.component.loop();
    assert(!f.component.request_probe(0x0510) && f.component.tx.size() == 10);
  }
  for (auto target : {0x12, 0x13}) {
    ProbeFixture f;
    f.send();
    receive(f.component, 0x10, target, value(0x06, 0x10, 45));
    if (target == 0x12) assert(f.status.state == "Read transmitted; waiting for response");
    else assert(f.status.state.find("Locked: other traffic") == 0);
    assert(!f.component.request_probe(0x0510));
  }
  {
    ProbeFixture f;
    f.send();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    assert(f.status.state == "Read transmitted; waiting for response");
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    assert(f.status.state == "Reply received; observing bus recovery");
    assert(f.component.tx.size() == 10);
  }
  {
    ProbeFixture f;
    f.send();
    receive(f.component, 0x10, 0x13, {0x21, 0x05, 0x10, 1, 0});
    assert(f.status.state.find("Locked: other traffic") == 0);
  }
  {
    ProbeFixture f;
    f.send();
    auto bad = frame(0x10, 0x13, value(0x05, 0x10, 45));
    bad[bad.size() - 2] ^= 0x80;
    enqueue(f.component, bad);
    f.component.loop();
    assert(f.status.state.find("Locked: framing errors") == 0);
  }
  {
    ProbeFixture f;
    f.send();
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    test_now += 1000;
    f.component.loop();
    assert(f.status.state.find("Locked: bus did not resume") == 0);
  }
  {
    ProbeFixture f(std::numeric_limits<uint32_t>::max() - 3);
    f.send();
    test_now += 2;
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    test_now += 600;
    receive(f.component, 0x12, 0x10, {0x20, 0x05, 0x10});
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    test_now += 400;
    f.component.loop();
    assert(f.status.state == "Reply received; bus traffic resumed; 60s cooldown");
    assert(f.airflow.state == 45 && f.component.tx.size() == 10);
  }
}
