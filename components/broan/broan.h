#pragma once

#include "esphome.h"
#include <deque>
#include "protocol.h"
#include "esphome/core/component.h"

#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif

#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif

#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif

#include "esphome/components/uart/uart.h"


namespace esphome {
namespace broan {

#define CONTROL_TIMEOUT 5000
#define UPDATE_RATE 1000
#define HEARTBEAT_RATE 10000

#define UPDATE_RATE_FAST 10000 // 10 seconds
#define UPDATE_RATE_SLOW 60000 // 1 minute
#define UPDATE_RATE_ONCE 0xFFFFFFFE
#define UPDATE_RATE_NEVER 0xFFFFFFFF

#define MAX_REQUEST_SIZE 10
#define INVALID_FIELD 0xFFFFFF

#define FILTER_LIFE_MAX 7884000

//#define SCAN_UNKNOWN 1
//#define LISTEN_ONLY 1

template<typename T>
concept BroanFieldTypes = 	std::is_same_v<T, float> ||
							std::is_same_v<T, uint8_t> ||
							std::is_same_v<T, uint32_t>;

enum BroanFieldType
{
	Float,
	Int,
	Byte,
	Void,
	String,
};

enum BroanCFMMode
{
	Input = 1 >> 0,
	Output = 1 >> 1,
	Both = BroanCFMMode::Input | BroanCFMMode::Output,
};

enum BroanFanMode
{
	Off = 0x01,
	Ovr = 0x02,
	Intermittent = 0x08,
	Recirculate = 0x06,
	Min = 0x09,
	Max = 0x0a,
	Smart = 0x11,
	Manual = 0x0b,
	Turbo = 0x0c,
	Humidity = 0x0d,
	Away = 0x0F, // "OTH", no idea what this actually does?
};

enum BroanField
{
	// Control
	FanMode = 0,
	HumidityControl,
	IntModeDuration,
	TargetHumidityA, // Set both to same value per VTSPEEDW
	TargetHumidityB,

	// Info
	Uptime, // In seconds?
	Wattage,
	TemperatureIn,
	TemperatureOut,
	SupplyCFM,
	ExhaustCFM,
	SupplyRPM,
	ExhaustRPM,

	// Speeds
	CFMIn_Medium,
	CFMOut_Medium,
	CFMIn_Max,
	CFMOut_Max,
	CFMIn_Min,
	CFMOut_Min,

	// Input
	Heartbeat, // Weird void value that controllers ping every 10s
	ControllerHumidity,
	ControllerTemperature,

	// Maintenance
	FilterReset, // Set to 1 to reset
	FilterLife, // default 7884000 / 3 months
	FilterLifeStage, // 09:30. Must be preloaded with the desired FilterLife value before FilterReset is set, otherwise the ERV ignores the reset.

	// Diagnostic
	WarningCode,
	FaultCode,

	// State
	BaseMode,
	ActiveMode,

	// Info 
	Firmware,
	Model,
	FirmwareVersion,
	HardwareRevision,

	MAX_FIELDS,
};

struct BroanField_t
{
	uint8_t m_nOpcodeHigh;
	uint8_t m_nOpcodeLow;

	uint8_t m_nType;

	union {
		char m_rgBytes[4];
		float m_flValue;
		uint32_t m_nValue;
		uint8_t m_chValue;
	} m_value;

	uint32_t m_unPollRate = UPDATE_RATE_SLOW;
	uint32_t m_unLastUpdate = 0;

	// Totally safe blind copy of the incoming value.
	BroanField_t copyForUpdate(BroanFieldTypes auto const &newVal) const
	{
		BroanField_t copy = *this;

		size_t len = (m_nType == static_cast<uint8_t>(BroanFieldType::Byte)) ? 1 : 4;
		std::memcpy(copy.m_value.m_rgBytes, &newVal, len);

		return copy;
	}

