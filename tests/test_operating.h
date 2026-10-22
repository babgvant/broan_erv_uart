#pragma once

void run_operating_tests() {
  for (int operating_mode = 0; operating_mode < 3; ++operating_mode) {
    Fixture f;
    binary_sensor::BinarySensor operating;
    f.component.set_operating_binary_sensor(&operating);
    f.component.set_passive_mode(operating_mode == 0);
    f.component.set_experimental_query(operating_mode == 1);
    f.component.loop();
    assert(!operating.has_state);
    receive(f.component, 0x10, 0x12, {0x21, 0x07, 0x20, 1, 0});
    assert(operating.has_state && !operating.state);
    for (uint8_t code = 1; code <= 5; ++code) {
      receive(f.component, 0x10, 0x12, {0x21, 0x07, 0x20, 4, code, 0, 0, 0});
      assert(operating.has_state && operating.state);
    }
    receive(f.component, 0x10, 0x12, {0x21, 0x07, 0x20, 1, 222});
    assert(!operating.has_state);
    receive(f.component, 0x10, 0x12, {0x21, 0x07, 0x20, 1, 1});
    test_now += 900;
    receive(f.component, 0x10, 0x12, {0x21, 0x07, 0x20, 1, 1});
    test_now += 999;
    receive(f.component, 0x10, 0x12, value(0x04, 0x10, 1200));
    assert(operating.has_state && operating.state);
    test_now += 1;
    f.component.loop();
    assert(!operating.has_state);
    receive(f.component, 0x10, 0x12, {0x21, 0x07, 0x20, 1, 1});
    assert(operating.has_state && operating.state);
    receive(f.component, 0x10, 0x12, {0x21, 0x07, 0x20, 2, 0, 0});
    assert(operating.has_state && operating.state);
    receive(f.component, 0x10, 0x12, {0x21, 0x07, 0x20, 1, 0});
    assert(operating.has_state && !operating.state);
    if (operating_mode == 0) assert(f.component.tx.empty());
  }
}
