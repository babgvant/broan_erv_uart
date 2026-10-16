#pragma once

struct PhaseFixture : SharedFixture {
  PhaseFixture() : SharedFixture(true) {}
  static std::vector<uint8_t> request(uint16_t phase, bool extra = false) {
    std::vector<uint8_t> data;
    switch (phase) {
      case 0x0020: data = {0x20, 0x00, 0x20, 0x02, 0x20, 0x06, 0x20, 0x07, 0x20, 0x08, 0x20, 0x03, 0x30, 0x04, 0x30}; break;
      case 0x0030: data = {0x20, 0x00, 0x30, 0x02, 0x30}; break;
      case 0x1250: data = {0x20, 0x12, 0x50, 0x13, 0x50, 0x14, 0x50}; break;
      case 0x0830: data = {0x20, 0x08, 0x30, 0x09, 0x30, 0x19, 0x50, 0x20, 0x50, 0x21, 0x50}; break;
      case 0x1200: data = {0x20, 0x12, 0x00, 0x1A, 0x00, 0x01, 0xE0, 0x0A, 0xF0}; break;
      case 0x0421: data = {0x20, 0x04, 0x21, 0x05, 0x21, 0x06, 0x21, 0x0F, 0x22, 0x10, 0x22}; break;
      case 0x0222: data = {0x20, 0x02, 0x22, 0x03, 0x22, 0x0A, 0x22, 0x0C, 0x22, 0x0E, 0x22}; break;
      case 0x0C21: data = {0x20, 0x0C, 0x21, 0x0D, 0x21, 0x08, 0x50, 0x09, 0x50}; break;
    }
    if (extra) data.insert(data.end(), {0x14, 0x00});
    return data;
  }
  static std::vector<uint8_t> response(uint16_t phase, bool extra = false) {
    const auto read = request(phase, extra);
    std::vector<uint8_t> data{0x21};
    for (size_t n = (read.size() - 1) / 2; n > 0; --n) {
      const size_t i = 1 + (n - 1) * 2;
      const uint16_t id = (read[i] << 8) | read[i+1];
      const uint8_t length = id == 0x0330 || id == 0x0430 || id == 0x1400 ? 4 : 1;
      data.insert(data.end(), {read[i], read[i+1], length});
      data.insert(data.end(), length, 0);
    }
    return data;
  }
  void group(uint16_t phase, bool extra = false) {
    receive(component, 0x12, 0x10, request(phase, extra));
    receive(component, 0x10, 0x12, response(phase, extra));
  }
};

