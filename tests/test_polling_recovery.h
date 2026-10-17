#pragma once

struct ContinuousFixture : RecoveryFixture {
  void enable() {
    assert(component.start_probe_polling());
    assert(component.get_probe_polling());
    ready();
    component.loop();
    component.loop();
    poll_send();
    assert(component.tx.size() == 10);
  }
  void listen(uint32_t duration) {
    const uint32_t start = test_now;
    while (static_cast<uint32_t>(test_now - start) < duration) {
      const uint32_t remaining = duration - static_cast<uint32_t>(test_now - start);
      if (remaining > 5) {
        test_now += std::min(95U, remaining - 5);
        normal();
        poll_send();
      } else {
        test_now += remaining;
        normal();
      }
    }
  }
  void corrupt() {
    enqueue(component, {1, 0x22, 0x33, 0xFF, 2});
    component.loop();
  }
  void mode(uint8_t code) {
    receive(component, 0x12, 0x10, {0x40, 0x00, 0x20, 1, code, 0x08, 0x20, 1, 0});
    receive(component, 0x10, 0x12, {0x41, 0x08, 0x20, 0x00, 0x20});
  }
  void confirm_mode(uint8_t code) {
    receive(component, 0x12, 0x10, {0x20, 0x00, 0x20, 0x08, 0x20});
    receive(component, 0x10, 0x12, {0x21, 0x08, 0x20, 1, 0, 0x00, 0x20, 1, code});
  }
};

