#pragma once

struct ProbeFixture {
  BroanComponent component;
  GPIOPin direction;
  text_sensor::TextSensor status;
  sensor::Sensor airflow;
  explicit ProbeFixture(uint32_t now = 100) {
    test_now = now;
    component.set_experimental_query(true);
    component.set_flow_control_pin(&direction);
    component.set_query_status_text_sensor(&status);
    component.set_supply_cfm_sensor(&airflow);
    component.setup();
    component.loop();
  }
  void handoff() {
    receive(component, 0x12, 0x10, {0x04});
    receive(component, 0x10, 0x12, {0x05});
  }
  void send() {
    const unsigned starts = test_fast_loop_starts, stops = test_fast_loop_stops;
    assert(component.request_probe(0x0510));
    assert(test_fast_loop_starts == starts + 1);
    handoff();
    test_now += 5;
    component.loop();
    assert(component.tx == frame(0x13, 0x10, {0x20, 0x05, 0x10}));
    assert(component.flushes == 1 && direction.writes == 3);
    assert(test_fast_loop_stops == stops + 1);
  }
};

void run_probe_tests() {
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
    assert(f.status.state.find("Locked: other traffic") == 0);
    assert(!f.component.request_probe(0x0510));
  }
  {
    ProbeFixture f;
    f.send();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    assert(f.status.state.find("Locked: other traffic") == 0);
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