	void markDirty()
	{
		m_unLastUpdate = millis() - m_unPollRate;
	}

};

class BroanComponent : public Component, public uart::UARTDevice
{

#ifdef USE_SENSOR
	SUB_SENSOR(power)
	SUB_SENSOR(temperature)
	SUB_SENSOR(temperature_out)
	SUB_SENSOR(filter_life)
	SUB_SENSOR(supply_cfm)
	SUB_SENSOR(exhaust_cfm)
	SUB_SENSOR(supply_rpm)
	SUB_SENSOR(exhaust_rpm)
	SUB_SENSOR(fault_code)
	SUB_SENSOR(warning_code)
	SUB_SENSOR(base_mode_code)
	SUB_SENSOR(active_mode_code)
	SUB_SENSOR(humidity_target)
	SUB_SENSOR(intermittent_on_time)
#endif

#ifdef USE_SELECT
	SUB_SELECT(fan_mode)
#endif

#ifdef USE_NUMBER
	SUB_NUMBER(fan_speed)
	SUB_NUMBER(humidity_setpoint)
	SUB_NUMBER(intermittent_period)
#endif

#ifdef USE_BUTTON
  SUB_BUTTON(filter_reset)
#endif

#ifdef USE_SWITCH
  SUB_SWITCH(humidity_control)
#endif

#ifdef USE_TEXT_SENSOR
	SUB_TEXT_SENSOR(model)
	SUB_TEXT_SENSOR(firmware)
	SUB_TEXT_SENSOR(firmware_version)
	SUB_TEXT_SENSOR(hardware_revision)
	SUB_TEXT_SENSOR(fault_code)
	SUB_TEXT_SENSOR(warning_code)
	SUB_TEXT_SENSOR(active_mode)
	SUB_TEXT_SENSOR(base_mode)
	SUB_TEXT_SENSOR(last_fault)
	SUB_TEXT_SENSOR(query_status)
#endif

#ifdef USE_BINARY_SENSOR
	SUB_BINARY_SENSOR(fault_active)
	SUB_BINARY_SENSOR(communication_ok)
	SUB_BINARY_SENSOR(fault_data_available)
	SUB_BINARY_SENSOR(humidity_control_enabled)
#endif

public:
	const uint8_t m_nServerAddress = 0x10;
	const uint8_t m_nClientAddress = 0x12;

	bool m_bWaitForRemote = false;

