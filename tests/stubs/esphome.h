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

#define USE_SENSOR
#define USE_TEXT_SENSOR
#define USE_BINARY_SENSOR
#define USE_SELECT
#define USE_NUMBER
#define USE_SWITCH
#define USE_BUTTON
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGCONFIG(...) ((void)0)

namespace esphome {
inline uint32_t test_now = 0;
inline uint32_t millis() { return test_now; }
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
class UARTDevice {
 public:
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  unsigned flushes = 0;
  size_t available() { return rx.size(); }
  bool read_byte(uint8_t *byte) { if (rx.empty()) return false; *byte = rx.front(); rx.pop_front(); return true; }
  void write(uint8_t byte) { tx.push_back(byte); }
  void flush() { ++flushes; }
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
inline std::string format_hex_pretty(const std::vector<uint8_t> &) { return ""; }
inline std::string format_hex_pretty(const uint8_t *, size_t) { return ""; }
}

#define TEST_SUB_ENTITY(ns, cls, suffix, name) protected: ns::cls *name##_##suffix##_{nullptr}; public: void set_##name##_##suffix(ns::cls *value) { name##_##suffix##_ = value; }
#define SUB_SENSOR(name) TEST_SUB_ENTITY(sensor, Sensor, sensor, name)
#define SUB_TEXT_SENSOR(name) TEST_SUB_ENTITY(text_sensor, TextSensor, text_sensor, name)
#define SUB_BINARY_SENSOR(name) TEST_SUB_ENTITY(binary_sensor, BinarySensor, binary_sensor, name)
#define SUB_SELECT(name) TEST_SUB_ENTITY(select, Select, select, name)
#define SUB_NUMBER(name) TEST_SUB_ENTITY(number, Number, number, name)
#define SUB_SWITCH(name) TEST_SUB_ENTITY(switch_, Switch, switch, name)
#define SUB_BUTTON(name) TEST_SUB_ENTITY(button, Button, button, name)
