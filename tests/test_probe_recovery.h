#pragma once

struct RecoveryFixture : SharedFixture {
  RecoveryFixture() : SharedFixture(true) { component.set_query_recovery_mode(true); }
  void ready() {
    for (unsigned i = 0; i < 3; ++i) {
      test_now += 100;
      normal();
    }
  }
  void settle(uint32_t duration) {
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += 100) {
      test_now += 100;
      normal();
    }
  }
};

void run_recovery_probe_tests() {
  for (uint32_t start : {100U, UINT32_MAX - 500U}) {
    RecoveryFixture f;
    test_now = start;
    assert(std::isnan(f.component.get_last_probe_success_age()));
    f.ready();
    f.send();
    f.normal();
    assert(std::isnan(f.component.get_last_probe_success_age()));
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    assert(f.component.get_last_probe_success_age() == 0);
    test_now += 2000;
    assert(f.component.get_last_probe_success_age() == 2);
    f.component.loop();
    f.component.stop_probe_polling();
    test_now += 3000;
    f.component.loop();
    assert(f.component.get_last_probe_success_age() == 5);
    test_now += UINT32_MAX - 10000U;
    f.component.loop();
    test_now += 20000;
    f.component.loop();
    assert(f.component.get_last_probe_success_age() > 4294967.0f);
  }
  for (uint32_t start : {100U, UINT32_MAX - 5}) {
    RecoveryFixture f;
    test_now = start;
    f.ready();
    assert(f.component.start_probe_trial());
    test_millis_step = 1;
    f.component.loop();
    test_millis_step = 0;
    assert(f.component.get_probe_polling());
    assert(f.status.state == "Queued; waiting for acknowledged Touch grant");
    assert(f.component.tx.empty());
    f.poll_send();
    assert(f.component.tx.size() == 10);
  }
  {
    SharedFixture f(true);
    f.normal();
    assert(!f.component.start_probe_trial());
    assert(f.component.tx.empty());
  }
  {
    RecoveryFixture f;
    assert(!f.component.request_probe(0x0510));
    f.normal();
    f.normal();
    assert(!f.component.start_probe_trial());
    f.normal();
    f.send();
    receive(f.component, 0x12, 0x10, PhaseFixture::request(0x0421));
    test_now += 33;
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    test_now += 2100;
    receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x50, 0});
    receive(f.component, 0x10, 0x12, {0x41, 0x00, 0x50});
    f.settle(5000);
    assert(f.status.state == "Shared-address reply received; Touch recovered; 60s cooldown");
    assert(f.component.tx.size() == 10 && f.airflow.state == 45);
    assert(!f.component.request_probe(0x0310));
    f.settle(60000);
    assert(f.component.request_probe(0x0310));
    f.poll_send();
    assert(f.component.tx.size() == 20);
  }
  {
    RecoveryFixture f;
    f.ready();
    f.send();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    test_now += 5000;
    f.component.loop();
    assert(f.status.state == "Reply received; observing bus recovery");
    f.normal();
    f.normal();
    assert(f.status.state == "Reply received; observing bus recovery");
    f.normal();
    assert(f.status.state == "Shared-address reply received; Touch recovered; 60s cooldown");
  }
  for (bool reply : {false, true}) {
    RecoveryFixture f;
    f.ready();
    f.send();
    const uint32_t sent = test_now;
    if (reply) receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    else {
      test_now += 500;
      f.component.loop();
    }
    test_now = sent + 9999;
    f.component.loop();
    assert(f.status.state.find("Locked:") != 0);
    ++test_now;
    f.component.loop();
    assert(f.status.state.find("Locked:") == 0);
    assert(!f.component.start_probe_trial() && f.component.tx.size() == 10);
  }
  {
    RecoveryFixture f;
    f.ready();
    f.send();
    const uint32_t sent = test_now;
    test_now += 500;
    f.component.loop();
    test_now = sent + 9900;
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    assert(f.status.state.find("Locked:") != 0);
    test_now = sent + 10000;
    f.component.loop();
    assert(f.status.state.find("Locked:") == 0);
  }
  {
    RecoveryFixture f;
    f.ready();
    f.send();
    test_now += 500;
    f.component.loop();
    receive(f.component, 0x12, 0x10, {0x20, 0x05, 0x10});
    assert(f.status.state.find("Locked: Touch requested probe register") == 0);
  }
  for (bool followup : {false, true}) {
    RecoveryFixture f;
    f.ready();
    assert(f.component.start_probe_trial());
    f.component.loop();
    f.poll_send();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x20, 1, 9, 0x08, 0x20, 1, 0});
    if (followup) f.normal();
    else {
      test_now += 1500;
      f.component.loop();
    }
    assert(f.status.state.find("Locked: Touch write") == 0);
    assert(!f.component.get_probe_polling() && f.component.tx.size() == 10);
    receive(f.component, 0x10, 0x12, {0x41, 0x08, 0x20, 0x00, 0x20});
    f.settle(60000);
    assert(f.component.tx.size() == 10 && !f.component.start_probe_trial());
  }
  {
    RecoveryFixture f;
    f.ready();
    assert(f.component.start_probe_trial());
    f.component.loop();
    receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x20, 1, 17, 0x08, 0x20, 1, 0});
    receive(f.component, 0x10, 0x12, {0x41, 0x08, 0x20, 0x00, 0x20});
    receive(f.component, 0x12, 0x10, {0x20, 0x00, 0x20, 0x08, 0x20});
    receive(f.component, 0x10, 0x12, {0x21, 0x08, 0x20, 1, 0, 0x00, 0x20, 1, 17});
    f.settle(2900);
    f.poll_send();
    assert(f.component.tx.empty() && f.component.get_probe_polling());
    f.settle(100);
    f.poll_send();
    assert(f.component.tx.size() == 10);
  }
  {
    RecoveryFixture f;
    f.ready();
    f.send();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    enqueue(f.component, {1, 0x22, 0x33, 0xFF, 2});
    f.component.loop();
    assert(f.status.state.find("Locked: framing errors") == 0);
  }
  for (bool correct : {false, true}) {
    RecoveryFixture f;
    f.ready();
    receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x20, 1, 9, 0x08, 0x20, 1, 0});
    const uint32_t written = test_now;
    receive(f.component, 0x10, 0x12, {0x41, 0x08, 0x20, 0x00, 0x20});
    test_now += 100;
    receive(f.component, 0x12, 0x10, {0x20, 0x00, 0x20, 0x08, 0x20});
    receive(f.component, 0x10, 0x12, {0x21, 0x00, 0x20, 1, static_cast<uint8_t>(correct ? 9 : 17), 0x08, 0x20, 1, 0});
    f.settle(3000);
    assert(f.component.start_probe_trial() == correct);
    f.component.stop_probe_polling();
    test_now = written + 10000;
    f.component.loop();
    assert((f.status.state.find("Locked: Touch mode readback") == 0) == !correct);
    assert(f.component.tx.empty());
  }
  {
    RecoveryFixture f;
    f.ready();
    receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x20, 1, 9});
    receive(f.component, 0x10, 0x12, {0x41, 0x00, 0x20});
    f.settle(10000);
    assert(f.status.state.find("Locked: Touch mode readback") == 0);
    assert(!f.component.request_probe(0x0510));
  }
  {
    RecoveryFixture f;
    f.ready();
    assert(f.component.start_probe_trial());
    f.component.loop();
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
      f.poll_send();
      test_now += 500;
      f.component.loop();
      f.settle(5100);
      if (attempt < 2) {
        assert(f.component.get_probe_polling());
        f.settle(10000);
      }
    }
    assert(f.status.state.find("Locked: three consecutive unanswered probes") == 0);
    assert(!f.component.get_probe_polling() && f.component.tx.size() == 30);
  }
  for (uint32_t start : {100U, UINT32_MAX - 1000}) {
    RecoveryFixture f;
    test_now = start;
    f.ready();
    assert(f.component.start_probe_trial());
    const uint16_t registers[]{0x0510, 0x0310, 0x0610, 0x0410, 0x2350};
    uint32_t previous = 0;
    for (unsigned i = 0; i < 5; ++i) {
      f.component.loop();
      f.poll_send();
      const uint32_t sent = test_now;
      if (i) assert(static_cast<uint32_t>(sent - previous) >= 15000);
      previous = sent;
      assert(f.component.tx.size() == (i + 1) * 10);
      const auto opcode = registers[i];
      assert(f.component.tx[i * 10 + 6] == opcode >> 8 && f.component.tx[i * 10 + 7] == (opcode & 255));
      test_now += 34;
      receive(f.component, 0x10, 0x12, value(opcode >> 8, opcode & 255, 45));
      f.settle(5100);
      if (i < 4) {
        assert(f.component.get_probe_polling());
        assert(f.status.state == "Bounded trial; Touch recovered; 15s cooldown");
        while (static_cast<uint32_t>(test_now - sent) < 15000) {
          f.settle(100);
          assert(f.component.tx.size() == (i + 1) * 10);
        }
      }
    }
    assert(f.status.state == "Bounded trial complete; five reads sent; polling off");
    assert(!f.component.get_probe_polling());
    assert(!f.component.start_probe_trial());
    f.settle(120000);
    assert(f.component.tx.size() == 50);
  }
  for (bool sent : {false, true}) {
    RecoveryFixture f;
    f.ready();
    assert(f.component.start_probe_trial());
    f.component.loop();
    if (sent) {
      f.poll_send();
      receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    }
    f.component.stop_probe_polling();
    f.settle(120000);
    f.poll_send();
    assert(!f.component.get_probe_polling());
    assert(f.component.tx.size() == (sent ? 10 : 0));
  }
  {
    RecoveryFixture f;
    f.ready();
    assert(f.component.start_probe_trial());
    f.component.loop();
    test_now += 10000;
    f.component.loop();
    assert(f.status.state.find("Locked: Touch traffic lost") == 0);
    assert(!f.component.get_probe_polling() && f.component.tx.empty());
  }
}
