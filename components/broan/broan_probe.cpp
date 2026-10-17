#include "broan.h"
#include <algorithm>

namespace esphome {
namespace broan {

namespace {
struct TouchPhase {
  uint16_t id;
  std::vector<uint16_t> fields;
};
const TouchPhase TOUCH_PHASES[]{
    {0x0020, {0x0020, 0x0220, 0x0330, 0x0430, 0x0620, 0x0720, 0x0820}},
    {0x0030, {0x0030, 0x0230}},
    {0x0830, {0x0830, 0x0930, 0x1950, 0x2050, 0x2150}},
    {0x1250, {0x1250, 0x1350, 0x1450}},
    {0x1200, {0x01E0, 0x0AF0, 0x1200, 0x1A00}},
    {0x0421, {0x0421, 0x0521, 0x0621, 0x0F22, 0x1022}},
    {0x0222, {0x0222, 0x0322, 0x0A22, 0x0C22, 0x0E22}},
    {0x0C21, {0x0850, 0x0950, 0x0C21, 0x0D21}},
};

bool known_touch_phase(uint16_t id) {
  for (const auto &phase : TOUCH_PHASES) if (phase.id == id) return true;
  return false;
}

uint16_t identify_touch_phase(const std::vector<uint16_t> &fields) {
  std::vector<uint16_t> normalized;
  for (auto field : fields) if (field != 0x1400) normalized.push_back(field);
  for (const auto &phase : TOUCH_PHASES) if (normalized == phase.fields) return phase.id;
  return 0;
}

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

void BroanComponent::lockProbe(const char *reason, bool permanent) {
  if (!permanent && !query_hard_lock_ && query_recovery_mode_ && query_polling_ && !query_trial_) {
    pauseProbePolling(reason);
    return;
  }
  query_hard_lock_ |= permanent;
  if (query_state_ == QueryState::WAITING || query_state_ == QueryState::OBSERVING) logProbeResult("locked");
  query_loop_.stop();
  query_release_pending_ = false;
  query_polling_ = false;
  query_trial_ = false;
  query_trial_remaining_ = 0;
  query_state_ = QueryState::LOCKED;
  query_window_ = false;
  setProbeStatus(reason);
}

void BroanComponent::pauseProbePolling(const char *reason, bool failure) {
  if (query_state_ == QueryState::WAITING || query_state_ == QueryState::OBSERVING) {
    logProbeResult("paused");
    if (query_automatic_) query_poll_index_ = (query_poll_index_ + 1) % 5;
  }
  if (failure) {
    if (query_poll_faults_ < 5) ++query_poll_faults_;
    query_poll_backoff_ms_ = std::min(60000U << (query_poll_faults_ - 1), 900000U);
    query_poll_pause_at_ = millis();
    query_poll_successes_ = 0;
  }
  query_loop_.stop();
  query_state_ = QueryState::PAUSED;
  query_release_pending_ = query_window_ = touch_yielded_ = query_phase_ready_ = false;
  query_automatic_ = query_poll_healthy_ = false;
  query_poll_mode_wait_ = false;
  if (failure) {
    touch_transaction_pending_ = false;
    touch_request_fields_.clear();
  }
  touch_verified_reads_ = 0;
  have_touch_read_reply_ = false;
  query_poll_invalid_baseline_ = framer_.invalid;
  std::string detail(reason);
  if (detail.find("Locked: ") == 0) detail.erase(0, 8);
  const auto suffix = detail.find("; reboot required");
  if (suffix != std::string::npos) detail.erase(suffix);
  char status[240];
  snprintf(status, sizeof(status), "Polling paused: %s; automatic recovery, backoff=%us", detail.c_str(),
           (unsigned) (query_poll_backoff_ms_ / 1000));
  setProbeStatus(status);
}

void BroanComponent::observePollingRecovery(uint8_t sender, uint8_t target, const std::vector<uint8_t> &payload) {
  if (!is_touch_traffic(sender, target, payload)) {
    touch_verified_reads_ = 0;
    query_poll_healthy_ = false;
    return;
  }
  const bool matched = observeTouchTransaction(sender, target, payload);
  if (sender == m_nServerAddress && (payload[0] == 0x21 || payload[0] == 0x41) && !matched) {
    touch_verified_reads_ = 0;
    query_poll_healthy_ = false;
  }
}

void BroanComponent::updatePollingRecovery() {
  if (!query_polling_) return;
  if (framer_.invalid != query_poll_invalid_baseline_) {
    query_poll_invalid_baseline_ = framer_.invalid;
    touch_verified_reads_ = 0;
    query_poll_healthy_ = false;
  }
  const uint32_t now = millis();
  if (!touch_mode_readback_.empty() && !query_poll_mode_wait_ &&
      static_cast<uint32_t>(now - touch_mode_write_at_) >= 10000) {
    query_poll_mode_wait_ = true;
    setProbeStatus("Polling paused: Touch mode readback unconfirmed; awaiting readback or a newer mode command");
  }
  if (touch_verified_reads_ < 3 || !have_touch_read_reply_ || !touch_mode_readback_.empty() ||
      static_cast<uint32_t>(now - touch_last_read_reply_) >= 1500) {
    query_poll_healthy_ = false;
    return;
  }
  if (!query_poll_healthy_) {
    query_poll_healthy_ = true;
    query_poll_healthy_at_ = now;
  }
  if (static_cast<uint32_t>(now - query_poll_pause_at_) < query_poll_backoff_ms_ ||
      static_cast<uint32_t>(now - query_poll_healthy_at_) < (query_poll_faults_ ? 10000U : 0U) ||
      !recoveryReady() || touch_transaction_pending_ || framer_.has_pending_bytes() || available()) return;
  query_state_ = QueryState::IDLE;
  query_poll_invalid_baseline_ = framer_.invalid;
  setProbeStatus("Telemetry polling ready; waiting for next safe read");
}

bool BroanComponent::request_probe(uint16_t opcode, uint32_t idle_ms, uint16_t after_phase) {
  if (!experimental_query_ || passive_mode_ || !probeDirectionValid() ||
      idle_ms < 1 || idle_ms > 8 ||
      (after_phase != 0 && !known_touch_phase(after_phase)) ||
      query_source_address_ < 0x12 || query_source_address_ > 0x1F ||
      (query_tolerant_mode_ && query_allow_shared_address_) ||
      (query_recovery_mode_ && (!query_allow_shared_address_ || query_tolerant_mode_)) ||
      ((query_source_address_ == 0x12) != query_allow_shared_address_)) return false;
  if (query_state_ != QueryState::IDLE) {
    ESP_LOGI("broan_probe", "Probe refused: pending or locked; no transmission");
    return false;
  }
  if (query_sent_ && static_cast<uint32_t>(millis() - query_sent_at_) < probeInterval()) {
    ESP_LOGI("broan_probe", "Probe refused: %us cooldown; no transmission", (unsigned) (probeInterval() / 1000));
    return false;
  }
  if (query_recovery_mode_) {
    if (!checkTouchWriteTimeout()) return false;
    if (!recoveryReady()) {
      ESP_LOGI("broan_probe", "Probe deferred: waiting for three matched Touch reads and write quiet period");
      return false;
    }
  }
  switch (opcode) {
    case 0x0310: case 0x0410: case 0x0510: case 0x0610:
    case 0x2350: case 0x03E0: case 0x1700: break;
    default: return false;
  }
  query_opcode_ = opcode;
  query_automatic_ = false;
  query_idle_ms_ = idle_ms;
  query_phase_filter_ = after_phase;
  query_phase_ready_ = false;
  query_phase_read_baseline_ = touch_read_serial_;
  query_phase_matches_ = query_phase_skips_ = 0;
  query_phase_at_send_ = 0;
  query_release_enabled_ = query_release_pending_ = query_release_sent_ = query_release_ack_ = false;
  query_release_ack_ms_ = 0;
  query_touch_grant_ms_ = query_touch_request_ms_ = query_touch_reply_ms_ = UINT32_MAX;
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
  ESP_LOGI("broan_probe", "Queued single read %04X from address %02X idle_min=%ums; no retries", opcode, query_source_address_,
           (unsigned) query_idle_ms_);
  setProbeStatus(query_allow_shared_address_ ? "Queued; waiting for acknowledged Touch grant" :
                 "Queued; waiting for Touch handoff");
  if (query_phase_filter_) {
    ESP_LOGI("broan_probe", "Phase filter=%04X; waiting for a new matched Touch read transaction", query_phase_filter_);
    setProbeStatus("Queued; waiting for selected Touch phase and handoff");
  }
  return true;
}

bool BroanComponent::request_probe_after_phase(uint16_t opcode, uint16_t after_phase, uint32_t idle_ms) {
  if (after_phase == 0) return false;
  return request_probe(opcode, idle_ms, after_phase);
}

bool BroanComponent::request_probe_with_release(uint16_t opcode, uint32_t idle_ms) {
  if (!query_tolerant_mode_ || query_allow_shared_address_) return false;
  if (!request_probe(opcode, idle_ms)) return false;
  query_release_enabled_ = query_release_pending_ = true;
  ESP_LOGI("broan_probe", "Read-and-release experiment armed; one release after reply or 150ms; no retries");
  return true;
}

bool BroanComponent::start_probe_polling() {
  if (query_polling_) return true;
  if (!experimental_query_ || passive_mode_ || !query_allow_shared_address_ || query_source_address_ != 0x12 ||
      query_tolerant_mode_ || !probeDirectionValid() || query_hard_lock_) return false;
  if (query_recovery_mode_ && !query_trial_) {
    query_poll_index_ = 0;
    query_poll_idle_ms_ = query_reply_received_ ? query_idle_ms_ : 5;
    query_poll_phase_ = query_reply_received_ ? query_phase_filter_ : 0;
    query_poll_invalid_baseline_ = framer_.invalid;
    query_polling_ = true;
    if (query_state_ == QueryState::LOCKED) pauseProbePolling("previous probe stopped");
    else if (query_state_ == QueryState::IDLE || query_state_ == QueryState::PAUSED)
      pauseProbePolling("waiting for verified Touch traffic and write quiet period", false);
    else setProbeStatus("Telemetry polling enabled; waiting for current probe");
    return true;
  }
  if (query_state_ != QueryState::IDLE) return false;
  if (!have_touch_read_reply_ || static_cast<uint32_t>(millis() - touch_last_read_reply_) >= 1500) {
    setProbeStatus("Polling refused: waiting for verified Touch traffic");
    return false;
  }
  if (query_recovery_mode_) {
    if (!checkTouchWriteTimeout()) return false;
    if (!recoveryReady()) {
      setProbeStatus("Polling refused: waiting for three matched Touch reads and write quiet period");
      return false;
    }
  }
  query_poll_index_ = 0;
  query_poll_idle_ms_ = query_reply_received_ ? query_idle_ms_ : 5;
  query_poll_phase_ = query_reply_received_ ? query_phase_filter_ : 0;
  query_poll_invalid_baseline_ = framer_.invalid;
  query_polling_ = true;
  setProbeStatus("Telemetry polling enabled; one read per 60s");
  ESP_LOGI("broan_probe", "Telemetry polling idle_min=%ums after_phase=%04X", (unsigned) query_poll_idle_ms_, query_poll_phase_);
  return true;
}

bool BroanComponent::start_probe_trial() {
  if (!query_recovery_mode_ || query_polling_ || query_state_ != QueryState::IDLE ||
      (query_sent_ && static_cast<uint32_t>(millis() - query_sent_at_) < 60000)) return false;
  query_trial_ = true;
  if (!start_probe_polling()) {
    query_trial_ = false;
    return false;
  }
  query_trial_remaining_ = 5;
  query_poll_idle_ms_ = 5;
  query_poll_phase_ = 0;
  setProbeStatus("Bounded trial enabled; five reads, at least 15s apart");
  return true;
}

void BroanComponent::stop_probe_polling() {
  query_polling_ = false;
  query_trial_ = false;
  query_trial_remaining_ = 0;
  query_poll_healthy_ = false;
  if (query_state_ == QueryState::PAUSED) {
    query_state_ = QueryState::IDLE;
    setProbeStatus("Telemetry polling stopped; automatic recovery cancelled");
    return;
  }
  if (query_state_ == QueryState::QUEUED && query_automatic_) {
    query_loop_.stop();
    query_window_ = false;
    query_state_ = QueryState::IDLE;
    setProbeStatus("Telemetry polling stopped; queued read cancelled");
  } else if (query_state_ == QueryState::IDLE) {
    setProbeStatus("Telemetry polling stopped; manual probe required");
  }
}

void BroanComponent::observeProbe(uint8_t sender, uint8_t target, const std::vector<uint8_t> &payload) {
  if (query_state_ == QueryState::LOCKED) return;
  if (query_state_ == QueryState::PAUSED) {
    observePollingRecovery(sender, target, payload);
    return;
  }
  if (!checkProbeErrors() || !checkTouchWriteTimeout()) {
    if (query_state_ == QueryState::PAUSED) observePollingRecovery(sender, target, payload);
    return;
  }
  if (query_state_ == QueryState::WAITING && static_cast<uint32_t>(millis() - query_sent_at_) >= probeReplyTimeout())
    handleProbeTimeout();
  if (query_state_ == QueryState::LOCKED) return;
  if ((query_state_ == QueryState::WAITING || query_state_ == QueryState::OBSERVING) && query_release_sent_ &&
      sender == m_nServerAddress && target == query_source_address_ && payload == std::vector<uint8_t>{0x05}) {
    if (!query_release_ack_) {
      query_release_ack_ = true;
      query_release_ack_ms_ = millis() - query_release_at_;
      ESP_LOGI("broan_probe", "Probe %u release acknowledged after %ums", (unsigned) query_attempts_, (unsigned) query_release_ack_ms_);
    }
    return;
  }
  const bool matching_reply = sender == m_nServerAddress && target == query_source_address_ &&
      payload.size() == 8 && payload[0] == 0x21 && payload[3] == 4 &&
      payload[1] == (query_opcode_ >> 8) && payload[2] == (query_opcode_ & 0xFF);
  const bool late_reply_allowed = query_state_ == QueryState::OBSERVING && matching_reply &&
      ((query_tolerant_mode_ && static_cast<uint32_t>(millis() - query_observe_at_) < 5000) ||
       (query_recovery_mode_ && !query_reply_received_ && static_cast<uint32_t>(millis() - query_sent_at_) < 10000));
  if (query_allow_shared_address_ && (query_state_ == QueryState::WAITING ||
      (query_recovery_mode_ && query_state_ == QueryState::OBSERVING && !query_reply_received_)) && sender == m_nClientAddress &&
      target == m_nServerAddress && payload[0] == 0x20) {
    for (size_t i = 1; i + 1 < payload.size(); i += 2) {
      if (((payload[i] << 8) | payload[i + 1]) == query_opcode_) {
        lockProbe("Locked: Touch requested probe register; reply attribution ambiguous; reboot required");
        return;
      }
    }
  }
  bool touch_reply = false;
  if (query_allow_shared_address_ || query_phase_filter_ || query_state_ == QueryState::WAITING || query_state_ == QueryState::OBSERVING)
    touch_reply = observeTouchTransaction(sender, target, payload);
  if (query_state_ == QueryState::PAUSED) {
    observePollingRecovery(sender, target, payload);
    return;
  }
  if (query_state_ == QueryState::LOCKED) return;
  if (query_allow_shared_address_ && (query_polling_ || query_state_ == QueryState::WAITING || query_state_ == QueryState::OBSERVING)) {
    if (!is_touch_traffic(sender, target, payload)) {
      lockProbe("Locked: unexpected traffic during shared probe; reboot required");
      return;
    }
    if (sender == m_nServerAddress && (payload[0] == 0x21 || payload[0] == 0x41) &&
        !touch_reply && !(query_state_ == QueryState::WAITING && matching_reply) && !late_reply_allowed) {
      lockProbe("Locked: unmatched shared-address reply; reboot required");
      return;
    }
  }
  if (query_state_ == QueryState::WAITING || query_state_ == QueryState::OBSERVING) {
    const uint32_t elapsed = millis() - query_sent_at_;
    const bool touch_read = sender == m_nClientAddress && target == m_nServerAddress &&
        payload[0] == 0x20 && payload.size() >= 3 && (payload.size() - 1) % 2 == 0;
    if (query_touch_grant_ms_ == UINT32_MAX && sender == m_nServerAddress && target == m_nClientAddress &&
        payload.size() == 1 && payload[0] == 0x04) {
      query_touch_grant_ms_ = elapsed;
      ESP_LOGI("broan_probe", "Probe %u first Touch grant at +%ums", (unsigned) query_attempts_, (unsigned) elapsed);
    }
    if (query_touch_request_ms_ == UINT32_MAX && touch_read) {
      query_touch_request_ms_ = elapsed;
      ESP_LOGI("broan_probe", "Probe %u first Touch read at +%ums", (unsigned) query_attempts_, (unsigned) elapsed);
      ESP_LOGI("broan_probe", "Probe %u first Touch read phase=%04X", (unsigned) query_attempts_, identify_touch_phase(touch_request_fields_));
    }
    if (query_touch_reply_ms_ == UINT32_MAX && touch_reply && payload[0] == 0x21) {
      query_touch_reply_ms_ = elapsed;
      ESP_LOGI("broan_probe", "Probe %u first matched Touch read reply at +%ums", (unsigned) query_attempts_, (unsigned) elapsed);
    }
  }
  if (!query_allow_shared_address_ && target == query_source_address_ && query_state_ != QueryState::WAITING && !late_reply_allowed) {
    lockProbe("Locked: unexpected traffic to probe address; reboot required");
    return;
  }
  if (!query_allow_shared_address_ && sender == query_source_address_) {
    if (query_release_sent_ && target == m_nServerAddress && payload == std::vector<uint8_t>{0x04} &&
        static_cast<uint32_t>(millis() - query_release_at_) < 10) return;
    if (query_state_ == QueryState::WAITING && target == m_nServerAddress &&
        payload == std::vector<uint8_t>{0x20, static_cast<uint8_t>(query_opcode_ >> 8), static_cast<uint8_t>(query_opcode_)} &&
        static_cast<uint32_t>(millis() - query_sent_at_) < 10) return;
    lockProbe("Locked: probe source address already in use; reboot required");
    return;
  }
  if (query_state_ == QueryState::WAITING) {
    if (matching_reply) {
      query_state_ = QueryState::OBSERVING;
      query_observe_at_ = millis();
      query_reply_received_ = true;
      have_probe_success_ = true;
      probe_success_tick_ = millis();
      probe_success_age_ms_ = 0;
      query_rtt_ = millis() - query_sent_at_;
      ESP_LOGI("broan_probe", "Reply for %04X after %ums: %s", query_opcode_,
               static_cast<unsigned>(millis() - query_sent_at_), format_hex_pretty(payload).c_str());
      setProbeStatus("Reply received; observing bus recovery");
    } else if (is_touch_traffic(sender, target, payload)) {
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
        have_probe_success_ = true;
        probe_success_tick_ = millis();
        probe_success_age_ms_ = 0;
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
      query_recovered_ = !query_recovery_mode_ || touch_verified_reads_ >= 3;
      query_last_touch_reply_ = millis();
    }
    return;
  }
  if (query_state_ != QueryState::QUEUED) return;
  const uint8_t window_sender = query_allow_shared_address_ ? m_nServerAddress : m_nClientAddress;
  const uint8_t window_target = query_allow_shared_address_ ? m_nClientAddress : m_nServerAddress;
  const bool release = sender == window_sender && target == window_target &&
      payload.size() == 1 && payload[0] == 0x04;
  const bool acknowledge = touch_yielded_ && sender == window_target && target == window_sender &&
      payload.size() == 1 && payload[0] == 0x05;
  const bool selected_phase = !query_phase_filter_ || (query_phase_ready_ && last_touch_phase_ == query_phase_filter_ &&
      static_cast<uint32_t>(millis() - last_touch_phase_at_) < 250);
  query_window_ = acknowledge && !touch_transaction_pending_ && selected_phase;
  if (acknowledge && query_phase_filter_) {
    if (!selected_phase) ++query_phase_skips_;
    query_phase_ready_ = false;
  }
  if (query_window_) query_window_at_ = millis();
  touch_yielded_ = release;
}

void BroanComponent::updateProbe() {
  if (have_probe_success_) {
    const uint32_t tick = millis();
    probe_success_age_ms_ += static_cast<uint32_t>(tick - probe_success_tick_);
    probe_success_tick_ = tick;
  }
  if (!experimental_query_ || passive_mode_) return;
  if (!checkTouchWriteTimeout()) return;
  if (query_state_ == QueryState::PAUSED) {
    updatePollingRecovery();
    return;
  }
  const uint32_t now = millis();
  if (query_polling_ && query_state_ == QueryState::QUEUED &&
      (!have_touch_read_reply_ || static_cast<uint32_t>(now - touch_last_read_reply_) >= (query_recovery_mode_ ? 10000U : 1500U))) {
    lockProbe("Locked: Touch traffic lost before polling read; reboot required");
    return;
  }
  if (query_polling_ && query_state_ == QueryState::IDLE) {
    if (framer_.invalid != query_poll_invalid_baseline_) {
      lockProbe("Locked: framing error while polling; reboot required");
      return;
    }
    if (!have_touch_read_reply_ || static_cast<uint32_t>(now - touch_last_read_reply_) >= (query_recovery_mode_ ? 10000U : 1500U)) {
      lockProbe("Locked: Touch traffic lost while polling; reboot required");
      return;
    }
    if (query_recovery_mode_ && !recoveryReady()) return;
    if (!query_sent_ || static_cast<uint32_t>(now - query_sent_at_) >= probeInterval()) {
      static const uint16_t registers[]{0x0510, 0x0310, 0x0610, 0x0410, 0x2350};
      if (!request_probe(registers[query_poll_index_], query_poll_idle_ms_, query_poll_phase_)) {
        stop_probe_polling();
        return;
      }
      query_automatic_ = true;
      return;
    }
  }
  if (query_state_ == QueryState::IDLE || query_state_ == QueryState::LOCKED) return;
  if (!checkProbeErrors()) return;
  if (query_state_ == QueryState::WAITING || query_state_ == QueryState::OBSERVING) updateProbeRelease();
  if (query_state_ == QueryState::WAITING) {
    if (static_cast<uint32_t>(now - query_sent_at_) >= probeReplyTimeout()) handleProbeTimeout();
    return;
  }
  if (query_state_ == QueryState::OBSERVING) {
    if (query_recovery_mode_ && (!query_recovered_ || !recoveryReady(false)) &&
        static_cast<uint32_t>(now - query_sent_at_) < 10000) return;
    if (static_cast<uint32_t>(now - query_observe_at_) >= (query_tolerant_mode_ || query_allow_shared_address_ ? 5000U : 1000U) ||
        (query_recovery_mode_ && static_cast<uint32_t>(now - query_sent_at_) >= 10000)) {
      const bool fresh = query_recovery_mode_ ? recoveryReady(false) : query_tolerant_mode_ || query_allow_shared_address_ ? static_cast<uint32_t>(now - query_last_touch_reply_) < 1500 :
          have_valid_packet_ && static_cast<uint32_t>(now - last_valid_packet_) < 500;
      if (!query_recovered_ || !fresh) {
        lockProbe(query_reply_received_ ? "Locked: bus did not resume after reply; reboot required" :
                  "Locked: Touch did not recover after unanswered probe; reboot required");
      } else {
        if (!query_reply_received_ && query_recovery_mode_ && query_polling_ && !query_trial_) {
          ++query_failures_;
          pauseProbePolling("telemetry reply missing; Touch recovered");
          return;
        }
        if (query_reply_received_) query_failures_ = 0;
        else if (++query_failures_ >= 3) {
          lockProbe("Locked: three consecutive unanswered probes; reboot required");
          return;
        }
        logProbeResult(query_reply_received_ ? "reply" : "no_reply");
        if (query_reply_received_ && query_recovery_mode_ && query_polling_ && !query_trial_ && ++query_poll_successes_ >= 3) {
          query_poll_faults_ = 0;
          query_poll_backoff_ms_ = 0;
          query_poll_successes_ = 0;
        }
        query_state_ = QueryState::IDLE;
        if (query_automatic_) query_poll_index_ = (query_poll_index_ + 1) % 5;
        if (query_trial_ && query_trial_remaining_ == 0) {
          stop_probe_polling();
          setProbeStatus("Bounded trial complete; five reads sent; polling off");
          return;
        }
        setProbeStatus(query_trial_ ? "Bounded trial; Touch recovered; 15s cooldown" :
                       query_polling_ ? "Telemetry polling; Touch recovered; 60s cooldown" :
                       query_allow_shared_address_ ? "Shared-address reply received; Touch recovered; 60s cooldown" :
                       query_reply_received_ ? "Reply received; bus traffic resumed; 60s cooldown" :
                       "No reply; Touch recovered; 60s cooldown");
      }
    }
    return;
  }
  if (static_cast<uint32_t>(now - query_queued_at_) >= 30000) {
    if (query_recovery_mode_ && query_polling_ && !query_trial_) {
      pauseProbePolling("no eligible handoff in 30s; nothing transmitted");
      return;
    }
    query_loop_.stop();
    query_state_ = QueryState::IDLE;
    const bool polling = query_polling_;
    query_polling_ = false;
    query_trial_ = false;
    query_trial_remaining_ = 0;
    setProbeStatus(polling ? "Polling stopped: no eligible handoff in 30s; nothing transmitted" :
                   query_phase_filter_ ? "No matching Touch phase/handoff in 30s; nothing transmitted" :
                   "No eligible handoff in 30s; nothing transmitted");
    return;
  }
  if (!query_window_ || touch_transaction_pending_ || static_cast<uint32_t>(now - query_window_at_) > 8 ||
      (query_recovery_mode_ && !recoveryReady()) ||
      (query_phase_filter_ && static_cast<uint32_t>(now - last_touch_phase_at_) >= 250) ||
      static_cast<uint32_t>(now - last_rx_byte_) < query_idle_ms_ ||
      framer_.has_pending_bytes() || available()) return;
  std::vector<uint8_t> payload{0x20, static_cast<uint8_t>(query_opcode_ >> 8), static_cast<uint8_t>(query_opcode_)};
  std::vector<uint8_t> packet{1, m_nServerAddress, query_source_address_, 1, static_cast<uint8_t>(payload.size())};
  packet.insert(packet.end(), payload.begin(), payload.end());
  packet.push_back(packet_checksum(query_source_address_, m_nServerAddress, payload));
  packet.push_back(4);
  query_window_ = false;
  query_state_ = QueryState::WAITING;
  if (!query_release_pending_) query_loop_.stop();
  query_sent_ = true;
  ++query_attempts_;
  query_sent_at_ = now;
  if (query_recovery_mode_) touch_verified_reads_ = 0;
  if (query_trial_ && query_trial_remaining_) --query_trial_remaining_;
  query_phase_at_send_ = last_touch_phase_;
  query_phase_age_at_send_ = now - last_touch_phase_at_;
  if (!transmitProbePacket(packet)) return;
  ESP_LOGI("broan_probe", "TX single experimental read: %s", format_hex_pretty(packet).c_str());
  ESP_LOGI("broan_probe", "Probe %u register=%04X source=%02X handoff_age=%ums idle=%ums idle_min=%ums timeout=%ums error_budget=%u",
           (unsigned) query_attempts_, query_opcode_, query_source_address_, (unsigned) (now - query_window_at_),
           (unsigned) (now - last_rx_byte_), (unsigned) query_idle_ms_, (unsigned) probeReplyTimeout(), query_tolerant_mode_ ? 2U : 0U);
  ESP_LOGI("broan_probe", "Probe %u transmit_window=%s", (unsigned) query_attempts_,
           query_allow_shared_address_ ? "acknowledged Touch grant" : "Touch release acknowledged by ERV");
  ESP_LOGI("broan_probe", "Probe %u phase requested=%04X preceding=%04X response_age=%.0fms matches=%u skipped_windows=%u",
           (unsigned) query_attempts_, query_phase_filter_, query_phase_at_send_,
           query_phase_at_send_ ? static_cast<float>(query_phase_age_at_send_) : NAN,
           (unsigned) query_phase_matches_, (unsigned) query_phase_skips_);
  setProbeStatus("Read transmitted; waiting for response");
}

void BroanComponent::updateProbeRelease() {
  if (!query_release_pending_) return;
  const uint32_t now = millis();
  const uint32_t elapsed = now - query_sent_at_;
  if (elapsed >= 350) {
    query_release_pending_ = false;
    query_loop_.stop();
    ESP_LOGI("broan_probe", "Probe %u release skipped: no eligible idle gap within 350ms", (unsigned) query_attempts_);
    return;
  }
  if ((!query_reply_received_ && elapsed < 150) || static_cast<uint32_t>(now - last_rx_byte_) < 5 ||
      framer_.has_pending_bytes() || available()) return;
  std::vector<uint8_t> payload{0x04};
  std::vector<uint8_t> packet{1, m_nServerAddress, query_source_address_, 1, 1, 0x04};
  packet.push_back(packet_checksum(query_source_address_, m_nServerAddress, payload));
  packet.push_back(4);
  query_release_pending_ = false;
  query_release_sent_ = true;
  query_release_at_ = now;
  query_loop_.stop();
  if (!transmitProbePacket(packet)) return;
  ESP_LOGI("broan_probe", "Probe %u TX control release at +%ums idle=%ums: %s", (unsigned) query_attempts_,
           (unsigned) elapsed, (unsigned) (now - last_rx_byte_), format_hex_pretty(packet).c_str());
}

bool BroanComponent::transmitProbePacket(const std::vector<uint8_t> &packet) {
  const uint32_t started = micros();
  if (!query_uart_direction_) flow_control_pin_->digital_write(true);
  write_array(packet.data(), packet.size());
  const uint32_t queued = micros();
  const auto result = flush();
  const uint32_t drained = micros();
  if (!query_uart_direction_) flow_control_pin_->digital_write(false);
  const uint32_t finished = micros();
  ESP_LOGI("broan_probe", "Probe %u TX direction=%s bytes=%u write_us=%u flush_us=%u call_us=%u software_de_us=%.0f wire_us=%u flush_result=%u",
           (unsigned) query_attempts_, query_uart_direction_ ? "uart" : "software", (unsigned) packet.size(),
           (unsigned) (queued - started), (unsigned) (drained - queued), (unsigned) (finished - started),
           query_uart_direction_ ? NAN : static_cast<float>(finished - started),
           (unsigned) ((packet.size() * 10000000UL + 38399) / 38400), (unsigned) result);
  if (result != uart::UARTFlushResult::UART_FLUSH_RESULT_SUCCESS &&
      result != uart::UARTFlushResult::UART_FLUSH_RESULT_ASSUMED_SUCCESS) {
    lockProbe("Locked: UART transmit did not complete; reboot required", true);
    return false;
  }
  return true;
}

bool BroanComponent::observeTouchTransaction(uint8_t sender, uint8_t target, const std::vector<uint8_t> &payload) {
  if (query_recovery_mode_ && sender == m_nServerAddress && target == m_nClientAddress &&
      payload[0] == 0x21 && valid_register_payload(payload)) {
    for (size_t i = 1; i < payload.size(); i += 3 + payload[i + 2]) {
      const uint16_t field = (payload[i] << 8) | payload[i + 1];
      if (field == 0x0220 && (payload[i + 2] == 1 || payload[i + 2] == 4)) {
        const auto mode = touch_mode_readback_.find(0x0020);
        if (mode != touch_mode_readback_.end() && mode->second.size() == 1) {
          const uint8_t code = mode->second[0];
          if ((code == 1 || code == 8 || code == 9 || code == 10 || code == 11 || code == 17) &&
              payload[i + 3] == code && std::all_of(payload.begin() + i + 4,
                  payload.begin() + i + 3 + payload[i + 2], [](uint8_t byte) { return byte == 0; })) {
            ESP_LOGI("broan_probe", "Touch mode register 0020 confirmed by ERV base-mode readback after %ums",
                     (unsigned) (millis() - touch_mode_write_at_));
            touch_mode_readback_.erase(mode);
          }
        }
      }
      auto expected = touch_mode_readback_.find(field);
      if (expected != touch_mode_readback_.end() && expected->second ==
          std::vector<uint8_t>(payload.begin() + i + 3, payload.begin() + i + 3 + payload[i + 2])) {
        ESP_LOGI("broan_probe", "Touch mode register %04X confirmed by ERV readback after %ums", field,
                 (unsigned) (millis() - touch_mode_write_at_));
        touch_mode_readback_.erase(expected);
      }
    }
  }
  if (sender == m_nClientAddress && target == m_nServerAddress &&
      (payload[0] == 0x20 || payload[0] == 0x40)) {
    if (query_state_ == QueryState::QUEUED) query_phase_ready_ = false;
    if (touch_transaction_pending_) {
      if (query_recovery_mode_ && touch_reply_type_ == 0x41) {
        if (query_state_ == QueryState::PAUSED) pauseProbePolling("Touch write unacknowledged during recovery");
        else {
          lockProbe("Locked: Touch write unacknowledged before next transaction; reboot required");
          return false;
        }
      }
      if (!query_recovery_mode_ && (query_polling_ || query_state_ != QueryState::IDLE) &&
          (!query_tolerant_mode_ || query_state_ == QueryState::QUEUED || ++query_resyncs_ > 2)) {
        lockProbe("Locked: overlapping Touch transactions; reboot required");
        return false;
      }
      query_recovered_ = false;
      query_touch_request_ = false;
      if (query_recovery_mode_) {
        ++query_resyncs_;
        touch_verified_reads_ = 0;
        query_poll_healthy_ = false;
      }
      ESP_LOGI("broan_probe", "Probe %u Touch transaction resync=%u at +%ums", (unsigned) query_attempts_,
               (unsigned) query_resyncs_, (unsigned) (millis() - query_sent_at_));
    }
    touch_transaction_pending_ = true;
    touch_request_at_ = millis();
    touch_request_fields_.clear();
    touch_reply_type_ = payload[0] == 0x20 ? 0x21 : 0x41;
    if (payload[0] == 0x20) {
      if (payload.size() <= 1 || (payload.size() - 1) % 2 != 0) return false;
      ++touch_read_serial_;
      for (size_t i = 1; i + 1 < payload.size(); i += 2)
        touch_request_fields_.push_back((payload[i] << 8) | payload[i + 1]);
    } else {
      if (payload.size() <= 1 || !valid_register_payload(payload)) return false;
      for (size_t i = 1; i < payload.size(); i += 3 + payload[i + 2])
        touch_request_fields_.push_back((payload[i] << 8) | payload[i + 1]);
      if (query_recovery_mode_) {
        have_touch_control_ = true;
        touch_control_at_ = millis();
        query_window_ = false;
        ESP_LOGI("broan_probe", "Touch write observed: %s; telemetry deferred", format_hex_pretty(payload).c_str());
        for (size_t i = 1; i < payload.size(); i += 3 + payload[i + 2]) {
          const uint16_t field = (payload[i] << 8) | payload[i + 1];
          if (field == 0x0020 || field == 0x0820) {
            query_poll_mode_wait_ = false;
            touch_mode_readback_[field] = std::vector<uint8_t>(payload.begin() + i + 3,
                payload.begin() + i + 3 + payload[i + 2]);
            touch_mode_write_at_ = millis();
          }
        }
      }
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
      if (payload[0] == 0x21) {
        if (!have_touch_read_reply_ || static_cast<uint32_t>(millis() - touch_last_read_reply_) >= 1500)
          touch_verified_reads_ = 0;
        if (touch_verified_reads_ < 3) ++touch_verified_reads_;
        touch_last_read_reply_ = millis();
        have_touch_read_reply_ = true;
        last_touch_phase_ = identify_touch_phase(touch_request_fields_);
        last_touch_phase_at_ = millis();
        if (query_state_ == QueryState::QUEUED && query_phase_filter_) {
          query_phase_ready_ = last_touch_phase_ == query_phase_filter_ && touch_read_serial_ != query_phase_read_baseline_;
          if (query_phase_ready_) {
            ++query_phase_matches_;
            ESP_LOGI("broan_probe", "Selected Touch phase %04X completed; waiting for handoff", last_touch_phase_);
          }
        }
      } else if (query_recovery_mode_) {
        ESP_LOGI("broan_probe", "Touch write acknowledged after %ums: %s", (unsigned) (millis() - touch_request_at_),
                 format_hex_pretty(payload).c_str());
      }
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

bool BroanComponent::checkTouchWriteTimeout() {
  if (!query_recovery_mode_) return true;
  if (query_state_ == QueryState::LOCKED) return false;
  if (query_state_ == QueryState::PAUSED) {
    if (touch_transaction_pending_ && touch_reply_type_ == 0x41 &&
        static_cast<uint32_t>(millis() - touch_request_at_) >= 1500) {
      pauseProbePolling("Touch write acknowledgement timeout during recovery");
    }
    return true;
  }
  if (!touch_mode_readback_.empty() && static_cast<uint32_t>(millis() - touch_mode_write_at_) >= 10000) {
    lockProbe("Locked: Touch mode readback not confirmed within 10s; reboot required");
    return false;
  }
  if (touch_transaction_pending_ && touch_reply_type_ == 0x41 &&
      static_cast<uint32_t>(millis() - touch_request_at_) >= 1500) {
    lockProbe("Locked: Touch write acknowledgement timeout; reboot required");
    return false;
  }
  return true;
}

bool BroanComponent::recoveryReady(bool require_quiet) const {
  return touch_verified_reads_ >= 3 && have_touch_read_reply_ && touch_mode_readback_.empty() &&
      static_cast<uint32_t>(millis() - touch_last_read_reply_) < 1500 &&
      !(touch_transaction_pending_ && touch_reply_type_ == 0x41) &&
      (!require_quiet || !have_touch_control_ || static_cast<uint32_t>(millis() - touch_control_at_) >= 3000);
}

void BroanComponent::handleProbeTimeout() {
  if (!query_tolerant_mode_ && !query_recovery_mode_) {
    lockProbe("Locked: probe response timeout; reboot required");
    return;
  }
  query_state_ = QueryState::OBSERVING;
  query_observe_at_ = millis();
  query_touch_request_ = false;
  query_recovered_ = false;
  if (!query_recovery_mode_) {
    touch_transaction_pending_ = false;
    touch_request_fields_.clear();
  }
  setProbeStatus(query_recovery_mode_ ? "No reply within 500ms; observing Touch recovery" :
                 "No reply within 1500ms; observing Touch recovery");
}

void BroanComponent::logProbeResult(const char *result) {
  ESP_LOGI("broan_probe", "Probe %u result_phase requested=%04X preceding=%04X", (unsigned) query_attempts_,
           query_phase_filter_, query_phase_at_send_);
  ESP_LOGI("broan_probe", "Probe %u register=%04X result=%s reply=%s late=%u rtt=%ums invalid_delta=%u checksum_delta=%u touch_resync=%u touch_reply_age=%.1fs failures=%u",
           (unsigned) query_attempts_, query_opcode_, result, query_reply_received_ ? "yes" : "no", query_reply_late_ ? 1U : 0U,
           (unsigned) query_rtt_, (unsigned) (framer_.invalid - query_invalid_baseline_),
           (unsigned) (framer_.checksum_failures - query_checksum_baseline_), (unsigned) query_resyncs_,
           query_recovered_ ? (millis() - query_last_touch_reply_) / 1000.0f : NAN, (unsigned) query_failures_);
  ESP_LOGI("broan_probe", "Probe %u timing idle_min=%ums touch_grant=%.0fms touch_read=%.0fms touch_reply=%.0fms",
           (unsigned) query_attempts_, (unsigned) query_idle_ms_,
           query_touch_grant_ms_ == UINT32_MAX ? NAN : static_cast<float>(query_touch_grant_ms_),
           query_touch_request_ms_ == UINT32_MAX ? NAN : static_cast<float>(query_touch_request_ms_),
           query_touch_reply_ms_ == UINT32_MAX ? NAN : static_cast<float>(query_touch_reply_ms_));
  if (query_release_enabled_)
    ESP_LOGI("broan_probe", "Probe %u release sent=%u ack=%u after=%.0fms ack_rtt=%.0fms", (unsigned) query_attempts_,
             query_release_sent_ ? 1U : 0U, query_release_ack_ ? 1U : 0U,
             query_release_sent_ ? static_cast<float>(query_release_at_ - query_sent_at_) : NAN,
             query_release_ack_ ? static_cast<float>(query_release_ack_ms_) : NAN);
}

}
}
