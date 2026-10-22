#include "broan.h"
#include <cinttypes>

namespace esphome {
namespace broan {


void BroanComponent::setup()
{
	//uart::UARTDevice::setup();
	Component::setup();
	//esp_log_level_set("broan", ESP_LOG_DEBUG);


	for( int i=0; i<BroanField::MAX_FIELDS; i++ )
		m_vecFields[i].markDirty();

    if(flow_control_pin_ && !passive_mode_ && !query_uart_direction_)
    {
        this->flow_control_pin_->setup();
        if (experimental_query_) this->flow_control_pin_->digital_write(false);
    }
    if (experimental_query_) {
        char status[100];
        snprintf(status, sizeof(status), "Idle; source 0x%02X; %s; manual probe required", query_source_address_,
                 query_recovery_mode_ ? "shared recovery" : query_tolerant_mode_ ? "tolerant" : query_allow_shared_address_ ? "shared strict" : "strict");
        setProbeStatus(status);
        ESP_LOGI("broan_probe", "RS485 direction=%s", query_uart_direction_ ? "UART-managed" : "software GPIO");
    }

	// Initial state
#ifdef USE_TEXT_SENSOR
	if( !passive_mode_ && !experimental_query_ && warning_code_text_sensor_ )
		warning_code_text_sensor_->publish_state("OK");
	if( !passive_mode_ && !experimental_query_ && fault_code_text_sensor_ )
		fault_code_text_sensor_->publish_state("OK");
#endif
}


void BroanComponent::loop()
{
  auto receive = [this](uint8_t sender, uint8_t target, const std::vector<uint8_t> &payload) {
    last_valid_packet_ = millis();
    have_valid_packet_ = true;
    if (log_packets_) {
      std::vector<uint8_t> packet{1, target, sender, 1, static_cast<uint8_t>(payload.size())};
      packet.insert(packet.end(), payload.begin(), payload.end());
      packet.push_back(packet_checksum(sender, target, payload));
      packet.push_back(4);
      ESP_LOGI("broan", "RX %02X -> %02X %s: %s", sender, target,
               payload[0] == 0x20 ? "request" : payload[0] == 0x21 ? "response" : "message",
               format_hex_pretty(packet).c_str());
    }
    if (experimental_query_ && !passive_mode_) observeProbe(sender, target, payload);
    handleMessage(sender, target, payload);
  };
  framer_.expire(millis(), receive);
  uint8_t byte;
  while (available() && read_byte(&byte)) {
    last_rx_byte_ = millis();
    framer_.feed(byte, millis(), receive);
  }
#ifdef USE_BINARY_SENSOR
  if (!passive_mode_ && !experimental_query_ && !field_stale_[ActiveMode] &&
      (!field_seen_[ActiveMode] || millis() - received_at_[ActiveMode] >= sensor_timeout_)) {
    if (operating_binary_sensor_) operating_binary_sensor_->invalidate_state();
    field_stale_[ActiveMode] = true;
  }
#endif
  if (passive_mode_ || experimental_query_) {
    updateFreshness();
    if (experimental_query_ && !passive_mode_) updateProbe();
  }
  else {
    replyIfAllowed();
    runTasks();
  }
}

void BroanComponent::dump_config()
{
	ESP_LOGCONFIG("broan", "Broan: %s", passive_mode_ ? "passive" : experimental_query_ ? "experimental query" : "active");
	if(flow_control_pin_)
	{
		char buffer[255];
		this->flow_control_pin_->dump_summary( buffer, 255 );
		ESP_LOGCONFIG("broan", "Flow Control Pin: %s", buffer );
	}
}

float BroanComponent::get_setup_priority() const
{
  // After UART bus
  return setup_priority::BUS - 1.0f;
}

void BroanComponent::writeRegisters( const std::vector<BroanField_t> &values )
{
	if (passive_mode_ || experimental_query_) return;

	std::vector<uint8_t> message;

	message.push_back(0x40); // Write

	for( BroanField_t value : values )
	{
		message.push_back( value.m_nOpcodeHigh );
		message.push_back( value.m_nOpcodeLow );
		uint8_t len = value.m_nType == BroanFieldType::Byte ? 0x01 : 0x04;
		message.push_back( len );
		for( int i=0; i<len; i++ )
			message.push_back( value.m_value.m_rgBytes[i] );
	}

	queueMessage( message );
}

void esp_log_vector_hex(const char* tag, const std::vector<uint8_t>& message) {
    if (message.empty()) {
        ESP_LOGW(tag, "Message vector is empty");
        return;
    }
    std::string hex_string;
    for (size_t i = 0; i < message.size(); ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", message[i]);
        hex_string += buf;
        // Optional: newline every 16 bytes for readability
        if ((i + 1) % 16 == 0) {
            ESP_LOGW(tag, "%s", hex_string.c_str());
            hex_string.clear();
        }
    }
    if (!hex_string.empty()) {
        ESP_LOGW(tag, "%s", hex_string.c_str());
    }
}

void BroanComponent::handleMessage(uint8_t sender, uint8_t target, const std::vector<uint8_t>& message)
{
  if (message.empty()) return;
  if (passive_mode_ || experimental_query_) {
    if (target == m_nServerAddress && message[0] == 0x20 && (message.size() - 1) % 2 == 0) {
      for (size_t i = 1; i + 1 < message.size(); i += 2)
        observeRegister((message[i] << 8) | message[i + 1], true);
    }
    // Read responses carry their own register IDs, lengths and values.
    // Only ERV-originated data is telemetry; writes are commanded values.
    if (sender == m_nServerAddress && message[0] == 0x21) parseBroanFields(message);
    return;
  }

	if( target == m_nServerAddress )
	{
		if( message[0] == 0x03 )
			m_bWaitForRemote = false;
	}
#ifndef LISTEN_ONLY
	if (target != m_nClientAddress) return;
#endif

	int m_nType = message[0];
	switch (m_nType)
	{
		case 0x02:
		{
			// Respond to ping
			std::vector<uint8_t> reply = {0x03};
			reply.insert(reply.end(), message.begin() + 1, message.end());

			send(reply);

			ESP_LOGD("broan","0x02 Ping");
			m_bERVReady = true;
			break;
		}
		case 0x04:
		{
			// Flow control
			m_nLastHadControl = millis();
			m_bHaveControl = true;
			m_bExpectingReply = false;
			// ERV won't re-ping us if we drop, so just assume if we're getting flow
			// control messages it's ready for us to start feeding it data.
			m_bERVReady = true;

			// Ack that we have control. We'll send any queued messages then release with 0x04
			send({ 0x05 });
			//ESP_LOGD("broan","Got flow control");
			break;
		}
		case 0x05:
			// ERV has confirmed it has control, no-op
			break;

		case 0x41:
		{
			// set register ACK, mark all fields dirty
			for( int i=1; i+1<message.size(); i+=2)
			{
				BroanField_t *pField = lookupField(message[i], message[i+1]);
				if( !pField )
				{
					ESP_LOGW("broan", "Got write response for unknown field %02X %02X", message[i], message[i+1]);
					continue;
				}
				pField->markDirty();
			}
			m_bExpectingReply = false;


			break;
		}
		case 0x21:
		{
			// Request register response
			parseBroanFields(message);
			m_bExpectingReply = false;

			break;
		}
#ifdef LISTEN_ONLY
		case 0x20:
			break;
#endif
		default:
		{
			// Log unhandled m_nType
			ESP_LOGW("broan", "Unhandled m_nType %02X", m_nType);
			esp_log_vector_hex("broan", message );
			break;
		}
	}
}

void BroanComponent::replyIfAllowed()
{
	if (passive_mode_ || experimental_query_) return;

	uint32_t time = millis();
	if( m_nLastHadControl + CONTROL_TIMEOUT < time )
	{
		ESP_LOGW("broan","ERV has not yielded control in over %ims, communication has likely failed. Please restart the device.", CONTROL_TIMEOUT);
		m_bERVReady = false;
		m_nLastHadControl = time;
	}

	if( !m_bHaveControl || m_bExpectingReply )
		return;

	if( m_vecSendQueue.size() > 0 )
	{
		send( m_vecSendQueue.front() );
		m_vecSendQueue.pop_front();
		m_bExpectingReply = true;
		m_bHaveSentMessage = true;
		return;
	}

	if( m_bHaveControl && !m_bExpectingReply && m_vecSendQueue.size() == 0 )
	{
		// Release control.
		send( { 0x04 } );
		m_bHaveControl = false;
		m_bERVReady = true;
		m_bHaveSentMessage = false;
		return;
	}

}

void BroanComponent::queueMessage(std::vector<uint8_t>& message)
{
	if (passive_mode_ || experimental_query_) return;

	if( m_vecSendQueue.size() > 20 )
	{
		ESP_LOGW("broan","Dropping queued message: Stack is full. (Tried to queue %02X)",message[0]);
		return;
	}
	m_vecSendQueue.push_back(message);
}


void BroanComponent::parseBroanFields(const std::vector<uint8_t>& message)
{
  if (!valid_register_payload(message)) {
    ESP_LOGD("broan", "Malformed register response");
    return;
  }
  // Validate the entire response before publishing any of it.
  for (size_t i = 1; i < message.size();) {
    const auto *field = lookupField(message[i], message[i + 1]);
    const uint8_t len = message[i + 2];
    // VTTOUCH traffic also reports these numeric mode registers as bytes.
    const bool byte_mode = len == 1 &&
        (field == &m_vecFields[BaseMode] || field == &m_vecFields[ActiveMode]);
    if (field && ((field->m_nType == BroanFieldType::Byte && len != 1) ||
                  ((field->m_nType == BroanFieldType::Int || field->m_nType == BroanFieldType::Float) && len != 4 && !byte_mode) ||
                  (field->m_nType == BroanFieldType::Void && len != 0) ||
                  ((field == &m_vecFields[FirmwareVersion] || field == &m_vecFields[HardwareRevision]) && len < 3))) {
      ESP_LOGD("broan", "Unexpected register length");
      return;
    }
    i += 3 + len;
  }

    size_t i = 1;

	std::string strBuf;

    while (i < message.size())
    {
        uint8_t nOpcodeHigh = message[i++];
        uint8_t nOpcodeLow  = message[i++];
		uint16_t nOpcode = ( nOpcodeHigh << 8 ) | nOpcodeLow;
		size_t len = message[i++];
		uint32_t nDataPos = i;

		i += len;

		uint32_t unField = lookupFieldIndex(nOpcodeHigh, nOpcodeLow);
        const bool changed = observeResponse(nOpcode, message.data() + nDataPos, len);
        if (unField == INVALID_FIELD) {
            ++unknown_registers_;
            if (log_unknown_registers_ && changed) handleUnknownField(nOpcodeHigh, nOpcodeLow, len, nDataPos, message);
            continue;
        }
        received_at_[unField] = millis();
        field_seen_[unField] = true;
        field_stale_[unField] = false;

		BroanField_t *pField = &m_vecFields[unField];
		if( !pField )
		{
			handleUnknownField(nOpcodeHigh, nOpcodeLow, len, nDataPos, message);
			continue;
		}

		switch( pField->m_nType )
		{
			case BroanFieldType::String: 
				strBuf.assign( reinterpret_cast<const char*>(message.data() + nDataPos), len );
			break;
			default: 
				// Clear upper bytes when a register changes from four-byte to byte encoding.
				pField->m_value.m_nValue = 0;
				for (size_t b = 0; b < len; ++b)
					pField->m_value.m_rgBytes[b] = static_cast<char>(message[nDataPos+b]);
			

		}
        if (unField == BathBoostRemaining || unField == TurboRemaining) {
            const auto seconds = pField->m_value.m_nValue;
            if (seconds > 86400U) {
                expireField(unField);
                field_seen_[unField] = false;
                field_stale_[unField] = true;
                continue;
            }
#ifdef USE_SENSOR
            auto *countdown = unField == BathBoostRemaining ? bath_boost_remaining_sensor_ : turbo_remaining_sensor_;
            if (countdown) countdown->publish_state(static_cast<float>(seconds));
#endif
        }
#ifdef USE_SENSOR
        if (unField == BroanField::TargetHumidityA && humidity_target_sensor_)
            humidity_target_sensor_->publish_state(pField->m_value.m_flValue);
        if (unField == BroanField::IntModeDuration && intermittent_on_time_sensor_)
            intermittent_on_time_sensor_->publish_state(pField->m_value.m_nValue / 60.0f);
        if (unField == BroanField::FaultCode && fault_code_sensor_)
            fault_code_sensor_->publish_state(static_cast<int32_t>(pField->m_value.m_nValue));
        if (unField == BroanField::WarningCode && warning_code_sensor_)
            warning_code_sensor_->publish_state(static_cast<int32_t>(pField->m_value.m_nValue));
        if (unField == BroanField::BaseMode && base_mode_code_sensor_)
            base_mode_code_sensor_->publish_state(pField->m_value.m_nValue);
        if (unField == BroanField::ActiveMode && active_mode_code_sensor_)
            active_mode_code_sensor_->publish_state(pField->m_value.m_nValue);
#endif

#ifdef USE_BINARY_SENSOR
        if (unField == BroanField::ActiveMode && operating_binary_sensor_) {
            const auto mode = pField->m_value.m_nValue;
            if (mode <= 5) operating_binary_sensor_->publish_state(mode != 0);
            else operating_binary_sensor_->invalidate_state();
        }
        if (unField == BroanField::FaultCode && fault_data_available_binary_sensor_)
            fault_data_available_binary_sensor_->publish_state(true);
        if (unField == BroanField::HumidityControl && humidity_control_enabled_binary_sensor_)
            humidity_control_enabled_binary_sensor_->publish_state(pField->m_value.m_chValue != 0);
        if (unField == BroanField::FaultCode && fault_active_binary_sensor_)
            fault_active_binary_sensor_->publish_state(pField->m_value.m_nValue != 0xFFFFFFFF);
#endif

		switch(unField)
		{
#ifdef USE_SELECT
			case BroanField::FanMode:
			{
				if( !fan_mode_select_ )
					continue;

				std::string strMode;
				switch( pField->m_value.m_chValue )
				{
					case BroanFanMode::Ovr: strMode = "ovr"; break;
					case BroanFanMode::Intermittent: strMode = "int"; break;
					case BroanFanMode::Min: strMode = "min"; break;
					case BroanFanMode::Max: strMode = "max"; break;
					case BroanFanMode::Manual: strMode = "manual"; break;
					case BroanFanMode::Turbo: strMode = "turbo"; break;
					case BroanFanMode::Humidity: strMode = "humidity"; break;
					case BroanFanMode::Recirculate: strMode = "recirculate"; break;
					case BroanFanMode::Smart: strMode = "smart"; break;

					default: strMode = "off"; break;
				}

				fan_mode_select_->publish_state( strMode );
			}
			break;
#endif
#ifdef USE_SENSOR
			case BroanField::Wattage:
				if( !power_sensor_ )
					continue;

				power_sensor_->publish_state(pField->m_value.m_flValue);
			break;

			case BroanField::FilterLife:
				if( !filter_life_sensor_ )
					continue;

				// Seconds -> Days
				filter_life_sensor_->publish_state(pField->m_value.m_nValue / 86400.0f );
			break;

			case BroanField::TemperatureIn:
				if( !temperature_sensor_ )
					continue;

				temperature_sensor_->publish_state(pField->m_value.m_flValue);
			break;

			case BroanField::SupplyCFM:
				if( !supply_cfm_sensor_ )
					continue;

				supply_cfm_sensor_->publish_state(pField->m_value.m_flValue);
			break;

			case BroanField::ExhaustCFM:
				if( !exhaust_cfm_sensor_ )
					continue;

				exhaust_cfm_sensor_->publish_state(pField->m_value.m_flValue);
			break;

			case BroanField::SupplyRPM:
				if( !supply_rpm_sensor_ )
					continue;

				supply_rpm_sensor_->publish_state(pField->m_value.m_flValue);
			break;

			case BroanField::ExhaustRPM:
				if( !exhaust_rpm_sensor_ )
					continue;

				exhaust_rpm_sensor_->publish_state(pField->m_value.m_flValue);
			break;
				
			case BroanField::TemperatureOut:
			{
				// @todo: We should stop querying NaN fields...
				if( !temperature_out_sensor_ )
					continue;

				temperature_out_sensor_->publish_state(pField->m_value.m_flValue);		
			}
			break;

#endif	
#ifdef USE_NUMBER
			case BroanField::TargetHumidityA:
				if( !humidity_setpoint_number_ )
					continue;
				humidity_setpoint_number_->publish_state(pField->m_value.m_flValue);
			break;

			// @todo: We don't support unbalanced values here currently....
			case BroanField::CFMIn_Medium:
			{
				if( !fan_speed_number_ )
					continue;

				float flMin = m_vecFields[CFMIn_Min].m_value.m_flValue;
				float flMax = m_vecFields[CFMIn_Max].m_value.m_flValue;
				float flAdjusted = remap( pField->m_value.m_flValue, flMin, flMax, 0.f, 100.f );
				fan_speed_number_->publish_state(flAdjusted);
			}
			break;

			case BroanField::IntModeDuration:
				if( !intermittent_period_number_ )
					continue;

				intermittent_period_number_->publish_state(pField->m_value.m_nValue /* / 1000 */ );
			break;
#endif
#ifdef USE_SWITCH
			case BroanField::HumidityControl:
				if( !humidity_control_switch_ )
						continue;
				
				humidity_control_switch_->publish_state(pField->m_value.m_chValue==1);
			break;
#endif
#ifdef USE_TEXT_SENSOR
			case BroanField::Firmware: 
				if( !firmware_text_sensor_ )
						continue;
				
				firmware_text_sensor_->publish_state(strBuf);
			break;
			case BroanField::FirmwareVersion: 
			{
				if( !firmware_version_text_sensor_ )
						continue;

				char buf[12];
				snprintf( buf, sizeof(buf), "%u.%u.%u", static_cast<uint8_t>(strBuf[0]), static_cast<uint8_t>(strBuf[1]), static_cast<uint8_t>(strBuf[2]) );
				
				firmware_version_text_sensor_->publish_state(buf);
			}
			break;
			case BroanField::Model: 
				if( !model_text_sensor_ )
						continue;
				
				model_text_sensor_->publish_state(strBuf);
			break;
			case BroanField::HardwareRevision: 
			{
				if( !hardware_revision_text_sensor_ )
						continue;
				
				char buf[12];
				snprintf( buf, sizeof(buf), "%u.%u.%u", static_cast<uint8_t>(strBuf[0]), static_cast<uint8_t>(strBuf[1]), static_cast<uint8_t>(strBuf[2]) );

				hardware_revision_text_sensor_->publish_state(buf);
			}
			break;

			case BroanField::WarningCode: 
			{
				if( !warning_code_text_sensor_ )
						continue;

				if( pField->m_value.m_nValue == -1 )
				{
					warning_code_text_sensor_->publish_state("OK");
					break;
				}
				
				char buf[32];
				snprintf( buf, sizeof(buf), "W%" PRIu32, pField->m_value.m_nValue );

				warning_code_text_sensor_->publish_state(buf);
			}
			break;
            case BroanField::FaultCode:
            {
                const uint32_t code = pField->m_value.m_nValue;
                const bool active = code != 0xFFFFFFFF;
                char buf[32];
                snprintf(buf, sizeof(buf), "E%u", static_cast<unsigned>(code));
                if (fault_code_text_sensor_) fault_code_text_sensor_->publish_state(active ? buf : "OK");
                if (active && code != last_fault_value_ && last_fault_text_sensor_) {
                    char retained[80];
                    snprintf(retained, sizeof(retained), "%s at boot + %us", buf, static_cast<unsigned>(millis() / 1000));
                    last_fault_text_sensor_->publish_state(retained);
                }
                last_fault_value_ = code;
            }
            break;

			case BroanField::BaseMode: 
				if( !base_mode_text_sensor_ )
						continue;

                base_mode_text_sensor_->publish_state(baseModeToString(pField->m_value.m_nValue));
			break;


			case BroanField::ActiveMode: 
				if( !active_mode_text_sensor_ )
						continue;
						
				active_mode_text_sensor_->publish_state(activeModeToString( pField->m_value.m_nValue ));
			break;
#endif
		}

        if (!changed) continue;
		switch( pField->m_nType )
		{
			case BroanFieldType::Byte:
				ESP_LOGD("broan","%02X%02X is now Byte %02X", nOpcodeHigh, nOpcodeLow, pField->m_value.m_chValue );
				break;
			case BroanFieldType::Int:
				ESP_LOGD("broan","%02X%02X is now Int %" PRIu32, nOpcodeHigh, nOpcodeLow, pField->m_value.m_nValue );
				break;
			case BroanFieldType::Float:
				ESP_LOGD("broan","%02X%02X is now Float %f", nOpcodeHigh, nOpcodeLow, pField->m_value.m_flValue );
				break;
			case BroanFieldType::Void:
				ESP_LOGD("broan","%02X%02X is not set", nOpcodeHigh, nOpcodeLow );
				break;
			case BroanFieldType::String:
				ESP_LOGD("broan","%02X%02X is now %s", nOpcodeHigh, nOpcodeLow, strBuf.c_str() );
				break;
		}
    }
}

void BroanComponent::handleUnknownField(uint32_t nOpcodeHigh, uint32_t nOpcodeLow, uint8_t len, uint32_t i, const std::vector<uint8_t>& message )
{
    ESP_LOGD("broan", "%02" PRIX32 "%02" PRIX32 " unmapped len=%u raw=%s",
             nOpcodeHigh, nOpcodeLow, static_cast<unsigned>(len),
             format_hex_pretty(message.data() + i, len).c_str());
}

void BroanComponent::send(const std::vector<uint8_t>& vecMessage)
{
	if (passive_mode_ || experimental_query_) return;

#ifndef LISTEN_ONLY
 	if(flow_control_pin_)
    	flow_control_pin_->digital_write(true);

	uint8_t header = 0x01;
	uint8_t alignment = 0x01;
	uint8_t footer = 0x04;
	write(header);
	write(m_nServerAddress);
	write(m_nClientAddress);
	write(alignment);
	write((uint8_t)vecMessage.size());
	for (auto b : vecMessage) write(b);
	write(calculateChecksum(m_nClientAddress, m_nServerAddress, vecMessage));
	write(footer);

	flush();

 	if(flow_control_pin_)
    	flow_control_pin_->digital_write(false);
#endif
}

uint8_t BroanComponent::calculateChecksum(uint8_t sender, uint8_t receiver, const std::vector<uint8_t>& message)
{
	return packet_checksum(sender, receiver, message);
}

BroanField_t* BroanComponent::lookupField( uint8_t opcodeHigh, uint8_t opcodeLow )
{
	uint32_t unField = lookupFieldIndex( opcodeHigh, opcodeLow );
	if( unField != INVALID_FIELD )
	{
		return &m_vecFields[unField];
	}

	return nullptr;
}

uint32_t BroanComponent::lookupFieldIndex( uint8_t opcodeHigh, uint8_t opcodeLow )
{
	for( int i=0; i<BroanField::MAX_FIELDS; i++ )
	{
		BroanField_t *pField = &m_vecFields[i];
		if( pField->m_nOpcodeHigh == opcodeHigh && pField->m_nOpcodeLow == opcodeLow )
			return i;
	}

	return INVALID_FIELD;
}

void BroanComponent::runTasks()
{
	if (passive_mode_ || experimental_query_) return;

	uint32_t time = millis();

	if( m_bERVReady )
	{
		//ESP_LOGD("broan", "Reading values" );

		std::vector<unsigned char> vecRequest;

		int nCount = 0;
		for( int i=0; i<BroanField::MAX_FIELDS && nCount < MAX_REQUEST_SIZE; i++ )
		{	

			if( m_vecFields[i].m_unPollRate != UPDATE_RATE_ONCE && ( m_vecFields[i].m_unPollRate == UPDATE_RATE_NEVER || time - m_vecFields[i].m_unLastUpdate < m_vecFields[i].m_unPollRate ) )
				continue;

			nCount++;
			m_vecFields[i].m_unLastUpdate = time;

			if( vecRequest.size() == 0 )
				vecRequest.push_back(0x20);

			vecRequest.push_back( m_vecFields[i].m_nOpcodeHigh );
			vecRequest.push_back( m_vecFields[i].m_nOpcodeLow );

			if( m_vecFields[i].m_unPollRate == UPDATE_RATE_ONCE )
				m_vecFields[i].m_unPollRate = UPDATE_RATE_NEVER;
		}

		if( vecRequest.size() > 0 )
		{
			queueMessage(vecRequest);
		}
	}


	if( time - m_unLastHeartbeat > HEARTBEAT_RATE )
	{
		m_unLastHeartbeat = time;
		std::vector<unsigned char> vecRequest;
		vecRequest.push_back(0x40);
		vecRequest.push_back(0x00);
		vecRequest.push_back(0x50);
		vecRequest.push_back(0x00);

		queueMessage(vecRequest);
	}

#ifdef SCAN_UNKNOWN

	if( m_nNextScan == 0 )
		m_nNextScan	= time + 15000;

	if( m_bERVReady && time > m_nNextScan )
	{
		m_nNextScan = time + 100;

		std::vector<unsigned char> vecRequest;
		vecRequest.push_back(0x20);

		for( int i=0; i<15;i++)
		{
			vecRequest.push_back(m_nFieldCursor);
			vecRequest.push_back(m_nGroupCursor);


			if( m_nFieldCursor == 0xFF)
			{

				switch( m_nGroupCursor )
				{
					case 0x20: m_nGroupCursor = 0x21; break;
					case 0x21: m_nGroupCursor = 0x22; break;
					case 0x22: m_nGroupCursor = 0x30; break;
					case 0x30: m_nGroupCursor = 0x40; break;
					case 0x40: m_nGroupCursor = 0x50; break;
					case 0x50: m_nGroupCursor = 0x60; break;
					case 0x60: m_nGroupCursor = 0xE0; break;
					case 0xE0: m_nGroupCursor = 0x20; break;
					//case 0xF0: m_nGroupCursor = 0x20; break;
				}
				//ESP_LOGD("broan","Brute force: Group is now %02X ", m_nGroupCursor );
			}
			m_nFieldCursor++;
		}

		queueMessage(vecRequest);

	}
#endif
}

std::string BroanComponent::baseModeToString(int code) {
    switch (code) {
        case 1: return "Standby";
        case 8: return "Intermittent";
        case 9: return "Continuous Low";
        case 10: return "Continuous High";
        case 11: return "Continuous Medium";
        case 17: return "Smart";
        default: return std::to_string(code);
    }
}

std::string BroanComponent::activeModeToString( int code )
{
	// @todo: Figure these out
	
	// OFF returns code 0 / 1 (active / base)
	// MIN returns code 1 / 9
	// MED returns code 4 / 11
	// MAX returns code 2 / 10
	// INT returns code 0 / 8 (while idle)
	// INT returns code 1 / 8 (while running)
	// INT returns code 1 / 9 (while running)
	// TURBO returns    3 / 8 

	// Can make assumptions around active code, not quite about base mode.
	// Probably need more data for recirc/etc to know their running modes.
	
	switch( code )
	{
		case 0: return "Idle";
		case 1: return "Low";
		case 2: return "High";
		case 3: return "Turbo";
		case 4: return "Medium";
		case 5: return "Bath Override";
	}
	

	char buf[30];
	snprintf( buf, sizeof(buf), "%u", (unsigned)code );
	return buf;
}

}
}