void run_phase_probe_tests() {
  for (uint16_t phase : {0x0020, 0x0030, 0x1250, 0x0830, 0x1200, 0x0421, 0x0222, 0x0C21}) {
    for (bool extra : {false, true}) {
      PhaseFixture f;
      assert(f.component.request_probe_after_phase(0x0510, phase));
      f.group(phase == 0x0030 ? 0x0020 : 0x0030);
      f.poll_send();
      assert(f.component.tx.empty());
      f.group(phase, extra);
      f.poll_send();
      assert(f.component.tx == frame(0x12, 0x10, {0x20, 0x05, 0x10}));
      assert(f.component.array_writes == 1 && f.direction.writes == 0);
    }
  }
  {
    PhaseFixture f;
    f.group(0x0020);
    assert(f.component.request_probe_after_phase(0x0510, 0x0020));
    f.poll_send();
    assert(f.component.tx.empty());
    f.group(0x0020);
    f.poll_send();
    assert(f.component.tx.size() == 10);
  }
  {
    PhaseFixture f;
    receive(f.component, 0x12, 0x10, f.request(0x0020));
    assert(f.component.request_probe_after_phase(0x0510, 0x0020));
    receive(f.component, 0x10, 0x12, f.response(0x0020));
    f.poll_send();
    assert(f.component.tx.empty());
    f.group(0x0020);
    f.poll_send();
    assert(f.component.tx.size() == 10);
  }
  {
    PhaseFixture f;
    assert(f.component.request_probe_after_phase(0x0510, 0x0020));
    receive(f.component, 0x12, 0x10, f.request(0x0020));
    receive(f.component, 0x10, 0x12, {0x21, 0x00, 0x20, 1, 17});
    f.poll_send();
    assert(f.component.tx.empty());
    receive(f.component, 0x10, 0x12, f.response(0x0020));
    f.poll_send();
    assert(f.component.tx.size() == 10);
  }
  {
    PhaseFixture f;
    assert(f.component.request_probe_after_phase(0x0510, 0x0020));
    receive(f.component, 0x12, 0x10, {0x20, 0x00, 0x20, 0x09, 0x50});
    receive(f.component, 0x10, 0x12, {0x21, 0x00, 0x20, 1, 17, 0x09, 0x50, 4, 0, 0, 0, 0});
    f.poll_send();
    assert(f.component.tx.empty());
  }
  for (bool write : {false, true}) {
    PhaseFixture f;
    assert(f.component.request_probe_after_phase(0x0510, 0x0020));
    f.group(0x0020);
    if (write) {
      receive(f.component, 0x12, 0x10, {0x40, 0x00, 0x50, 0});
      receive(f.component, 0x10, 0x12, {0x41, 0x00, 0x50});
    } else f.group(0x0030);
    f.poll_send();
    assert(f.component.tx.empty());
    f.group(0x0020);
    f.poll_send();
    assert(f.component.tx.size() == 10);
  }
  for (uint32_t delay : {245U, 251U}) {
    PhaseFixture f;
    assert(f.component.request_probe_after_phase(0x0510, 0x0020));
    f.group(0x0020);
    test_now += delay;
    f.poll_send();
    assert(f.component.tx.empty());
    f.group(0x0020);
    f.poll_send();
    assert(f.component.tx.size() == 10);
  }
  {
    PhaseFixture f;
    assert(f.component.request_probe_after_phase(0x0510, 0x0020));
    f.group(0x0020);
    f.handoff();
    test_now += 9;
    f.component.loop();
    f.poll_send();
    assert(f.component.tx.empty());
    f.group(0x0020);
    f.poll_send();
    assert(f.component.tx.size() == 10);
  }
  {
    PhaseFixture f;
    assert(f.component.request_probe_after_phase(0x0510, 0x0020));
    for (unsigned i = 0; i < 30; ++i) {
      test_now += 1000;
      f.group(0x0030);
      f.handoff();
    }
    assert(f.component.tx.empty());
    assert(f.status.state == "No matching Touch phase/handoff in 30s; nothing transmitted");
    f.send();
  }
  {
    PhaseFixture f;
    assert(!f.component.request_probe_after_phase(0x0510, 0));
    assert(!f.component.request_probe_after_phase(0x0510, 0xFFFF));
    assert(!f.component.request_probe_after_phase(0x0020, 0x0020));
    f.component.set_passive_mode(true);
    assert(!f.component.request_probe_after_phase(0x0510, 0x0020));
    assert(f.component.tx.empty());
  }
  {
    PhaseFixture f;
    assert(f.component.request_probe_after_phase(0x0510, 0x0020, 1));
    f.group(0x0020);
    f.handoff();
    ++test_now;
    f.component.loop();
    const uint32_t sent = test_now;
    test_now += 50;
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    f.recover();
    assert(f.component.start_probe_polling());
    while (static_cast<uint32_t>(test_now - sent) < 60000) {
      test_now += 1000;
      f.normal();
    }
    f.handoff();
    ++test_now;
    f.component.loop();
    assert(f.component.tx.size() == 10);
    f.group(0x0020);
    f.handoff();
    ++test_now;
    f.component.loop();
    assert(f.component.tx.size() == 20 && f.component.get_probe_polling());
    f.component.stop_probe_polling();
  }
  {
    ProbeFixture f(100, false, false, true);
    assert(f.component.request_probe_after_phase(0x0510, 0x0020));
    receive(f.component, 0x12, 0x10, PhaseFixture::request(0x0020));
    receive(f.component, 0x10, 0x12, PhaseFixture::response(0x0020));
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(f.component.tx == frame(0x13, 0x10, {0x20, 0x05, 0x10}));
  }
  {
    PhaseFixture f;
    test_now = UINT32_MAX - 2;
    assert(f.component.request_probe_after_phase(0x0510, 0x0020));
    f.group(0x0020);
    f.poll_send();
    assert(f.component.tx.size() == 10);
  }
}
