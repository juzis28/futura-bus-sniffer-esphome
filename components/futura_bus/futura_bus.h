#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <vector>
#include <map>
#include <string>
#include <set>

namespace esphome {
namespace futura_bus {

// ─────────────────────────────────────────────────────────────────────────────
// Known register annotation
// ─────────────────────────────────────────────────────────────────────────────
struct KnownReg {
  const char *name;
  float       scale;
  const char *unit;
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-register snapshot
// Map key: (uint32_t)(slave_id << 16) | reg_addr
// ─────────────────────────────────────────────────────────────────────────────
struct RegSnapshot {
  uint16_t last_value{0};
  uint16_t min_value{0xFFFF};
  uint16_t max_value{0};
  uint32_t last_seen_ms{0};
  uint32_t seen_count{0};
  bool     ever_changed{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Damper (VarioBreeze sklendė)
// ─────────────────────────────────────────────────────────────────────────────
struct DamperConfig {
  uint8_t     slave_id;
  std::string room;
  uint8_t     zone;
  std::string type;        // "privod" | "odtah"
  uint8_t     damper_index;
};

struct DamperState {
  DamperConfig config;
  int      target_position{-1};   // reg 102, written by Futura
  int      status_code{-1};       // reg 107, polled by Futura
  uint32_t last_target_ms{0};
  uint32_t last_status_ms{0};
  uint32_t last_seen_ms{0};

  sensor::Sensor *position_sensor{nullptr};
  sensor::Sensor *status_sensor{nullptr};
};

// ─────────────────────────────────────────────────────────────────────────────
// Zone device — wall panels (slaves 9-13) and ALFA panel (slave 16)
//
// Wall panel reg91 status bitmask (confirmed from log):
//   bit 0 = basic sensor present   (status & 1) → 1 = active
//   bit 1 = boost button present   (status & 2) → 3 = has boost button
//   slaves 9,10 = status 1 (no boost button)
//   slaves 11,12,13 = status 3 (have boost button: WC, vonios, virtuvė)
// ─────────────────────────────────────────────────────────────────────────────
struct ZoneDeviceState {
  uint8_t     slave_id;
  std::string room;

  float    temp_room{-1.0f};   // °C   — reg93 × 0.01 or reg68 × 0.1
  float    temp_out{-1.0f};    // °C   — reg92 × 0.01 (outdoor, same all zones)
  float    humidity{-1.0f};    // %RH  — reg94 × 0.01 (CONFIRMED from log)
  float    co2{-1.0f};         // ppm  — reg95 or reg70
  int      status{-1};         // reg91: 1=plain, 3=has boost button
  bool     has_boost_button{false};
  uint32_t last_seen_ms{0};

  sensor::Sensor *temp_sensor{nullptr};
  sensor::Sensor *humidity_sensor{nullptr};
  sensor::Sensor *co2_sensor{nullptr};
};

// ─────────────────────────────────────────────────────────────────────────────
// Pressure sensor node
// Covers slaves 32-34 (peripheral pressure sensors) and
// ALFA16 regs 50-67 (per-zone duct pressures, Pa — all 0 when idle).
// ─────────────────────────────────────────────────────────────────────────────
struct PressureState {
  uint8_t     slave_id;
  uint16_t    reg_addr;   // which register carries this pressure
  std::string label;      // descriptive name for HA
  float       value_pa{-1.0f};
  uint32_t    last_seen_ms{0};

  sensor::Sensor *pressure_sensor{nullptr};
};

// ─────────────────────────────────────────────────────────────────────────────
// System-level aggregated data from ALFA16 (fan speeds, setpoints)
// ─────────────────────────────────────────────────────────────────────────────
struct SystemState {
  int   fan_airflow{-1};        // reg49: current airflow, unit steps × 64 m³/h
  int   exhaust_fan_pct{-1};    // reg198: exhaust fan % (written periodically)
  int   supply_fan_pct{-1};     // reg199: supply fan % (written periodically)
  int   nominal_airflow{-1};    // reg193: system design total m³/h (constant 230)
  int   active_zones{-1};       // reg194: currently active zone count (constant 6)
  int   mode_status{-1};        // reg181: operating mode (1=normal)
  float alfa_co2{-1.0f};        // reg37: ALFA panel's own CO₂ sensor
  uint32_t last_boost_ms{0};    // timestamp of last boost command (reg10+11 write)
  int   last_r256{-1};          // reg256: last written ramp/countdown value (FC16)
  int   last_r14{-1};           // reg14: last pacing signal value

  sensor::Sensor *fan_airflow_sensor{nullptr};
  sensor::Sensor *exhaust_fan_pct_sensor{nullptr};
  sensor::Sensor *supply_fan_pct_sensor{nullptr};
};

// ─────────────────────────────────────────────────────────────────────────────
// Pending Modbus request
// Map key: (slave_id << 8) | fc
// ─────────────────────────────────────────────────────────────────────────────
struct PendingRequest {
  uint8_t  slave_id;
  uint8_t  function_code;
  uint16_t start_address;
  uint16_t register_count;
  uint32_t timestamp_ms;
};

// ─────────────────────────────────────────────────────────────────────────────
// FuturaBusComponent
// ─────────────────────────────────────────────────────────────────────────────
class FuturaBusComponent : public Component, public uart::UARTDevice {
 public:
  void setup()       override;
  void loop()        override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Feature flags
  void set_frame_gap_ms(float ms)         { frame_gap_ms_ = ms; }
  void set_discovery_mode(bool en)        { discovery_mode_ = en; }
  void set_summary_interval_s(uint32_t s) { summary_interval_ms_ = s * 1000UL; }

  // Damper registration
  void add_damper_config(uint8_t slave_id, const std::string &room,
                         uint8_t zone, const std::string &type,
                         uint8_t damper_index);
  void register_position_sensor(uint8_t slave_id, sensor::Sensor *s);
  void register_status_sensor(uint8_t slave_id, sensor::Sensor *s);

  // Zone device registration (wall panels + ALFA)
  void add_zone_device(uint8_t slave_id, const std::string &room);
  void register_zone_temp(uint8_t slave_id, sensor::Sensor *s);
  void register_zone_humidity(uint8_t slave_id, sensor::Sensor *s);
  void register_zone_co2(uint8_t slave_id, sensor::Sensor *s);

  // System sensors (fan airflow, setpoints)
  void register_fan_airflow_sensor(sensor::Sensor *s)      { system_.fan_airflow_sensor = s; }
  void register_exhaust_fan_sensor(sensor::Sensor *s)      { system_.exhaust_fan_pct_sensor = s; }
  void register_supply_fan_sensor(sensor::Sensor *s)       { system_.supply_fan_pct_sensor = s; }

  // Pressure sensors — registered by (slave_id, reg_addr, label)
  void add_pressure_sensor(uint8_t slave_id, uint16_t reg_addr,
                           const std::string &label, sensor::Sensor *s);

  // Diagnostic sensors
  void set_frames_sensor(sensor::Sensor *s)            { frames_sensor_    = s; }
  void set_valid_sensor(sensor::Sensor *s)             { valid_sensor_     = s; }
  void set_bytes_sensor(sensor::Sensor *s)             { bytes_sensor_     = s; }
  void set_discovery_sensor(text_sensor::TextSensor *s){ discovery_sensor_ = s; }

  // Accessors
  const std::map<uint8_t, DamperState>     &get_dampers()     const { return dampers_; }
  const std::map<uint8_t, ZoneDeviceState> &get_zone_devices() const { return zone_devices_; }

 protected:
  // Modbus frame parsing
  static uint16_t crc16_modbus(const uint8_t *data, size_t len);
  void try_parse_frame();
  void process_frame(const uint8_t *data, size_t len);
  bool find_valid_frame(const std::vector<uint8_t> &buf,
                        size_t &offset, size_t &frame_len);
  void decode_and_apply(const uint8_t *data, size_t len);

  // Per-register callback
  void on_register_value(uint8_t slave_id, uint8_t fc,
                         uint16_t reg_addr, uint16_t value);

  // Write interceptors
  void on_write_single(uint8_t slave_id, uint16_t reg_addr, uint16_t value);
  void on_write_multiple(uint8_t slave_id, uint16_t start_addr,
                         uint16_t reg_count, const uint8_t *reg_bytes);

  // Per-slave-group decoders
  void decode_zone_sensor(uint8_t slave_id, uint16_t reg, uint16_t value);
  void decode_alfa_main  (uint8_t slave_id, uint16_t reg, uint16_t value);
  void decode_alfa_other (uint8_t slave_id, uint16_t reg, uint16_t value);
  void decode_peripheral (uint8_t slave_id, uint16_t reg, uint16_t value);
  void decode_damper     (uint8_t slave_id, uint16_t reg, uint16_t value);
  void decode_high_slave (uint8_t slave_id, uint16_t reg, uint16_t value);

  // Pressure helper — checks pressure map and publishes if registered
  void maybe_publish_pressure(uint8_t slave_id, uint16_t reg, uint16_t value);

  // Discovery
  void snapshot_reg(uint8_t slave_id, uint16_t reg, uint16_t value);
  void log_discovery_summary();
  void publish_discovery_summary();

  static const KnownReg *lookup_known_reg(uint8_t slave_id, uint16_t reg);
  static const char     *classify_slave(uint8_t slave_id);

  // Correlation snapshot — logs all damper positions + zone CO2 when fan changes
  void log_damper_correlation(const char *trigger, int trigger_val);
  int  get_snapshot_value(uint8_t slave_id, uint16_t reg);

  // Utility
  void log_diagnostics();
  void hex_dump(const char *prefix, const uint8_t *data,
                size_t len, size_t max_bytes = 40);

  // ── State ──────────────────────────────────────────────────────────────────
  float    frame_gap_ms_{3.0f};
  bool     discovery_mode_{true};
  uint32_t summary_interval_ms_{60000};

  std::vector<uint8_t> buffer_;
  uint32_t last_byte_time_{0};

  uint32_t frames_total_{0};
  uint32_t frames_valid_crc_{0};
  uint32_t bytes_received_{0};
  uint32_t buffers_discarded_{0};
  uint32_t last_diag_time_{0};
  uint32_t last_summary_time_{0};

  std::map<uint8_t, DamperState>     dampers_;
  std::map<uint8_t, ZoneDeviceState> zone_devices_;
  SystemState                        system_;

  // Pressure map key: (slave_id << 16) | reg_addr
  std::map<uint32_t, PressureState> pressure_sensors_;

  std::map<uint16_t, PendingRequest> pending_requests_;
  std::map<uint32_t, RegSnapshot>    reg_snapshots_;
  std::set<uint8_t>                  seen_slaves_;

  sensor::Sensor           *frames_sensor_{nullptr};
  sensor::Sensor           *valid_sensor_{nullptr};
  sensor::Sensor           *bytes_sensor_{nullptr};
  text_sensor::TextSensor  *discovery_sensor_{nullptr};
};

}  // namespace futura_bus
}  // namespace esphome
