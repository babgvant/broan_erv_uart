#pragma once

void run_probe_timing_tests() {
  for (uint32_t idle : {1U, 3U, 5U, 8U}) {
    TolerantFixture f;
    assert(f.component.request_probe(0x0510, idle));
    f.handoff();
    test_now += idle - 1;
    f.component.loop();
    assert(f.component.tx.empty());
    ++test_now;
    f.component.loop();
    assert(f.component.tx == frame(0x13, 0x10, {0x20, 0x05, 0x10}));
    assert(f.component.flushes == 1);
    assert(!f.component.request_probe(0x0510, idle));
  }
  for (uint32_t idle : {0U, 9U, UINT32_MAX}) {
    TolerantFixture f;
    assert(!f.component.request_probe(0x0510, idle));
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(f.component.tx.empty());
    assert(f.component.request_probe(0x0510));
  }
  for (bool shared : {false, true}) {
    ProbeFixture f(100, shared);
    assert(!f.component.request_probe(0x0510, 1));
    assert(!f.component.request_probe(0x0510, 3));
    f.send();
  }
  {
    TolerantFixture f;
    f.component.set_passive_mode(true);
    assert(!f.component.request_probe(0x0510, 1));
    assert(f.component.tx.empty());
  }
  for (uint32_t idle : {1U, 3U}) {
    TolerantFixture f;
    assert(f.component.request_probe(0x0510, idle));
    f.handoff();
    receive(f.component, 0x10, 0x12, {0x04});
    test_now += idle;
    f.component.loop();
    assert(f.component.tx.empty());
    f.handoff();
    test_now += 9;
    f.component.loop();
    assert(f.component.tx.empty());
    f.handoff();
    test_now += idle;
    f.component.loop();
    assert(f.component.tx.size() == 10);
  }
  {
    TolerantFixture f;
    assert(f.component.request_probe(0x0510, 1));
    f.handoff();
    enqueue(f.component, {1, 0x12});
    ++test_now;
    f.component.loop();
    ++test_now;
    f.component.loop();
    assert(f.component.tx.empty());
    test_now += 251;
    f.component.loop();
    assert(f.status.state.find("Locked: framing errors") == 0);
  }
  for (uint32_t now : {100U, UINT32_MAX - 5}) {
    TolerantFixture f;
    const size_t log_start = test_logs.size();
    test_now = now;
    assert(f.component.request_probe(0x0510, 1));
    f.handoff();
    ++test_now;
    f.component.loop();
    test_now += 12;
    receive(f.component, 0x10, 0x12, {0x04});
    test_now += 3;
    receive(f.component, 0x12, 0x10, {0x20, 0x09, 0x50});
    test_now += 4;
    receive(f.component, 0x10, 0x12, {0x21, 0x08, 0x50, 1, 1});
    test_now += 2000;
    receive(f.component, 0x10, 0x12, {0x21, 0x09, 0x50, 4, 40, 0, 0, 0});
    test_now += 4000;
    f.normal();
    test_now += 1000;
    f.component.loop();
    bool grant = false, read = false, reply = false, summary = false;
    for (size_t i = log_start; i < test_logs.size(); ++i) {
      const auto &line = test_logs[i];
      grant |= line.find("first Touch grant at +12ms") != std::string::npos;
      read |= line.find("first Touch read at +15ms") != std::string::npos;
      reply |= line.find("first matched Touch read reply at +6019ms") != std::string::npos;
      summary |= line.find("timing idle_min=1ms touch_grant=12ms touch_read=15ms touch_reply=6019ms") != std::string::npos;
    }
    assert(grant && read && reply && summary);
    assert(f.component.tx.size() == 10);
  }
  {
    TolerantFixture f;
    const size_t log_start = test_logs.size();
    f.send();
    test_now += 10;
    receive(f.component, 0x12, 0x10, {0x20, 0x09, 0x50});
    test_now += 15;
    receive(f.component, 0x10, 0x12, {0x21, 0x09, 0x50, 4, 40, 0, 0, 0});
    test_now += 20;
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    test_now += 4000;
    f.normal();
    test_now += 1000;
    f.component.loop();
    bool summary = false;
    for (size_t i = log_start; i < test_logs.size(); ++i)
      summary |= test_logs[i].find("timing idle_min=5ms touch_grant=nanms touch_read=10ms touch_reply=25ms") != std::string::npos;
    assert(summary && f.status.state == "Reply received; bus traffic resumed; 60s cooldown");
  }
}
