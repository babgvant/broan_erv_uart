#include "broan.h"

namespace esphome {
namespace broan {

void BroanComponent::observeRegister(uint16_t opcode, bool request) {
  if (!register_discovery_) return;
  // Bound memory even on a noisy bus or unexpected firmware.
  if (observations_.find(opcode) == observations_.end() && observations_.size() >= 256) return;
  auto &entry = observations_[opcode];
  if (request) ++entry.requests;
  else ++entry.responses;
}

void BroanComponent::updateFreshness() {
  const uint32_t now = millis();
#ifdef USE_BINARY_SENSOR
  if (communication_ok_binary_sensor_)
    communication_ok_binary_sensor_->publish_state(have_valid_packet_ && now - last_valid_packet_ < communication_timeout_);
#endif
  for (uint32_t field = 0; field < BroanField::MAX_FIELDS; ++field) {
    if (!field_stale_[field] && (!field_seen_[field] || now - received_at_[field] >= sensor_timeout_)) {
      expireField(field);
      field_stale_[field] = true;
    }
  }
  if (register_discovery_ && now - last_diagnostics_ >= 60000) {
    last_diagnostics_ = now;
    ESP_LOGI("broan", "Packets total=%u valid=%u invalid=%u checksum=%u unknown_registers=%u age=%.1fs",
             (unsigned) framer_.packets, (unsigned) framer_.valid, (unsigned) framer_.invalid,
             (unsigned) framer_.checksum_failures, (unsigned) unknown_registers_, get_last_valid_packet_age());
    for (const auto &item : observations_)
      ESP_LOGI("broan", "Register %04X requests=%u responses=%u (cumulative)",
               item.first, (unsigned) item.second.requests, (unsigned) item.second.responses);
  }
}

void BroanComponent::expireField(uint32_t field) {
  switch (field) {
#ifdef USE_SENSOR
    case Wattage: if (power_sensor_) power_sensor_->publish_state(NAN); break;
    case TemperatureIn: if (temperature_sensor_) temperature_sensor_->publish_state(NAN); break;
    case TemperatureOut: if (temperature_out_sensor_) temperature_out_sensor_->publish_state(NAN); break;
    case FilterLife: if (filter_life_sensor_) filter_life_sensor_->publish_state(NAN); break;
    case SupplyCFM: if (supply_cfm_sensor_) supply_cfm_sensor_->publish_state(NAN); break;
    case ExhaustCFM: if (exhaust_cfm_sensor_) exhaust_cfm_sensor_->publish_state(NAN); break;
    case SupplyRPM: if (supply_rpm_sensor_) supply_rpm_sensor_->publish_state(NAN); break;
    case ExhaustRPM: if (exhaust_rpm_sensor_) exhaust_rpm_sensor_->publish_state(NAN); break;
#endif
    default: break;
  }
#ifdef USE_SENSOR
  if (field == FaultCode && fault_code_sensor_) fault_code_sensor_->publish_state(NAN);
  if (field == WarningCode && warning_code_sensor_) warning_code_sensor_->publish_state(NAN);
  if (field == BaseMode && base_mode_code_sensor_) base_mode_code_sensor_->publish_state(NAN);
  if (field == ActiveMode && active_mode_code_sensor_) active_mode_code_sensor_->publish_state(NAN);
#endif
#ifdef USE_TEXT_SENSOR
  switch (field) {
    case FaultCode: if (fault_code_text_sensor_) fault_code_text_sensor_->publish_state("Unknown"); break;
    case WarningCode: if (warning_code_text_sensor_) warning_code_text_sensor_->publish_state("Unknown"); break;
    case BaseMode: if (base_mode_text_sensor_) base_mode_text_sensor_->publish_state("Unknown"); break;
    case ActiveMode: if (active_mode_text_sensor_) active_mode_text_sensor_->publish_state("Unknown"); break;
    case Model: if (model_text_sensor_) model_text_sensor_->publish_state("Unknown"); break;
    case Firmware: if (firmware_text_sensor_) firmware_text_sensor_->publish_state("Unknown"); break;
    case FirmwareVersion: if (firmware_version_text_sensor_) firmware_version_text_sensor_->publish_state("Unknown"); break;
    case HardwareRevision: if (hardware_revision_text_sensor_) hardware_revision_text_sensor_->publish_state("Unknown"); break;
    default: break;
  }
#endif
#ifdef USE_BINARY_SENSOR
  if (field == FaultCode && fault_active_binary_sensor_) fault_active_binary_sensor_->invalidate_state();
#endif
}

}  // namespace broan
}  // namespace esphome