	BroanField_t m_vecFields[BroanField::MAX_FIELDS] = {
		// Known fields
		// Control
		{ 0x00, 0x20, BroanFieldType::Byte, {0}, UPDATE_RATE_FAST }, // FanMode
		{ 0x0F, 0x22, BroanFieldType::Byte, {0}, UPDATE_RATE_SLOW }, // Humidity control on/off
		{ 0x02, 0x22, BroanFieldType::Int, {0}, UPDATE_RATE_SLOW }, // INT mode on time (seconds, OFF time will be what remains of an hour)
		{ 0x0C, 0x22, BroanFieldType::Float, {0}, UPDATE_RATE_SLOW }, // Target humidity?
		{ 0x0A, 0x22, BroanFieldType::Float, {0}, UPDATE_RATE_SLOW }, // Target humidity? (These are set together)


		// Info
		{ 0x14, 0x00, BroanFieldType::Int, {0}, UPDATE_RATE_SLOW }, // Uptime (Seconds)
		{ 0x23, 0x50, BroanFieldType::Float, {0}, UPDATE_RATE_FAST }, // Power draw (Watts)
		{ 0x01, 0xE0, BroanFieldType::Float, {0}, UPDATE_RATE_FAST }, // Temperature sensor (In)
		{ 0x03, 0xE0, BroanFieldType::Float, {0}, UPDATE_RATE_FAST }, // Temperature sensor (Out)
		{ 0x05, 0x10, BroanFieldType::Float, {0}, UPDATE_RATE_FAST }, // Intake CFM
		{ 0x06, 0x10, BroanFieldType::Float, {0}, UPDATE_RATE_FAST }, // Exhaust CFM
		{ 0x03, 0x10, BroanFieldType::Float, {0}, UPDATE_RATE_FAST }, // Intake RPM
		{ 0x04, 0x10, BroanFieldType::Float, {0}, UPDATE_RATE_FAST }, // Exhaust RPM

		// Speeds
		{ 0x06, 0x22, BroanFieldType::Float, {0}, UPDATE_RATE_FAST }, // MED target CFM in.
		{ 0x08, 0x22, BroanFieldType::Float, {0}, UPDATE_RATE_FAST }, // MED target CFM out.
		{ 0x0E, 0x50, BroanFieldType::Float, {0}, UPDATE_RATE_SLOW }, // MAX target CFM in.
		{ 0x0F, 0x50, BroanFieldType::Float, {0}, UPDATE_RATE_SLOW }, // MAX target CFM out.
		{ 0x0A, 0x50, BroanFieldType::Float, {0}, UPDATE_RATE_SLOW }, // MIN target CFM in.
		{ 0x0B, 0x50, BroanFieldType::Float, {0}, UPDATE_RATE_SLOW }, // MIN target CFM out.

		//Input
		{ 0x00, 0x50, BroanFieldType::Void, {0}, UPDATE_RATE_NEVER }, // Unknown. Controllers regularly write this. Some kind of heartbeat maybe?
		{ 0x04, 0x50, BroanFieldType::Float, {0}, UPDATE_RATE_NEVER }, // Controller Humidity (Write only)
		{ 0x05, 0x50, BroanFieldType::Float, {0}, UPDATE_RATE_NEVER }, // Controller temperature (Write only)

		// Maintenance
		{ 0x01, 0x30, BroanFieldType::Byte, {0}, UPDATE_RATE_SLOW }, // Set to 0x01 to reset filter
		{ 0x08, 0x30, BroanFieldType::Int, {0}, UPDATE_RATE_SLOW }, // Number of seconds until filter needs reset. Set along side reset byte
		{ 0x09, 0x30, BroanFieldType::Int, {0}, UPDATE_RATE_NEVER }, // FilterLifeStage. Wall controller writes the desired FilterLife here first, before FilterReset.

		// Diagnostic
		{ 0x1A, 0x00, BroanFieldType::Int, {0}, UPDATE_RATE_FAST }, // Warning code -1 = OK, if multiple warnings, will cycle through on each read.
		{ 0x17, 0x00, BroanFieldType::Int, {0}, UPDATE_RATE_FAST }, // Fault code, -1 = OK
		{ 0x02, 0x20, BroanFieldType::Int, {0}, UPDATE_RATE_FAST }, // Base mode.
		{ 0x07, 0x20, BroanFieldType::Int, {0}, UPDATE_RATE_FAST }, // Active mode.

		// Info
		{ 0x02, 0x00, BroanFieldType::String,  {0}, UPDATE_RATE_ONCE }, // Firmware
		{ 0x02, 0x60, BroanFieldType::String,  {0}, UPDATE_RATE_ONCE }, // Model
		{ 0x01, 0x00, BroanFieldType::String,  {0}, UPDATE_RATE_ONCE }, // Firmware Version
		{ 0x01, 0x60, BroanFieldType::String,  {0}, UPDATE_RATE_ONCE }, // Hardware Revision
/*
		// Unknown fields scanned by the VTSPEEDW
		{ 0x02, 0x30, BroanFieldType::Byte, {0}, UPDATE_RATE_SLOW }, // Unknown. 1. Set to 0 in TURBO mode
		{ 0x0A, 0x22, BroanFieldType::Float, {0} }, // Unknown. 40 / 00002042
		{ 0x0E, 0x21, BroanFieldType::Byte, {0} }, // Unknown. 1 / 01
		{ 0x0C, 0x21, BroanFieldType::Byte, {0} }, // Unknown. 1 / 01
		{ 0x0B, 0x21, BroanFieldType::Byte, {0} }, // Unknown. 1 / 01
		{ 0x0A, 0x21, BroanFieldType::Byte, {0} }, // Unknown. 1 / 01
		{ 0x09, 0x21, BroanFieldType::Byte, {0} }, // Unknown. 1 / 01
		{ 0x08, 0x21, BroanFieldType::Byte, {0} }, // Unknown. 1 / 01
		{ 0x07, 0x21, BroanFieldType::Byte, {0} }, // Unknown. 1 / 01
		{ 0x06, 0x21, BroanFieldType::Byte, {0} }, // Unknown. 0 / 00
		{ 0x05, 0x21, BroanFieldType::Byte, {0} }, // Unknown. 0 / 00
		{ 0x04, 0x21, BroanFieldType::Byte, {0} }, // Unknown. 0 / 00
		{ 0x00, 0x30, BroanFieldType::Byte, {0} }, // Unknown. 0 / 00
		{ 0x00, 0x22, BroanFieldType::Int, {0} }, // Unknown. 14400 / 40380000
		{ 0x07, 0x50, BroanFieldType::Int, {0} }, // Unknown. VTSPEEDW often sets this to -1
		{ 0x03, 0x20, BroanFieldType::Byte, {0} }, // Unknown. Set to 0 when entering INT mode
		{ 0x08, 0x20, BroanFieldType::Byte, {0} }, // Unknown. Set to 0 when entering SMART mode, set to 1 in continuous modes.

		// Airstream humidity? Needs verification. Broan wiring and parts diagrams for b150e75nt do not list humidity sensors, only the single j7a thermistor. 
		// May be specific to certain models, this model DOES report changing values on these registers, so I'm suspicious.
		{ 0x08, 0xE0, BroanFieldType::Float, {0}, UPDATE_RATE_NEVER }, // Unknown. Seems to change a lot. 38.943115 / 1109116352 (Does not correlate with fan speed)
		{ 0x09, 0xE0, BroanFieldType::Float, {0}, UPDATE_RATE_NEVER }, // Unknown. Seems to change a lot. 36.360962 / 1108439456 (Same as above)

*/
	};

