#include "broan.h"
#include <algorithm>

namespace esphome {
namespace broan {

namespace {
bool is_touch_traffic(uint8_t sender, uint8_t target, const std::vector<uint8_t> &payload) {
  if (!((sender == 0x10 && target == 0x12) || (sender == 0x12 && target == 0x10))) return false;
  switch (payload[0]) {
    case 0x02: case 0x03: return true;
    case 0x04: case 0x05: return payload.size() == 1;
    case 0x20: return sender == 0x12 && payload.size() >= 3 && (payload.size() - 1) % 2 == 0;
    case 0x21: return sender == 0x10 && payload.size() >= 4 && valid_register_payload(payload);
    case 0x40: return sender == 0x12 && payload.size() >= 4 && valid_register_payload(payload);
    case 0x41: return sender == 0x10 && payload.size() >= 3 && (payload.size() - 1) % 2 == 0;
    default: return false;
  }
}
}

void BroanComponent::setProbeStatus(const char *status) {
  ESP_LOGI("broan_probe", "%s", status);
#ifdef USE_TEXT_SENSOR
  if (query_status_text_sensor_) query_status_text_sensor_->publish_state(status);
#endif
}

void BroanComponent::lockProbe(const char *reason) {
  if (query_state_ == QueryState::WAITING || query_state_ == QueryState::OBSERVING) logProbeResult("locked");
  query_loop_.stop();
  query_state_ = QueryState::LOCKED;
  query_window_ = false;
  setProbeStatus(reason);
}

bool BroanComponent::request_probe(uint16_t opcode) {
  if (!experimental_query_ || passive_mode_ || !flow_control_pin_ ||
      query_source_address_ < 0x12 || query_source_address_ > 0x1F ||
      (query_tolerant_mode_ && query_allow_shared_address_) ||
      ((query_source_address_ == 0x12) != query_allow_shared_address_)) return false;
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
  query_checksum_baseline_ = framer_.checksum_failures;
  query_reported_errors_ = 0;
  query_resyncs_ = 0;
  query_reply_received_ = false;
  query_reply_late_ = false;
  query_rtt_ = 0;
  query_window_ = false;
  touch_yielded_ = false;
  query_touch_request_ = false;
  query_recovered_ = false;
  if (!query_allow_shared_address_) {
    touch_transaction_pending_ = false;
    touch_request_fields_.clear();
  }
  query_state_ = QueryState::QUEUED;
  query_loop_.start();
  ESP_LOGI("broan_probe", "Queued single read %04X from address %02X; no retries", opcode, query_source_address_);
  setProbeStatus("Queued; waiting for Touch handoff");
  return true;
}

void BroanComponent::observeProbe(uint8_t sender, uint8_t target, const std::vector<uint8_t> &payload) {
  if (query_state_ == QueryState::LOCKED) return;
  if (!checkProbeErrors()) return;
  if (query_state_ == QueryState::WAITING && static_cast<uint32_t>(millis() - query_sent_at_) >= probeReplyTimeout())
    handleProbeTimeout();
  if (query_state_ == QueryState::LOCKED) return;
  const bool matching_reply = sender == m_nServerAddress && target == query_source_address_ &&
      payload.size() == 8 && payload[0] == 0x21 && payload[3] == 4 &&
      payload[1] == (query_opcode_ >> 8) && payload[2] == (query_opcode_ & 0xFF);
  const bool late_reply_allowed = query_tolerant_mode_ && query_state_ == QueryState::OBSERVING &&
      static_cast<uint32_t>(millis() - query_observe_at_) < 5000 && matching_reply;
  bool touch_reply = false;
  if (query_allow_shared_address_ || query_state_ == QueryState::WAITING || query_state_ == QueryState::OBSERVING)
    touch_reply = observeTouchTransaction(sender, target, payload);
  if (query_state_ == QueryState::LOCKED) return;
  if (!query_allow_shared_address_ && target == query_source_address_ && query_state_ != QueryState::WAITING && !late_reply_allowed) {
    lockProbe("Locked: unexpected traffic to probe address; reboot required");
    return;
  }
  if (!query_allow_shared_address_ && sender == query_source_address_) {
    if (query_state_ == QueryState::WAITING && target == m_nServerAddress &&
        payload == std::vector<uint8_t>{0x20, static_cast<uint8_t>(query_opcode_ >> 8), static_cast<uint8_t>(query_opcode_)} &&
        static_cast<uint32_t>(millis() - query_sent_at_) < 10) return;
    lockProbe("Locked: probe source address already in use; reboot required");
    return;
  }
  if (query_state_ == QueryState::WAITING) {
    if (matching_reply && (!query_allow_shared_address_ || !touch_transaction_pending_)) {
      query_state_ = QueryState::OBSERVING;
      query_observe_at_ = millis();
      query_reply_received_ = true;
      query_rtt_ = millis() - query_sent_at_;
      ESP_LOGI("broan_probe", "Reply for %04X after %ums: %s", query_opcode_,
               static_cast<unsigned>(millis() - query_sent_at_), format_hex_pretty(payload).c_str());
      setProbeStatus("Reply received; observing bus recovery");
    } else if (!query_allow_shared_address_ && is_touch_traffic(sender, target, payload)) {
      if (sender == m_nClientAddress && payload[0] == 0x20) query_touch_request_ = true;
    } else {
      lockProbe("Locked: other traffic before probe reply; reboot required");
    }
    return;
  }
  if (query_state_ == QueryState::OBSERVING) {
    if (late_reply_allowed) {
      if (!query_reply_received_) {
        query_reply_received_ = true;
        query_reply_late_ = true;
        query_rtt_ = millis() - query_sent_at_;
      }
      ESP_LOGI("broan_probe", "Late/duplicate reply for %04X after %ums: %s", query_opcode_,
               static_cast<unsigned>(millis() - query_sent_at_), format_hex_pretty(payload).c_str());
      return;
    }
    if (sender == m_nClientAddress && target == m_nServerAddress &&
        payload.size() > 1 && payload[0] == 0x20 && (payload.size() - 1) % 2 == 0)
      query_touch_request_ = true;
    if (query_touch_request_ && touch_reply && sender == m_nServerAddress && target == m_nClientAddress &&
        payload[0] == 0x21 && valid_register_payload(payload)) {
      query_recovered_ = true;
      query_last_touch_reply_ = millis();
    }
    return;
  }
  if (query_state_ != QueryState::QUEUED) return;
  const bool release = sender == m_nClientAddress && target == m_nServerAddress &&
      payload.size() == 1 && payload[0] == 0x04;
  const bool acknowledge = touch_yielded_ && sender == m_nServerAddress && target == m_nClientAddress &&
      payload.size() == 1 && payload[0] == 0x05;
  query_window_ = acknowledge && !touch_transaction_pending_;
  if (query_window_) query_window_at_ = millis();
  touch_yielded_ = release;
}

void BroanComponent::updateProbe() {
  if (!experimental_query_ || passive_mode_) return;
  const uint32_t now = millis();
  if (query_state_ == QueryState::IDLE || query_state_ == QueryState::LOCKED) return;
  if (!checkProbeErrors()) return;
  if (query_state_ == QueryState::WAITING) {
    if (static_cast<uint32_t>(now - query_sent_at_) >= probeReplyTimeout()) handleProbeTimeout();
    return;
  }
  if (query_state_ == QueryState::OBSERVING) {
    if (static_cast<uint32_t>(now - query_observe_at_) >= (query_tolerant_mode_ ? 5000U : 1000U)) {
      const bool fresh = query_tolerant_mode_ ? static_cast<uint32_t>(now - query_last_touch_reply_) < 1500 :
          have_valid_packet_ && static_cast<uint32_t>(now - last_valid_packet_) < 500;
      if (!query_recovered_ || !fresh) {
        lockProbe(query_reply_received_ ? "Locked: bus did not resume after reply; reboot required" :
                  "Locked: Touch did not recover after unanswered probe; reboot required");
      } else {
        if (query_reply_received_) query_failures_ = 0;
        else if (++query_failures_ >= 3) {
          lockProbe("Locked: three consecutive unanswered probes; reboot required");
          return;
        }
        logProbeResult(query_reply_received_ ? "reply" : "no_reply");
        query_state_ = query_allow_shared_address_ ? QueryState::LOCKED : QueryState::IDLE;
        setProbeStatus(query_allow_shared_address_ ? "Shared-address reply received; Touch traffic resumed; reboot before another probe" :
                       query_reply_received_ ? "Reply received; bus traffic resumed; 60s cooldown" :
                       "No reply; Touch recovered; 60s cooldown");
      }
    }
    return;
  }
  if (static_cast<uint32_t>(now - query_queued_at_) >= 30000) {
    query_loop_.stop();
    query_state_ = QueryState::IDLE;
    setProbeStatus("No eligible handoff in 30s; nothing transmitted");
    return;
  }
  if (!query_window_ || touch_transaction_pending_ || static_cast<uint32_t>(now - query_window_at_) > 8 ||
      static_cast<uint32_t>(now - last_rx_byte_) < 5 ||
      framer_.has_pending_bytes() || available()) return;
  std::vector<uint8_t> payload{0x20, static_cast<uint8_t>(query_opcode_ >> 8), static_cast<uint8_t>(query_opcode_)};
  std::vector<uint8_t> packet{1, m_nServerAddress, query_source_address_, 1, static_cast<uint8_t>(payload.size())};
  packet.insert(packet.end(), payload.begin(), payload.end());
  packet.push_back(packet_checksum(query_source_address_, m_nServerAddress, payload));
  packet.push_back(4);
  query_window_ = false;
  query_state_ = QueryState::WAITING;
  query_loop_.stop();
  query_sent_ = true;
  ++query_attempts_;
  query_sent_at_ = now;
  flow_control_pin_->digital_write(true);
  for (auto byte : packet) write(byte);
  flush();
  flow_control_pin_->digital_write(false);
  ESP_LOGI("broan_probe", "TX single experimental read: %s", format_hex_pretty(packet).c_str());
  ESP_LOGI("broan_probe", "Probe %u register=%04X source=%02X handoff_age=%ums idle=%ums timeout=%ums error_budget=%u",
           (unsigned) query_attempts_, query_opcode_, query_source_address_, (unsigned) (now - query_window_at_),
           (unsigned) (now - last_rx_byte_), (unsigned) probeReplyTimeout(), query_tolerant_mode_ ? 2U : 0U);
  setProbeStatus("Read transmitted; waiting for response");
}

bool BroanComponent::observeTouchTransaction(uint8_t sender, uint8_t target, const std::vector<uint8_t> &payload) {
  if (sender == m_nClientAddress && target == m_nServerAddress &&
      (payload[0] == 0x20 || payload[0] == 0x40)) {
    if (touch_transaction_pending_) {
      if (!query_tolerant_mode_ || query_state_ == QueryState::QUEUED || ++query_resyncs_ > 2) {
        lockProbe("Locked: overlapping Touch transactions; reboot required");
        return false;
      }
      query_recovered_ = false;
      query_touch_request_ = false;
      ESP_LOGI("broan_probe", "Probe %u Touch transaction resync=%u at +%ums", (unsigned) query_attempts_,
               (unsigned) query_resyncs_, (unsigned) (millis() - query_sent_at_));
    }
    touch_transaction_pending_ = true;
    touch_request_fields_.clear();
    touch_reply_type_ = payload[0] == 0x20 ? 0x21 : 0x41;
    if (payload[0] == 0x20) {
      if (payload.size() <= 1 || (payload.size() - 1) % 2 != 0) return false;
      for (size_t i = 1; i + 1 < payload.size(); i += 2)
        touch_request_fields_.push_back((payload[i] << 8) | payload[i + 1]);
    } else {
      if (payload.size() <= 1 || !valid_register_payload(payload)) return false;
      for (size_t i = 1; i < payload.size(); i += 3 + payload[i + 2])
        touch_request_fields_.push_back((payload[i] << 8) | payload[i + 1]);
    }
    std::sort(touch_request_fields_.begin(), touch_request_fields_.end());
  } else if (touch_transaction_pending_ && !touch_request_fields_.empty() &&
             sender == m_nServerAddress && target == m_nClientAddress && payload[0] == touch_reply_type_) {
    std::vector<uint16_t> response_fields;
    if (payload[0] == 0x21) {
      if (!valid_register_payload(payload)) return false;
      for (size_t i = 1; i < payload.size(); i += 3 + payload[i + 2])
        response_fields.push_back((payload[i] << 8) | payload[i + 1]);
    } else {
      if ((payload.size() - 1) % 2 != 0) return false;
      for (size_t i = 1; i + 1 < payload.size(); i += 2)
        response_fields.push_back((payload[i] << 8) | payload[i + 1]);
    }
    std::sort(response_fields.begin(), response_fields.end());
    if (response_fields == touch_request_fields_) {
      touch_transaction_pending_ = false;
      return true;
    }
  }
  return false;
}

bool BroanComponent::checkProbeErrors() {
  if (query_state_ == QueryState::IDLE) return true;
  if (query_state_ == QueryState::LOCKED) return false;
  const uint32_t errors = framer_.invalid - query_invalid_baseline_;
  const uint32_t budget = query_tolerant_mode_ && query_state_ != QueryState::QUEUED ? 2 : 0;
  if (errors != query_reported_errors_) {
    query_reported_errors_ = errors;
    ESP_LOGI("broan_probe", "Probe %u framing_delta=%u checksum_delta=%u phase=%s elapsed=%ums budget=%u",
             (unsigned) query_attempts_, (unsigned) errors, (unsigned) (framer_.checksum_failures - query_checksum_baseline_),
             query_state_ == QueryState::QUEUED ? "queued" : query_state_ == QueryState::WAITING ? "waiting" : "observing",
             (unsigned) (millis() - (query_state_ == QueryState::QUEUED ? query_queued_at_ : query_sent_at_)), (unsigned) budget);
    touch_transaction_pending_ = false;
    touch_request_fields_.clear();
    query_touch_request_ = false;
    query_recovered_ = false;
  }
  if (errors > budget) {
    lockProbe(budget ? "Locked: probe error budget exceeded; reboot required" :
                      "Locked: framing errors during probe; reboot required");
    return false;
  }
  return true;
}

void BroanComponent::handleProbeTimeout() {
  if (!query_tolerant_mode_) {
    lockProbe("Locked: probe response timeout; reboot required");
    return;
  }
  query_state_ = QueryState::OBSERVING;
  query_observe_at_ = millis();
  query_touch_request_ = false;
  query_recovered_ = false;
  touch_transaction_pending_ = false;
  touch_request_fields_.clear();
  setProbeStatus("No reply within 1500ms; observing Touch recovery");
}

void BroanComponent::logProbeResult(const char *result) {
  ESP_LOGI("broan_probe", "Probe %u register=%04X result=%s reply=%s late=%u rtt=%ums invalid_delta=%u checksum_delta=%u touch_resync=%u touch_reply_age=%.1fs failures=%u",
           (unsigned) query_attempts_, query_opcode_, result, query_reply_received_ ? "yes" : "no", query_reply_late_ ? 1U : 0U,
           (unsigned) query_rtt_, (unsigned) (framer_.invalid - query_invalid_baseline_),
           (unsigned) (framer_.checksum_failures - query_checksum_baseline_), (unsigned) query_resyncs_,
           query_recovered_ ? (millis() - query_last_touch_reply_) / 1000.0f : NAN, (unsigned) query_failures_);
}

}
}
