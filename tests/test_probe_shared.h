#pragma once

struct SharedFixture : ProbeFixture {
  SharedFixture() : ProbeFixture(100, true) {}
  void normal(uint16_t opcode = 0x0950) {
    receive(component, 0x12, 0x10, {0x20, static_cast<uint8_t>(opcode >> 8), static_cast<uint8_t>(opcode)});
    receive(component, 0x10, 0x12, value(opcode >> 8, opcode & 255, 40));
  }
  void recover() {
    for (unsigned i = 0; i < 5; ++i) {
      test_now += 1000;
      normal();
    }
  }
  void poll_send() {
    handoff();
    test_now += 5;
    component.loop();
  }
};

void run_shared_probe_tests() {
  {
    SharedFixture f;
    f.send();
    test_now += 16;
    receive(f.component, 0x10, 0x12, {0x04});
    receive(f.component, 0x12, 0x10, {0x05});
    test_now += 20;
    receive(f.component, 0x12, 0x10, {0x20, 0x04, 0x21, 0x05, 0x21, 0x06, 0x21, 0x0F, 0x22, 0x10, 0x22});
    test_now += 10;
    receive(f.component, 0x10, 0x12, {0x21, 0x05, 0x10, 4, 0xE4, 0x31, 0x35, 0x42});
    assert(f.status.state == "Reply received; observing bus recovery");
    assert(std::abs(f.airflow.state - 45.29872f) < 0.001f);
    receive(f.component, 0x10, 0x12, {0x21, 0x10, 0x22, 1, 0, 0x0F, 0x22, 1, 0,
        0x06, 0x21, 1, 0, 0x05, 0x21, 1, 0, 0x04, 0x21, 1, 0});
    f.recover();
    assert(f.status.state == "Shared-address reply received; Touch recovered; 60s cooldown");
    assert(!f.component.request_probe(0x0310));
    test_now += 60000;
    assert(f.component.request_probe(0x0310));
    f.poll_send();
    auto expected = frame(0x12, 0x10, {0x20, 0x05, 0x10});
    auto rpm = frame(0x12, 0x10, {0x20, 0x03, 0x10});
    expected.insert(expected.end(), rpm.begin(), rpm.end());
    assert(f.component.tx == expected && f.component.flushes == 2);
  }
  {
    SharedFixture f;
    f.send();
    receive(f.component, 0x12, 0x10, {0x20, 0x09, 0x50});
    receive(f.component, 0x10, 0x12, value(0x09, 0x50, 40));
    assert(f.status.state == "Read transmitted; waiting for response");
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    f.recover();
    assert(f.status.state == "Shared-address reply received; Touch recovered; 60s cooldown");
  }
  {
    SharedFixture f;
    f.send();
    receive(f.component, 0x12, 0x10, {0x20, 0x09, 0x50, 0x05, 0x10});
    assert(f.status.state.find("Locked: Touch requested probe register") == 0);
    assert(f.component.tx.size() == 10);
  }
  {
    SharedFixture f;
    f.send();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    f.normal(0x0510);
    f.recover();
    assert(f.status.state == "Shared-address reply received; Touch recovered; 60s cooldown");
  }
  {
    SharedFixture f;
    f.send();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    assert(f.status.state.find("Locked: unmatched shared-address reply") == 0);
  }
  {
    SharedFixture f;
    f.send();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    test_now += 5000;
    f.component.loop();
    assert(f.status.state.find("Locked: bus did not resume") == 0);
  }
  {
    SharedFixture f;
    assert(!f.component.start_probe_polling());
    assert(!f.component.get_probe_polling() && f.component.tx.empty());
    for (unsigned i = 0; i < 120; ++i) {
      test_now += 1000;
      f.normal();
    }
    assert(f.component.tx.empty() && !f.component.get_probe_polling());
  }
  for (uint32_t start : {100U, UINT32_MAX - 50}) {
    SharedFixture f;
    test_now = start;
    f.normal();
    assert(f.component.start_probe_polling());
    f.component.loop();
    const uint16_t registers[]{0x0510, 0x0310, 0x0610, 0x0410, 0x2350, 0x0510};
    std::vector<uint8_t> expected;
    for (uint16_t opcode : registers) {
      f.poll_send();
      auto packet = frame(0x12, 0x10, {0x20, static_cast<uint8_t>(opcode >> 8), static_cast<uint8_t>(opcode)});
      expected.insert(expected.end(), packet.begin(), packet.end());
      assert(f.component.tx == expected);
      const uint32_t sent = test_now;
      test_now += 100;
      receive(f.component, 0x10, 0x12, value(opcode >> 8, opcode & 255, 45));
      f.recover();
      assert(f.component.get_probe_polling());
      assert(f.status.state == "Telemetry polling; Touch recovered; 60s cooldown");
      while (static_cast<uint32_t>(test_now - sent) < 60000) {
        test_now += 1000;
        f.normal();
        assert(f.component.tx == expected);
      }
    }
    f.component.stop_probe_polling();
    assert(!f.component.get_probe_polling());
    assert(f.status.state == "Telemetry polling stopped; queued read cancelled");
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(f.component.tx == expected);
  }
  {
    SharedFixture f;
    f.normal();
    assert(f.component.start_probe_polling());
    f.component.loop();
    f.poll_send();
    f.component.stop_probe_polling();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    f.recover();
    assert(!f.component.get_probe_polling());
    assert(f.status.state == "Shared-address reply received; Touch recovered; 60s cooldown");
    test_now += 60000;
    f.component.loop();
    assert(f.component.tx.size() == 10);
  }
  for (unsigned failure = 0; failure < 3; ++failure) {
    SharedFixture f;
    f.normal();
    assert(f.component.start_probe_polling());
    f.component.loop();
    f.poll_send();
    if (failure == 0) {
      test_now += 500;
      f.component.loop();
    } else if (failure == 1) {
      receive(f.component, 0x12, 0x10, {0x20, 0x05, 0x10});
    } else {
      enqueue(f.component, {1, 0x22, 0x33, 0xFF, 2});
      f.component.loop();
    }
    assert(!f.component.get_probe_polling() && f.status.state.find("Locked:") == 0);
    test_now += 60000;
    f.normal();
    assert(f.component.tx.size() == 10 && !f.component.start_probe_polling());
  }
  for (bool framing : {false, true}) {
    SharedFixture f;
    f.normal();
    assert(f.component.start_probe_polling());
    f.component.loop();
    f.poll_send();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    f.recover();
    if (framing) enqueue(f.component, {1, 0x22, 0x33, 0xFF, 2});
    else test_now += 1500;
    f.component.loop();
    assert(!f.component.get_probe_polling() && f.status.state.find("Locked:") == 0);
    assert(f.component.tx.size() == 10);
  }
  {
    SharedFixture f;
    f.normal();
    assert(f.component.start_probe_polling());
    f.component.loop();
    for (unsigned i = 0; i < 30; ++i) {
      test_now += 1000;
      f.normal();
    }
    assert(!f.component.get_probe_polling() && f.component.tx.empty());
    assert(f.status.state.find("Polling stopped: no eligible handoff") == 0);
  }
  {
    SharedFixture f;
    f.normal();
    assert(f.component.start_probe_polling());
    f.component.loop();
    test_now += 1500;
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(!f.component.get_probe_polling() && f.component.tx.empty());
    assert(f.status.state.find("Locked: Touch traffic lost before polling read") == 0);
  }
  {
    SharedFixture f;
    f.normal();
    assert(f.component.start_probe_polling());
    f.component.loop();
    f.poll_send();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    f.recover();
    receive(f.component, 0x12, 0x10, {0x20, 0x09, 0x50});
    receive(f.component, 0x12, 0x10, {0x20, 0x08, 0x50});
    assert(!f.component.get_probe_polling());
    assert(f.status.state.find("Locked: overlapping Touch transactions") == 0);
    assert(f.component.tx.size() == 10);
  }
  {
    ProbeFixture f;
    assert(!f.component.start_probe_polling());
  }
  {
    SharedFixture f;
    f.normal();
    f.component.set_passive_mode(true);
    assert(!f.component.start_probe_polling());
    assert(f.component.tx.empty());
  }
}
