#pragma once

struct TolerantFixture : ProbeFixture {
  TolerantFixture() : ProbeFixture(100, false, true) {}
  void error() {
    enqueue(component, {1, 0x22, 0x33, 0xFF, 2});
    component.loop();
  }
  void normal() {
    receive(component, 0x12, 0x10, {0x20, 0x09, 0x50});
    receive(component, 0x10, 0x12, {0x21, 0x09, 0x50, 4, 40, 0, 0, 0});
  }
  void unanswered() {
    const size_t before = component.tx.size();
    assert(component.request_probe(0x0310));
    handoff();
    test_now += 5;
    component.loop();
    assert(component.tx.size() == before + 10);
    test_now += 1500;
    component.loop();
    assert(status.state == "No reply within 1500ms; observing Touch recovery");
    test_now += 4000;
    normal();
    test_now += 1000;
    component.loop();
  }
};

void run_tolerant_probe_tests() {
  {
    Fixture f;
    f.component.set_experimental_query(true);
    f.component.set_query_tolerant_mode(true);
    assert(!f.component.request_probe(0x0310));
    assert(f.component.tx.empty() && f.direction.writes == 0);
  }
  {
    TolerantFixture f;
    assert(f.status.state == "Idle; source 0x13; tolerant; manual probe required");
    f.send();
    f.error();
    f.error();
    assert(f.component.get_invalid_packets() == 2);
    assert(f.status.state == "Read transmitted; waiting for response");
    test_now += 900;
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    assert(f.status.state == "Reply received; observing bus recovery");
    test_now += 4000;
    f.normal();
    test_now += 1000;
    f.component.loop();
    assert(f.status.state == "Reply received; bus traffic resumed; 60s cooldown");
    assert(f.airflow.state == 45 && f.component.tx.size() == 10);
    bool result = false;
    for (const auto &line : test_logs)
      result |= line.find("result=reply reply=yes late=0 rtt=900ms invalid_delta=2 checksum_delta=0") != std::string::npos;
    assert(result);
    assert(!f.component.request_probe(0x0310));
    test_now += 60000;
    f.component.loop();
    assert(f.component.tx.size() == 10 && f.component.request_probe(0x0310));
  }
  {
    TolerantFixture f;
    f.send();
    f.error();
    f.error();
    f.error();
    assert(f.status.state.find("Locked: probe error budget exceeded") == 0);
    test_now += 60000;
    f.normal();
    assert(!f.component.request_probe(0x0310) && f.component.tx.size() == 10);
  }
  {
    TolerantFixture f;
    assert(f.component.request_probe(0x0310));
    f.error();
    assert(f.status.state.find("Locked: framing errors") == 0);
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(f.component.tx.empty());
  }
  {
    TolerantFixture f;
    f.send();
    test_now += 1500;
    f.component.loop();
    assert(f.status.state == "No reply within 1500ms; observing Touch recovery");
    test_now += 400;
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    test_now += 100;
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    test_now += 3500;
    f.normal();
    test_now += 1000;
    f.component.loop();
    assert(f.status.state == "Reply received; bus traffic resumed; 60s cooldown");
    bool late = false;
    for (const auto &line : test_logs)
      late |= line.find("reply=yes late=1 rtt=1900ms") != std::string::npos;
    assert(late && f.component.tx.size() == 10);
  }
  {
    TolerantFixture f;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
      f.unanswered();
      assert(f.status.state == (attempt < 2 ? "No reply; Touch recovered; 60s cooldown" :
          "Locked: three consecutive unanswered probes; reboot required"));
      test_now += 60000;
    }
    assert(f.component.tx.size() == 30 && f.component.flushes == 3);
    assert(!f.component.request_probe(0x0310));
  }
  {
    TolerantFixture f;
    f.unanswered();
    test_now += 60000;
    assert(f.component.request_probe(0x0510));
    f.handoff();
    test_now += 5;
    f.component.loop();
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    test_now += 4000;
    f.normal();
    test_now += 1000;
    f.component.loop();
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
      test_now += 60000;
      f.unanswered();
      assert(f.status.state == "No reply; Touch recovered; 60s cooldown");
    }
    assert(f.component.tx.size() == 40);
  }
  {
    TolerantFixture f;
    f.send();
    receive(f.component, 0x10, 0x13, value(0x05, 0x10, 45));
    test_now += 5000;
    receive(f.component, 0x10, 0x12, {0x04});
    assert(f.status.state.find("Locked: bus did not resume") == 0);
    assert(!f.component.request_probe(0x0310));
  }
  {
    TolerantFixture f;
    f.send();
    test_now += 1500;
    f.component.loop();
    test_now += 5000;
    receive(f.component, 0x10, 0x12, {0x04});
    assert(f.status.state.find("Locked: Touch did not recover after unanswered probe") == 0);
    assert(!f.component.request_probe(0x0310) && f.component.tx.size() == 10);
  }
  {
    TolerantFixture f;
    f.send();
    for (unsigned restart = 0; restart < 4; ++restart)
      receive(f.component, 0x12, 0x10, {0x20, 0x09, 0x50});
    assert(f.status.state.find("Locked: overlapping Touch transactions") == 0);
    assert(f.component.tx.size() == 10);
  }
}
