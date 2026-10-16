#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <type_traits>
#include <deque>
#include <cstdarg>

inline std::vector<std::string> test_logs;
inline void test_log(const char *, const char *format, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  test_logs.emplace_back(buffer);
}

#define USE_SENSOR
#define USE_TEXT_SENSOR
#define USE_BINARY_SENSOR
#define USE_SELECT
#define USE_NUMBER
#define USE_SWITCH
#define USE_BUTTON
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) test_log(__VA_ARGS__)
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_LOGCONFIG(...) ((void)0)

namespace esphome {
inline uint32_t test_now = 0;
inline uint32_t millis() { return test_now; }
inline uint32_t test_micros_extra = 0;
inline uint32_t micros() { return test_now * 1000U + test_micros_extra; }
inline unsigned test_fast_loop_starts = 0, test_fast_loop_stops = 0;
class HighFrequencyLoopRequester {
 public:
  void start() { if (!started_) { started_ = true; ++test_fast_loop_starts; } }
  void stop() { if (started_) { started_ = false; ++test_fast_loop_stops; } }
 private:
  bool started_{false};
};
namespace setup_priority { constexpr float BUS = 100; }
class Component {
 public:
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
};
struct GPIOPin {
  unsigned writes = 0, setups = 0;
  void setup() { ++setups; }
  void digital_write(bool) { ++writes; }
  void dump_summary(char *, unsigned) {}
};
namespace uart {
enum class UARTFlushResult { UART_FLUSH_RESULT_SUCCESS, UART_FLUSH_RESULT_TIMEOUT, UART_FLUSH_RESULT_FAILED, UART_FLUSH_RESULT_ASSUMED_SUCCESS };
class UARTDevice {
 public:
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  unsigned flushes = 0;
  unsigned array_writes = 0;
  uint32_t write_delay_us = 0, flush_delay_us = 0;
  UARTFlushResult flush_result = UARTFlushResult::UART_FLUSH_RESULT_SUCCESS;
  size_t available() { return rx.size(); }
  bool read_byte(uint8_t *byte) { if (rx.empty()) return false; *byte = rx.front(); rx.pop_front(); return true; }
  void write(uint8_t byte) { tx.push_back(byte); }
  void write_array(const uint8_t *data, size_t len) { ++array_writes; tx.insert(tx.end(), data, data + len); test_micros_extra += write_delay_us; }
  UARTFlushResult flush() { ++flushes; test_micros_extra += flush_delay_us; return flush_result; }
};
}
namespace sensor {
struct Sensor { float state = NAN; unsigned updates = 0; void publish_state(float value) { state = value; ++updates; } };
}
namespace text_sensor {
struct TextSensor { std::string state; unsigned updates = 0; void publish_state(const std::string &value) { state = value; ++updates; } };
}
namespace binary_sensor {
struct BinarySensor { bool state = false, has_state = false; void publish_state(bool value) { state = value; has_state = true; } void invalidate_state() { has_state = false; } };
}
namespace select { using Select = text_sensor::TextSensor; }
namespace number { using Number = sensor::Sensor; }
namespace switch_ { using Switch = binary_sensor::BinarySensor; }
namespace button { struct Button {}; }
inline std::string format_hex_pretty(const uint8_t *data, size_t size) {
  std::string result;
  for (size_t i = 0; i < size; ++i) {
    char buffer[4];
    snprintf(buffer, sizeof(buffer), "%02X", data[i]);
    if (i) result += ".";
    result += buffer;
  }
  return result;
}
inline std::string format_hex_pretty(const std::vector<uint8_t> &data) {
  return format_hex_pretty(data.data(), data.size());
}
}

#define TEST_SUB_ENTITY(ns, cls, suffix, name) protected: ns::cls *name##_##suffix##_{nullptr}; public: void set_##name##_##suffix(ns::cls *value) { name##_##suffix##_ = value; }
#define SUB_SENSOR(name) TEST_SUB_ENTITY(sensor, Sensor, sensor, name)
#define SUB_TEXT_SENSOR(name) TEST_SUB_ENTITY(text_sensor, TextSensor, text_sensor, name)
#define SUB_BINARY_SENSOR(name) TEST_SUB_ENTITY(binary_sensor, BinarySensor, binary_sensor, name)
#define SUB_SELECT(name) TEST_SUB_ENTITY(select, Select, select, name)
#define SUB_NUMBER(name) TEST_SUB_ENTITY(number, Number, number, name)
#define SUB_SWITCH(name) TEST_SUB_ENTITY(switch_, Switch, switch, name)
#define SUB_BUTTON(name) TEST_SUB_ENTITY(button, Button, button, name)