void run_polling_recovery_tests() {
  for (uint8_t length : {1, 4}) {
    ContinuousFixture f;
    f.enable();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    f.mode(9);
    receive(f.component, 0x12, 0x10, {0x20, 0x00, 0x20, 0x02, 0x20, 0x07, 0x20, 0x08, 0x20});
    std::vector<uint8_t> response{0x21, 0x00, 0x20, 1, 2, 0x07, 0x20, 1, 5, 0x08, 0x20, 1, 0, 0x02, 0x20, length, 9};
    if (length == 4) response.insert(response.end(), {0, 0, 0});
    receive(f.component, 0x10, 0x12, response);
    f.listen(11000);
    assert(f.component.get_probe_polling() && f.status.state.find("Polling paused:") != 0);
    assert(f.component.tx.size() == 10);
  }
  {
    ContinuousFixture f;
    f.enable();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    f.mode(2);
    receive(f.component, 0x12, 0x10, {0x20, 0x00, 0x20, 0x02, 0x20, 0x08, 0x20});
    receive(f.component, 0x10, 0x12, {0x21, 0x00, 0x20, 1, 9, 0x02, 0x20, 1, 2, 0x08, 0x20, 1, 0});
    f.listen(11000);
    assert(f.component.get_probe_polling() && f.status.state.find("Polling paused:") == 0);
    assert(f.component.tx.size() == 10);
  }
  {
    ContinuousFixture f;
    f.ready();
    receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x50, 0});
    assert(f.component.start_probe_polling());
    test_now += 1500;
    f.component.loop();
    assert(f.component.get_probe_polling() && f.status.state.find("backoff=60s") != std::string::npos);
    f.listen(59000);
    assert(f.component.tx.empty());
    f.listen(1300);
    assert(f.component.tx.size() == 10);
  }
  {
    ContinuousFixture f;
    f.ready();
    receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x50, 0});
    assert(f.component.start_probe_polling());
    test_now += 20;
    receive(f.component, 0x10, 0x12, {0x41, 0x00, 0x50});
    f.listen(2900);
    assert(f.component.tx.empty());
    f.listen(500);
    assert(f.component.tx.size() == 10);
  }
  {
    ContinuousFixture f;
    f.ready();
    f.send();
    f.corrupt();
    assert(!f.component.get_probe_polling() && f.status.state.find("Locked:") == 0);
    assert(f.component.start_probe_polling());
    f.listen(59000);
    assert(f.component.tx.size() == 10);
    f.listen(1300);
    assert(f.component.get_probe_polling() && f.component.tx.size() == 20);
  }
  {
    ContinuousFixture f;
    assert(f.component.start_probe_polling());
    assert(f.component.get_probe_polling());
    assert(f.status.state.find("Polling paused:") == 0);
    test_now += 120000;
    f.poll_send();
    assert(f.component.tx.empty() && f.component.get_probe_polling());
    f.ready();
    f.component.loop();
    f.poll_send();
    assert(f.component.tx.size() == 10);
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    f.listen(59000);
    assert(f.component.tx.size() == 10);
    f.listen(1500);
    assert(f.component.tx.size() == 20 && f.component.get_probe_polling());
  }
  {
    ContinuousFixture f;
    f.ready();
    f.mode(9);
    assert(f.component.start_probe_polling());
    f.listen(4000);
    assert(f.component.tx.empty() && f.component.get_probe_polling());
    f.confirm_mode(9);
    f.listen(500);
    assert(f.component.tx.size() == 10);
  }
  for (uint32_t start : {100U, UINT32_MAX - 1000}) {
    ContinuousFixture f;
    test_now = start;
    f.enable();
    f.corrupt();
    assert(f.component.get_probe_polling() && f.status.state.find("Polling paused:") == 0);
    assert(f.status.state.find("backoff=60s") != std::string::npos);
    f.listen(59000);
    assert(f.component.tx.size() == 10);
    f.listen(1300);
    assert(f.component.tx.size() == 20);
    assert(f.component.tx[16] == 0x03 && f.component.tx[17] == 0x10);
    receive(f.component, 0x10, 0x12, value(0x03, 0x10, 1000));
    f.listen(6000);
    assert(f.component.get_probe_polling() && f.status.state.find("Telemetry polling;") == 0);
  }
  {
    ContinuousFixture f;
    f.enable();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    test_now += 10000;
    f.component.loop();
    assert(f.component.get_probe_polling() && f.status.state.find("Polling paused:") == 0);
    f.listen(59000);
    assert(f.component.tx.size() == 10);
    f.listen(1300);
    assert(f.component.tx.size() == 20);
  }
  {
    ContinuousFixture f;
    f.enable();
    f.listen(6000);
    assert(f.component.get_probe_polling() && f.status.state.find("Polling paused:") == 0);
    assert(f.status.state.find("telemetry reply missing") != std::string::npos);
    f.listen(59000);
    assert(f.component.tx.size() == 10);
    f.listen(1500);
    assert(f.component.tx.size() == 20 && f.component.tx[16] == 0x03);
  }
  {
    ContinuousFixture f;
    f.enable();
    const uint32_t backoffs[]{60000, 120000, 240000, 480000, 900000, 900000};
    for (unsigned i = 0; i < 6; ++i) {
      f.corrupt();
      const size_t transmitted = f.component.tx.size();
      const auto status = std::string("backoff=") + std::to_string(backoffs[i] / 1000) + "s";
      assert(f.component.get_probe_polling() && f.status.state.find(status) != std::string::npos);
      f.listen(backoffs[i] - 100);
      assert(f.component.tx.size() == transmitted);
      f.listen(300);
      assert(f.component.tx.size() == transmitted + 10);
    }
  }
  {
    ContinuousFixture f;
    f.enable();
    f.corrupt();
    f.listen(59000);
    f.corrupt();
    f.listen(1000);
    assert(f.component.tx.size() == 10);
    f.listen(10000);
    assert(f.component.tx.size() == 20);
  }
  {
    ContinuousFixture f;
    f.enable();
    f.corrupt();
    for (unsigned i = 0; i < 12; ++i) {
      f.listen(4980);
      receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x50, 0});
      test_now += 20;
      receive(f.component, 0x10, 0x12, {0x41, 0x00, 0x50});
    }
    assert(f.component.tx.size() == 10 && f.component.get_probe_polling());
    f.listen(3300);
    assert(f.component.tx.size() == 20);
  }
  for (unsigned successes : {1U, 3U}) {
    ContinuousFixture f;
    f.enable();
    f.corrupt();
    f.listen(60300);
    for (unsigned i = 0; i < successes; ++i) {
      const size_t offset = f.component.tx.size() - 10;
      receive(f.component, 0x10, 0x12, value(f.component.tx[offset + 6], f.component.tx[offset + 7], 45));
      f.listen(6000);
      if (i + 1 < successes) f.listen(54300);
    }
    f.corrupt();
    const auto expected = successes == 3 ? "backoff=60s" : "backoff=120s";
    assert(f.status.state.find(expected) != std::string::npos && f.component.get_probe_polling());
  }
  {
    ContinuousFixture f;
    f.enable();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x20, 1, 9, 0x08, 0x20, 1, 0});
    test_now += 1500;
    f.component.loop();
    assert(f.component.get_probe_polling() && f.status.state.find("Polling paused:") == 0);
    f.listen(120000);
    assert(f.component.tx.size() == 10);
    f.mode(17);
    f.confirm_mode(17);
    f.listen(12000);
    assert(f.component.tx.size() == 20 && f.component.get_probe_polling());
    for (size_t i = 0; i < f.component.tx.size(); i += 10) assert(f.component.tx[i + 5] == 0x20);
  }
  {
    ContinuousFixture f;
    f.enable();
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    f.mode(9);
    f.listen(11000);
    assert(f.component.get_probe_polling() && f.status.state.find("Polling paused:") == 0);
    f.confirm_mode(17);
    f.listen(120000);
    assert(f.component.tx.size() == 10);
    f.confirm_mode(9);
    f.listen(12000);
    assert(f.component.tx.size() == 20);
  }
  for (bool failure : {false, true}) {
    ContinuousFixture f;
    if (failure) {
      f.enable();
      f.corrupt();
    } else assert(f.component.start_probe_polling());
    const auto transmitted = f.component.tx.size();
    f.component.stop_probe_polling();
    f.listen(1000000);
    assert(!f.component.get_probe_polling() && f.component.tx.size() == transmitted);
  }
  {
    ContinuousFixture f;
    f.ready();
    f.component.flush_result = uart::UARTFlushResult::UART_FLUSH_RESULT_FAILED;
    f.enable();
    assert(!f.component.get_probe_polling() && f.status.state.find("Locked: UART") == 0);
    f.component.flush_result = uart::UARTFlushResult::UART_FLUSH_RESULT_SUCCESS;
    f.listen(1000000);
    assert(!f.component.start_probe_polling() && f.component.tx.size() == 10);
  }
}
