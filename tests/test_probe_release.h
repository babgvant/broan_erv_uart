#pragma once

struct ReleaseFixture : TolerantFixture {
  uint32_t sent_at;
  void send_release_probe() {
    assert(component.request_probe_with_release(0x0510, 3));
    handoff();
    test_now += 3;
    component.loop();
    sent_at = test_now;
    assert(component.tx == frame(0x13, 0x10, {0x20, 0x05, 0x10}));
  }
  std::vector<uint8_t> expected() {
    auto bytes = frame(0x13, 0x10, {0x20, 0x05, 0x10});
    auto release = frame(0x13, 0x10, {0x04});
    bytes.insert(bytes.end(), release.begin(), release.end());
    return bytes;
  }
};

void run_probe_release_tests() {
  {
    ReleaseFixture f;
    const size_t log_start = test_logs.size();
    const unsigned stops = test_fast_loop_stops;
    f.send_release_probe();
    assert(test_fast_loop_stops == stops);
    test_now += 149;
    f.component.loop();
    assert(f.component.tx.size() == 10);
    ++test_now;
    f.component.loop();
    assert(f.component.tx == f.expected() && f.component.flushes == 2);
    assert(test_fast_loop_stops == stops + 1);
    receive(f.component, 0x13, 0x10, {0x04});
    test_now += 8;
    receive(f.component, 0x10, 0x13, {0x05});
    receive(f.component, 0x10, 0x13, {0x05});
    assert(f.status.state == "Read transmitted; waiting for response");
    test_now += 20;
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    test_now += 4000;
    f.normal();
    test_now += 1000;
    f.component.loop();
    assert(f.status.state == "Reply received; bus traffic resumed; 60s cooldown");
    assert(f.component.tx == f.expected());
    assert(!f.component.request_probe_with_release(0x0510));
    bool summary = false;
    for (size_t i = log_start; i < test_logs.size(); ++i)
      summary |= test_logs[i].find("release sent=1 ack=1 after=150ms ack_rtt=8ms") != std::string::npos;
    assert(summary);
    test_now += 60000;
    const auto before = f.component.tx;
    assert(f.component.request_probe(0x0510));
    f.handoff();
    test_now += 5;
    f.component.loop();
    test_now += 160;
    f.component.loop();
    assert(f.component.tx.size() == before.size() + 10);
  }
  {
    ReleaseFixture f;
    f.send_release_probe();
    test_now += 50;
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    test_now += 4;
    f.component.loop();
    assert(f.component.tx.size() == 10);
    ++test_now;
    f.component.loop();
    assert(f.component.tx == f.expected());
    test_now += 100;
    f.component.loop();
    assert(f.component.tx == f.expected());
  }
  {
    ReleaseFixture f;
    const unsigned stops = test_fast_loop_stops;
    f.send_release_probe();
    for (uint32_t elapsed = 149; elapsed <= 349; elapsed += 4) {
      test_now = f.sent_at + elapsed;
      receive(f.component, 0x10, 0x12, {0x04});
    }
    assert(f.component.tx.size() == 10);
    test_now = f.sent_at + 350;
    f.component.loop();
    assert(test_fast_loop_stops == stops + 1);
    test_now += 10;
    f.component.loop();
    assert(f.component.tx.size() == 10);
  }
  {
    ReleaseFixture f;
    f.send_release_probe();
    test_now += 150;
    enqueue(f.component, {1, 0x12});
    f.component.loop();
    test_now += 6;
    f.component.loop();
    assert(f.component.tx.size() == 10);
    test_now = f.sent_at + 350;
    f.component.loop();
    assert(f.component.tx.size() == 10);
  }
  {
    ReleaseFixture f;
    f.send_release_probe();
    receive(f.component, 0x10, 0x13, {0x05});
    assert(f.status.state.find("Locked: other traffic") == 0);
    test_now += 150;
    f.component.loop();
    assert(f.component.tx.size() == 10);
  }
  {
    ReleaseFixture f;
    f.send_release_probe();
    f.error();
    f.error();
    f.error();
    test_now += 150;
    f.component.loop();
    assert(f.status.state.find("Locked: probe error budget") == 0);
    assert(f.component.tx.size() == 10);
  }
  {
    ReleaseFixture f;
    test_now = UINT32_MAX - 80;
    f.send_release_probe();
    test_now += 150;
    f.component.loop();
    assert(f.component.tx == f.expected());
    test_now += 10;
    receive(f.component, 0x10, 0x13, {0x05});
    assert(f.status.state == "Read transmitted; waiting for response");
  }
  {
    ReleaseFixture f;
    f.send_release_probe();
    test_now += 1500;
    f.component.loop();
    assert(f.component.tx.size() == 10);
    assert(f.status.state == "No reply within 1500ms; observing Touch recovery");
  }
  {
    ReleaseFixture f;
    f.send_release_probe();
    test_now += 150;
    f.component.loop();
    test_now += 10;
    receive(f.component, 0x10, 0x13, {0x05});
    test_now = f.sent_at + 1500;
    f.component.loop();
    test_now += 5000;
    f.component.loop();
    assert(f.status.state.find("Locked: Touch did not recover") == 0);
    assert(f.component.tx == f.expected());
  }
  {
    ReleaseFixture f;
    assert(f.component.request_probe_with_release(0x0510));
    test_now += 30000;
    f.component.loop();
    assert(f.status.state == "No eligible handoff in 30s; nothing transmitted");
    test_now += 150;
    f.component.loop();
    assert(f.component.tx.empty());
  }
  for (bool shared : {false, true}) {
    ProbeFixture f(100, shared);
    assert(!f.component.request_probe_with_release(0x0510));
    assert(f.component.tx.empty());
  }
  {
    TolerantFixture f;
    f.component.set_passive_mode(true);
    assert(!f.component.request_probe_with_release(0x0510));
    assert(f.component.tx.empty());
  }
  {
    TolerantFixture f;
    assert(!f.component.request_probe_with_release(0x0020));
    assert(!f.component.request_probe_with_release(0x0510, 0));
    assert(f.component.tx.empty());
  }
}
