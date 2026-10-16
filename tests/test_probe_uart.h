#pragma once

void run_uart_probe_tests() {
  for (bool automatic : {false, true}) {
    SharedFixture f(automatic);
    const size_t log_start = test_logs.size();
    f.component.write_delay_us = 300;
    f.component.flush_delay_us = 12000;
    test_micros_extra = UINT32_MAX - 1000;
    f.send();
    assert(f.component.array_writes == 1);
    assert(f.direction.writes == (automatic ? 0 : 3));
    assert(f.direction.setups == (automatic ? 0 : 1));
    bool timing = false;
    for (size_t i = log_start; i < test_logs.size(); ++i) {
      timing |= test_logs[i].find(automatic ?
          "TX direction=uart bytes=10 write_us=300 flush_us=12000 call_us=12300 software_de_us=nan wire_us=2605 flush_result=0" :
          "TX direction=software bytes=10 write_us=300 flush_us=12000 call_us=12300 software_de_us=12300 wire_us=2605 flush_result=0") != std::string::npos;
    }
    assert(timing);
    test_now += 50;
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    f.recover();
    assert(f.status.state == "Shared-address reply received; Touch recovered; 60s cooldown");
    test_now += 60000;
    assert(f.component.request_probe(0x0310));
    f.poll_send();
    assert(f.component.array_writes == 2 && f.component.tx.size() == 20);
  }
  for (bool automatic : {false, true}) {
    for (auto result : {uart::UARTFlushResult::UART_FLUSH_RESULT_FAILED, uart::UARTFlushResult::UART_FLUSH_RESULT_TIMEOUT}) {
      SharedFixture f(automatic);
      f.component.flush_result = result;
      f.send();
      assert(f.status.state == "Locked: UART transmit did not complete; reboot required");
      assert(f.direction.writes == (automatic ? 0 : 3));
      test_now += 60000;
      f.normal();
      assert(f.component.tx.size() == 10 && !f.component.request_probe(0x0310));
    }
  }
  {
    SharedFixture f(true);
    f.component.set_flow_control_pin(&f.direction);
    assert(!f.component.request_probe(0x0510));
    f.normal();
    assert(!f.component.start_probe_polling());
    assert(f.component.tx.empty() && f.direction.writes == 0);
  }
  {
    SharedFixture f(true);
    f.component.set_passive_mode(true);
    assert(!f.component.request_probe(0x0510));
    assert(!f.component.start_probe_polling());
    f.handoff();
    test_now += 5;
    f.component.loop();
    assert(f.component.tx.empty() && f.direction.writes == 0);
  }
  {
    SharedFixture f(true);
    f.normal();
    assert(f.component.start_probe_polling());
    f.component.loop();
    f.poll_send();
    assert(f.component.tx.size() == 10 && f.component.array_writes == 1 && f.direction.writes == 0);
    receive(f.component, 0x10, 0x12, value(0x05, 0x10, 45));
    f.recover();
    assert(f.component.get_probe_polling());
    f.component.stop_probe_polling();
    assert(!f.component.get_probe_polling());
  }
  {
    ProbeFixture f(100, false, true, true);
    assert(f.component.request_probe_with_release(0x0510));
    f.handoff();
    test_now += 5;
    f.component.loop();
    test_now += 150;
    f.component.loop();
    assert(f.component.tx.size() == 18 && f.component.array_writes == 2 && f.direction.writes == 0);
  }
}
