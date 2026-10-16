#include "broan.h"

namespace esphome {
namespace broan {

void BroanComponent::setProbeStatus(const char *status) {
  ESP_LOGI("broan_probe", "%s", status);
#ifdef USE_TEXT_SENSOR
  if (query_status_text_sensor_) query_status_text_sensor_->publish_state(status);
#endif
}

void BroanComponent::lockProbe(const char *reason) {
  query_state_ = QueryState::LOCKED;
  query_window_ = false;
  setProbeStatus(reason);
}

bool BroanComponent::request_probe(uint16_t opcode) {
  if (!experimental_query_ || passive_mode_ || !flow_control_pin_ ||
      query_source_address_ < 0x13 || query_source_address_ > 0x1F) return false;
  if (query_state_ != QueryState::IDLE) {
    ESP_LOGI("broan_probe", "Probe refused: pending or locked; no transmission");
    return false;
  }
  if (query_sent_ && static_cast<uint32_t>(millis() - query_sent_at_) < 60000) {
    ESP_LOGI("broan_probe", "Probe refused: 60s cooldown; no transmission");
    return false;
  }
  switch (opcode) {
    case 0x0310: case 0x0410: case 0x0510: case 0x0610:
    case 0x2350: case 0x03E0: case 0x1700: break;
    default: return false;
  }
  query_opcode_ = opcode;
  query_queued_at_ = millis();
  query_invalid_baseline_ = framer_.invalid;
  query_window_ = false;
  touch_yielded_ = false;
  query_touch_request_ = false;
  query_recovered_ = false;
  query_state_ = QueryState::QUEUED;
  ESP_LOGI("broan_probe", "Queued single read %04X from address %02X; no retries", opcode, query_source_address_);
  setProbeStatus("Queued; waiting for Touch handoff");
  return true;
}

void BroanComponent::observeProbe(uint8_t sender, uint8_t target, const std::vector<uint8_t> &payload) {
  if (query_state_ == QueryState::LOCKED) return;
  if (target == query_source_address_ && query_state_ != QueryState::WAITING) {
    lockProbe("Locked: unexpected traffic to probe address; reboot required");
    return;
  }
  if (sender == query_source_address_) {
    if (query_state_ == QueryState::WAITING && target == m_nServerAddress &&
        payload == std::vector<uint8_t>{0x20, static_cast<uint8_t>(query_opcode_ >> 8), static_cast<uint8_t>(query_opcode_)} &&
        static_cast<uint32_t>(millis() - query_sent_at_) < 10) return;
    lockProbe("Locked: probe source address already in use; reboot required");
    return;
  }
  if (query_state_ == QueryState::WAITING) {
    if (sender == m_nServerAddress && target == query_source_address_ &&
        payload.size() == 8 && payload[0] == 0x21 && payload[3] == 4 &&
        payload[1] == (query_opcode_ >> 8) && payload[2] == (query_opcode_ & 0xFF)) {
      query_state_ = QueryState::OBSERVING;
      query_reply_at_ = millis();
      ESP_LOGI("broan_probe", "Reply for %04X after %ums: %s", query_opcode_,
               static_cast<unsigned>(millis() - query_sent_at_), format_hex_pretty(payload).c_str());
      setProbeStatus("Reply received; observing bus recovery");
    } else {
      lockProbe("Locked: other traffic before probe reply; reboot required");
    }
    return;
  }
  if (query_state_ == QueryState::OBSERVING) {
    if (sender == m_nClientAddress && target == m_nServerAddress &&
        payload.size() > 1 && payload[0] == 0x20 && (payload.size() - 1) % 2 == 0)
      query_touch_request_ = true;
    if (query_touch_request_ && sender == m_nServerAddress && target == m_nClientAddress &&
        payload[0] == 0x21 && valid_register_payload(payload))
      query_recovered_ = true;
    return;
  }
  if (query_state_ != QueryState::QUEUED) return;
  const bool release = sender == m_nClientAddress && target == m_nServerAddress &&
      payload.size() == 1 && payload[0] == 0x04;
  const bool acknowledge = touch_yielded_ && sender == m_nServerAddress && target == m_nClientAddress &&
      payload.size() == 1 && payload[0] == 0x05;
  query_window_ = acknowledge;
  if (acknowledge) query_window_at_ = millis();
  touch_yielded_ = release;
}

void BroanComponent::updateProbe() {
  if (!experimental_query_ || passive_mode_) return;
  const uint32_t now = millis();
  if (query_state_ == QueryState::IDLE || query_state_ == QueryState::LOCKED) return;
  if (framer_.invalid != query_invalid_baseline_) {
    lockProbe("Locked: framing errors during probe; reboot required");
    return;
  }
  if (query_state_ == QueryState::WAITING) {
    if (static_cast<uint32_t>(now - query_sent_at_) >= 500)
      lockProbe("Locked: probe response timeout; reboot required");
    return;
  }
  if (query_state_ == QueryState::OBSERVING) {
    if (static_cast<uint32_t>(now - query_reply_at_) >= 1000) {
      if (!query_recovered_ || !have_valid_packet_ || static_cast<uint32_t>(now - last_valid_packet_) >= 500) {
        lockProbe("Locked: bus did not resume after reply; reboot required");
      } else {
        query_state_ = QueryState::IDLE;
        setProbeStatus("Reply received; bus traffic resumed; 60s cooldown");
      }
    }
    return;
  }
  if (static_cast<uint32_t>(now - query_queued_at_) >= 30000) {
    query_state_ = QueryState::IDLE;
    setProbeStatus("No eligible handoff in 30s; nothing transmitted");
    return;
  }
  if (!query_window_ || static_cast<uint32_t>(now - query_window_at_) > 8 ||
      static_cast<uint32_t>(now - last_rx_byte_) < 5 ||
      framer_.has_pending_bytes() || available()) return;
  std::vector<uint8_t> payload{0x20, static_cast<uint8_t>(query_opcode_ >> 8), static_cast<uint8_t>(query_opcode_)};
  std::vector<uint8_t> packet{1, m_nServerAddress, query_source_address_, 1, static_cast<uint8_t>(payload.size())};
  packet.insert(packet.end(), payload.begin(), payload.end());
  packet.push_back(packet_checksum(query_source_address_, m_nServerAddress, payload));
  packet.push_back(4);
  query_window_ = false;
  query_state_ = QueryState::WAITING;
  query_sent_ = true;
  query_sent_at_ = now;
  flow_control_pin_->digital_write(true);
  for (auto byte : packet) write(byte);
  flush();
  flow_control_pin_->digital_write(false);
  ESP_LOGI("broan_probe", "TX single experimental read: %s", format_hex_pretty(packet).c_str());
  setProbeStatus("Read transmitted; waiting for response");
}

}
}