	// uart overrides
	void setup() override;
	void loop() override;
	void dump_config() override;
	float get_setup_priority() const override;

public:
	// Setup
	void set_flow_control_pin(GPIOPin *flow_control_pin) { this->flow_control_pin_ = flow_control_pin; }
	void set_passive_mode(bool value) { passive_mode_ = value; }
	void set_experimental_query(bool value) { experimental_query_ = value; }
	void set_query_source_address(uint8_t value) { query_source_address_ = value; }
	void set_query_allow_shared_address(bool value) { query_allow_shared_address_ = value; }
	void set_query_tolerant_mode(bool value) { query_tolerant_mode_ = value; }
	bool request_probe(uint16_t opcode, uint32_t idle_ms = 5);
	bool request_probe_with_release(uint16_t opcode, uint32_t idle_ms = 5);
	bool start_probe_polling();
	void stop_probe_polling();
	bool get_probe_polling() const { return query_polling_; }
	void set_communication_timeout(uint32_t value) { communication_timeout_ = value; }
	void set_sensor_timeout(uint32_t value) { sensor_timeout_ = value; }
	void set_log_packets(bool value) { log_packets_ = value; }
	void set_log_unknown_registers(bool value) { log_unknown_registers_ = value; }
	void set_register_discovery(bool value) { register_discovery_ = value; }
	uint32_t get_packets_received() const { return framer_.packets; }
	uint32_t get_valid_packets() const { return framer_.valid; }
	uint32_t get_invalid_packets() const { return framer_.invalid; }
	uint32_t get_checksum_failures() const { return framer_.checksum_failures; }
	uint32_t get_unknown_registers() const { return unknown_registers_; }
	float get_last_valid_packet_age() const { return have_valid_packet_ ? (millis() - last_valid_packet_) / 1000.0f : NAN; }

	// Control API
	void setFanMode( std::string mode );
	void setFanSpeed( float speed );
	void setFanSpeedCFM( BroanFanMode mode, BroanCFMMode direction, float flTargetCFM );
	void resetFilter();
	void setHumidityControl( bool enable );
	void setHumiditySetpoint( float humidity );
	void setCurrentHumidity( float humidity );
	void setIntermittentPeriod( uint32_t period );

private:
	enum class QueryState { IDLE, QUEUED, WAITING, OBSERVING, LOCKED };
	bool experimental_query_{false}, query_window_{false}, touch_yielded_{false}, query_sent_{false};
	bool query_touch_request_{false}, query_recovered_{false};
	bool query_allow_shared_address_{false}, touch_transaction_pending_{false};
	bool query_tolerant_mode_{false}, query_reply_received_{false}, query_reply_late_{false};
	uint32_t query_attempts_{0}, query_failures_{0}, query_resyncs_{0}, query_rtt_{0};
	uint32_t query_checksum_baseline_{0}, query_reported_errors_{0}, query_last_touch_reply_{0};
	uint32_t query_idle_ms_{5};
	uint32_t query_touch_grant_ms_{UINT32_MAX}, query_touch_request_ms_{UINT32_MAX}, query_touch_reply_ms_{UINT32_MAX};
	bool query_release_enabled_{false}, query_release_pending_{false}, query_release_sent_{false}, query_release_ack_{false};
	uint32_t query_release_at_{0}, query_release_ack_ms_{0};
	bool query_polling_{false}, query_automatic_{false}, have_touch_read_reply_{false};
	uint8_t query_poll_index_{0};
	uint32_t touch_last_read_reply_{0}, query_poll_invalid_baseline_{0};
	uint8_t touch_reply_type_{0};
	std::vector<uint16_t> touch_request_fields_;
	uint8_t query_source_address_{0x13};
	uint16_t query_opcode_{0};
	QueryState query_state_{QueryState::IDLE};
	HighFrequencyLoopRequester query_loop_;
	uint32_t query_queued_at_{0}, query_sent_at_{0}, query_window_at_{0}, query_observe_at_{0};
	uint32_t query_invalid_baseline_{0}, last_rx_byte_{0};
	void updateProbe();
	void updateProbeRelease();
	void observeProbe(uint8_t sender, uint8_t target, const std::vector<uint8_t> &payload);
	bool observeTouchTransaction(uint8_t sender, uint8_t target, const std::vector<uint8_t> &payload);
	void setProbeStatus(const char *status);
	void lockProbe(const char *reason);
	bool checkProbeErrors();
	void handleProbeTimeout();
	void logProbeResult(const char *result);
	uint32_t probeReplyTimeout() const { return query_tolerant_mode_ ? 1500 : 500; }
	bool passive_mode_{false}, log_packets_{false}, log_unknown_registers_{false}, register_discovery_{false};
	uint32_t communication_timeout_{120000}, sensor_timeout_{300000};
	PacketFramer framer_;
	uint32_t last_valid_packet_{0}, unknown_registers_{0}, last_diagnostics_{0};
	bool have_valid_packet_{false};
	uint32_t received_at_[BroanField::MAX_FIELDS]{};
	bool field_seen_[BroanField::MAX_FIELDS]{}, field_stale_[BroanField::MAX_FIELDS]{};
	uint32_t last_fault_value_{0xFFFFFFFF};
	struct RegisterObservation {
		uint32_t requests{0}, responses{0}, last_response{0}, max_gap{0};
		std::vector<uint8_t> value;
	};
	std::map<uint16_t, RegisterObservation> observations_;
	void updateFreshness();
	void expireField(uint32_t field);
	void observeRegister(uint16_t opcode, bool request);
	bool observeResponse(uint16_t opcode, const uint8_t *data, size_t length);

	uint32_t m_nLastHadControl = 0;
	uint32_t m_unLastHeartbeat = 0; // Next time to send heartbeat

	bool m_bERVReady = false;

#ifdef SCAN_UNKNOWN
	// Field scanner
	uint32_t m_nNextScan = 0;
	uint8_t m_nFieldCursor = 0;
	uint8_t m_nGroupCursor = 0x20;
	std::map<uint16_t, BroanField_t> m_vecFieldData;
#endif

	std::map<uint16_t, std::string> m_mapStrings;

	bool m_bHaveControl = false;
	bool m_bExpectingReply = false;
	bool m_bHaveSentMessage = false;

	std::deque<std::vector<uint8_t>> m_vecSendQueue;


private:
	// Internal
	void handleMessage(uint8_t sender, uint8_t target, const std::vector<uint8_t>& message);
	void send(const std::vector<uint8_t>& msg);
	uint8_t calculateChecksum(uint8_t sender, uint8_t receiver, const std::vector<uint8_t>& message);
	void replyIfAllowed();
	void runTasks();
	void parseBroanFields(const std::vector<uint8_t>& message);
	void writeRegisters( const std::vector<BroanField_t> &values );

	float remap(float flIn, float flInMin, float flInMax, float flOutMin, float flOutMax) {
  		return (flIn - flInMin) * (flOutMax - flOutMin) / (flInMax - flInMin) + flOutMin;
	}

	BroanField_t* lookupField( uint8_t opcodeHigh, uint8_t opcodeLow );
	uint32_t lookupFieldIndex( uint8_t opcodeHigh, uint8_t opcodeLow );
	void handleUnknownField(uint32_t nOpcodeHigh, uint32_t nOpcodeLow, uint8_t len, uint32_t i, const std::vector<uint8_t>& message );

	void queueMessage(std::vector<uint8_t>& message);
	std::string activeModeToString( int code );
	std::string baseModeToString(int code);


protected:
	// esphome glue
	std::string fan_mode_{};
	
	float fan_speed_{0.f};
	float power_{0.f};
	float temperature_{0.f};
	float temperature_out_{0.f};
	float supply_cfm_{0.f};
	float exhaust_cfm_{0.f};
	float supply_rpm_{0.f};
	float exhaust_rpm_{0.f};

	uint32_t filter_life_{0};

	GPIOPin *flow_control_pin_{nullptr};
};

}  // namespace broan
}  // namespace esphome
