#include "futura_bus.h"
#include "esphome/core/log.h"
#include <cstdio>
#include <cstring>

namespace esphome {
namespace futura_bus {

static const char *const TAG = "futura_bus";

// ─────────────────────────────────────────────────────────────────────────────
// Known register table
// ─────────────────────────────────────────────────────────────────────────────
static const KnownReg ZONE_SENSOR_REGS[] = {
  /* 91 */ {"status",    1.0f,   ""},
  /* 92 */ {"out_temp",  0.01f,  "°C"},
  /* 93 */ {"room_temp", 0.01f,  "°C"},
  /* 94 */ {"humidity",  0.01f,  "%RH"},  // CONFIRMED: ×0.01
  /* 95 */ {"co2",       1.0f,   "ppm"},
};

const KnownReg *FuturaBusComponent::lookup_known_reg(uint8_t slave_id,
                                                     uint16_t reg) {
  if (slave_id >= 9 && slave_id <= 13) {
    if (reg >= 91 && reg <= 95) return &ZONE_SENSOR_REGS[reg - 91];
  }
  if (slave_id == 16) {
    if (reg ==  37) { static KnownReg r{"own_co2",        1.0f,   "ppm"}; return &r; }
    if (reg ==  49) { static KnownReg r{"fan_airflow",    1.0f,   "step"}; return &r; }
    if (reg ==  68) { static KnownReg r{"temp_room",      0.1f,   "°C"}; return &r; }
    if (reg ==  69) { static KnownReg r{"humidity",       1.0f,   "%RH"}; return &r; }
    if (reg ==  70) { static KnownReg r{"co2",            1.0f,   "ppm"}; return &r; }
    if (reg == 181) { static KnownReg r{"mode_status",    1.0f,   ""}; return &r; }
    if (reg == 193) { static KnownReg r{"nominal_airflow",1.0f,   "m3/h"}; return &r; }
    if (reg == 194) { static KnownReg r{"active_zones",   1.0f,   ""}; return &r; }
    if (reg == 198) { static KnownReg r{"exhaust_fan_setpoint",1.0f,   ""}; return &r; }
    if (reg == 199) { static KnownReg r{"supply_fan_setpoint", 1.0f,   ""}; return &r; }
    if (reg == 255) { static KnownReg r{"transition_timer",      1.0f,   ""}; return &r; }
    if (reg == 256) { static KnownReg r{"ramp_countdown",      1.0f,   ""}; return &r; }
    if (reg == 296) { static KnownReg r{"boost_cmd",      1.0f,   ""}; return &r; }
    if (reg ==  14) { static KnownReg r{"boost_pace_or_mode",1.0f,""}; return &r; }
    if (reg >= 50 && reg <= 66) {
      static KnownReg r{"duct_pressure?", 1.0f, "Pa?"};
      return &r;
    }
  }
  if (slave_id >= 32 && slave_id <= 34) {
    if (reg == 105) { static KnownReg r{"peripheral_r105", 1.0f, ""}; return &r; }
    if (reg == 106) { static KnownReg r{"boost_event",     1.0f, ""}; return &r; }
  }
  if (slave_id >= 64 && slave_id <= 127) {
    if (reg == 102) { static KnownReg r{"target_pos",  1.0f, "%"}; return &r; }
    if (reg == 107) { static KnownReg r{"status_code", 1.0f, ""}; return &r; }
  }
  return nullptr;
}

const char *FuturaBusComponent::classify_slave(uint8_t slave_id) {
  if (slave_id >= 9  && slave_id <= 13)  return "WALL_PANEL";
  if (slave_id == 16)                    return "ALFA_MAIN";
  if (slave_id >= 17 && slave_id <= 31)  return "ALFA_OTHER";
  if (slave_id >= 32 && slave_id <= 34)  return "BOOST_CTRL";
  if (slave_id >= 64 && slave_id <= 95)  return "DAMPER_SUPPLY";
  if (slave_id >= 96 && slave_id <= 127) return "DAMPER_EXHAUST";
  if (slave_id >= 128)                   return "HIGH_SLAVE";
  return "UNKNOWN";
}

// ─────────────────────────────────────────────────────────────────────────────
// CRC16 Modbus
// ─────────────────────────────────────────────────────────────────────────────
uint16_t FuturaBusComponent::crc16_modbus(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xA001;
      else         crc >>= 1;
    }
  }
  return crc;
}

void FuturaBusComponent::hex_dump(const char *prefix, const uint8_t *data,
                                  size_t len, size_t max_bytes) {
  char buf[128];
  size_t show = (len > max_bytes) ? max_bytes : len;
  size_t pos  = 0;
  for (size_t i = 0; i < show && pos < sizeof(buf) - 4; i++)
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X ", data[i]);
  if (len > max_bytes)
    snprintf(buf + pos, sizeof(buf) - pos, "...");
  ESP_LOGD(TAG, "%s (%d bytes): %s", prefix, (int)len, buf);
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────
void FuturaBusComponent::add_damper_config(uint8_t slave_id,
                                           const std::string &room) {
  DamperConfig cfg{slave_id, room};
  DamperState  st;
  st.config = cfg;
  dampers_[slave_id] = st;
}

DamperState &FuturaBusComponent::ensure_damper(uint8_t slave_id) {
  if (!dampers_.count(slave_id)) {
    DamperConfig cfg{slave_id, ""};
    char name[32];
    if (cfg.index() > 1)
      snprintf(name, sizeof(name), "%s Z%d-I%d", cfg.type_str(), cfg.zone(), cfg.index());
    else
      snprintf(name, sizeof(name), "%s Z%d", cfg.type_str(), cfg.zone());
    cfg.room = name;
    DamperState st;
    st.config = cfg;
    dampers_[slave_id] = st;
    ESP_LOGW(TAG, ">>> AUTO-DISCOVERED damper s=%d → %s (add to YAML for HA sensors)", slave_id, name);
    update_discovery_text_();
  }
  return dampers_[slave_id];
}

ZoneDeviceState &FuturaBusComponent::ensure_zone_device(uint8_t slave_id) {
  if (!zone_devices_.count(slave_id)) {
    ZoneDeviceState st;
    st.slave_id = slave_id;
    char name[32];
    if (slave_id == 16)
      snprintf(name, sizeof(name), "ALFA panel");
    else
      snprintf(name, sizeof(name), "Zone %d", slave_id - 8);
    st.room = name;
    zone_devices_[slave_id] = st;
    ESP_LOGW(TAG, ">>> AUTO-DISCOVERED zone device s=%d → %s (add to YAML for HA sensors)", slave_id, name);
    update_discovery_text_();
  }
  return zone_devices_[slave_id];
}
void FuturaBusComponent::register_position_sensor(uint8_t slave_id, sensor::Sensor *s) {
  // Pre-create damper entry without auto-creating sensors
  if (!dampers_.count(slave_id)) {
    DamperConfig cfg{slave_id, ""};
    char name[32];
    if (cfg.index() > 1)
      snprintf(name, sizeof(name), "%s Z%d-I%d", cfg.type_str(), cfg.zone(), cfg.index());
    else
      snprintf(name, sizeof(name), "%s Z%d", cfg.type_str(), cfg.zone());
    cfg.room = name;
    DamperState st;
    st.config = cfg;
    dampers_[slave_id] = st;
  }
  dampers_[slave_id].position_sensor = s;
}
void FuturaBusComponent::register_status_sensor(uint8_t slave_id, sensor::Sensor *s) {
  if (!dampers_.count(slave_id)) {
    DamperConfig cfg{slave_id, ""};
    char name[32];
    if (cfg.index() > 1)
      snprintf(name, sizeof(name), "%s Z%d-I%d", cfg.type_str(), cfg.zone(), cfg.index());
    else
      snprintf(name, sizeof(name), "%s Z%d", cfg.type_str(), cfg.zone());
    cfg.room = name;
    DamperState st;
    st.config = cfg;
    dampers_[slave_id] = st;
  }
  dampers_[slave_id].status_sensor = s;
}
void FuturaBusComponent::add_zone_device(uint8_t slave_id, const std::string &room) {
  ZoneDeviceState st;
  st.slave_id = slave_id;
  st.room     = room;
  zone_devices_[slave_id] = st;
}
void FuturaBusComponent::register_zone_temp(uint8_t slave_id, sensor::Sensor *s) {
  if (!zone_devices_.count(slave_id)) {
    ZoneDeviceState st;
    st.slave_id = slave_id;
    st.room = (slave_id == 16) ? "ALFA panel" : "Zone " + std::to_string(slave_id - 8);
    zone_devices_[slave_id] = st;
  }
  zone_devices_[slave_id].temp_sensor = s;
}
void FuturaBusComponent::register_zone_humidity(uint8_t slave_id, sensor::Sensor *s) {
  if (!zone_devices_.count(slave_id)) {
    ZoneDeviceState st;
    st.slave_id = slave_id;
    st.room = (slave_id == 16) ? "ALFA panel" : "Zone " + std::to_string(slave_id - 8);
    zone_devices_[slave_id] = st;
  }
  zone_devices_[slave_id].humidity_sensor = s;
}
void FuturaBusComponent::register_zone_co2(uint8_t slave_id, sensor::Sensor *s) {
  if (!zone_devices_.count(slave_id)) {
    ZoneDeviceState st;
    st.slave_id = slave_id;
    st.room = (slave_id == 16) ? "ALFA panel" : "Zone " + std::to_string(slave_id - 8);
    zone_devices_[slave_id] = st;
  }
  zone_devices_[slave_id].co2_sensor = s;
}
void FuturaBusComponent::add_pressure_sensor(uint8_t slave_id, uint16_t reg_addr,
                                              const std::string &label,
                                              sensor::Sensor *s) {
  uint32_t key = ((uint32_t)slave_id << 16) | reg_addr;
  PressureState ps;
  ps.slave_id        = slave_id;
  ps.reg_addr        = reg_addr;
  ps.label           = label;
  ps.pressure_sensor = s;
  pressure_sensors_[key] = ps;
}

// ─────────────────────────────────────────────────────────────────────────────
// setup / dump_config
// ─────────────────────────────────────────────────────────────────────────────
void FuturaBusComponent::setup() {
  ESP_LOGCONFIG(TAG, "Futura Bus v11.4 starting");
  ESP_LOGCONFIG(TAG, "  Dampers      : %d", (int)dampers_.size());
  ESP_LOGCONFIG(TAG, "  Zone devices : %d", (int)zone_devices_.size());
  ESP_LOGCONFIG(TAG, "  Pressure/evt : %d", (int)pressure_sensors_.size());
  ESP_LOGCONFIG(TAG, "  Discovery    : %s  interval=%us",
                discovery_mode_ ? "ON" : "OFF",
                (unsigned)(summary_interval_ms_ / 1000));
  last_diag_time_    = millis();
  last_summary_time_ = millis();
}

void FuturaBusComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Futura Bus RS485 sniffer v11.4:");
  for (auto &kv : dampers_) {
    auto &d = kv.second;
    ESP_LOGCONFIG(TAG, "  Damper s=%d zone=%d %s %s",
                  d.config.slave_id, d.config.zone(),
                  d.config.type_str(), d.config.room.c_str());
  }
  for (auto &kv : zone_devices_)
    ESP_LOGCONFIG(TAG, "  Zone   s=%d %s", kv.second.slave_id, kv.second.room.c_str());
  for (auto &kv : pressure_sensors_)
    ESP_LOGCONFIG(TAG, "  Sensor s=%d r=%d \"%s\"",
                  kv.second.slave_id, kv.second.reg_addr, kv.second.label.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// loop
// ─────────────────────────────────────────────────────────────────────────────
void FuturaBusComponent::loop() {
  const uint32_t now = millis();
  while (this->available()) {
    uint8_t b;
    this->read_byte(&b);
    buffer_.push_back(b);
    last_byte_time_ = now;
    bytes_received_++;
    if (bytes_received_ == 1)
      ESP_LOGI(TAG, ">>> First byte received – data is flowing!");
  }
  if (buffer_.empty()) { log_diagnostics(); return; }

  if ((now - last_byte_time_) >= (uint32_t)(frame_gap_ms_) + 1)
    try_parse_frame();

  if (discovery_mode_ && (now - last_summary_time_ >= summary_interval_ms_)) {
    last_summary_time_ = now;
    log_discovery_summary();
    publish_discovery_summary();
  }
  log_diagnostics();
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame parsing
// ─────────────────────────────────────────────────────────────────────────────
bool FuturaBusComponent::find_valid_frame(const std::vector<uint8_t> &buf,
                                          size_t &offset, size_t &frame_len) {
  for (size_t off = 0; off < buf.size(); off++) {
    if (buf.size() - off < 4) break;
    uint8_t slave_id = buf[off];
    uint8_t fc       = buf[off + 1];
    if (slave_id > 247) continue;

    std::vector<size_t> cands;
    if (fc & 0x80) {
      cands.push_back(5);
    } else if (fc == 0x01 || fc == 0x02) {
      cands.push_back(8);
      if (buf.size() - off >= 3) {
        uint8_t bc = buf[off + 2];
        if (bc > 0 && bc <= 250) cands.push_back((size_t)(bc + 5));
      }
    } else if (fc == 0x03 || fc == 0x04) {
      cands.push_back(8);
      if (buf.size() - off >= 3) {
        uint8_t bc = buf[off + 2];
        if (bc > 0 && bc <= 250 && (bc % 2) == 0)
          cands.push_back((size_t)(bc + 5));
      }
    } else if (fc == 0x05 || fc == 0x06) {
      cands.push_back(8);
    } else if (fc == 0x0F) {
      cands.push_back(8);
      if (buf.size() - off >= 7) {
        uint8_t bc = buf[off + 6];
        if (bc > 0 && bc <= 246) cands.push_back((size_t)(bc + 9));
      }
    } else if (fc == 0x10) {
      cands.push_back(8);
      if (buf.size() - off >= 7) {
        uint8_t bc = buf[off + 6];
        if (bc > 0 && bc <= 246 && (bc % 2) == 0)
          cands.push_back((size_t)(bc + 9));
      }
    } else {
      continue;
    }

    size_t best = 0;
    for (size_t len : cands) {
      if (buf.size() - off < len) continue;
      uint16_t exp = crc16_modbus(&buf[off], len - 2);
      uint16_t got = buf[off + len - 2] | ((uint16_t)buf[off + len - 1] << 8);
      if (exp == got && len > best) best = len;
    }
    if (best > 0) { offset = off; frame_len = best; return true; }
  }
  return false;
}

void FuturaBusComponent::try_parse_frame() {
  while (!buffer_.empty()) {
    size_t off = 0, len = 0;
    if (find_valid_frame(buffer_, off, len)) {
      if (off > 0) buffer_.erase(buffer_.begin(), buffer_.begin() + off);
      process_frame(buffer_.data(), len);
      buffer_.erase(buffer_.begin(), buffer_.begin() + len);
    } else {
      hex_dump("Discarded", buffer_.data(), buffer_.size());
      buffers_discarded_++;
      buffer_.clear();
      break;
    }
  }
}

void FuturaBusComponent::process_frame(const uint8_t *data, size_t len) {
  frames_total_++;
  uint16_t exp = crc16_modbus(data, len - 2);
  uint16_t got = data[len - 2] | ((uint16_t)data[len - 1] << 8);
  if (exp != got) return;
  frames_valid_crc_++;
  seen_slaves_.insert(data[0]);
  decode_and_apply(data, len);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main dispatcher
// ─────────────────────────────────────────────────────────────────────────────
void FuturaBusComponent::decode_and_apply(const uint8_t *data, size_t len) {
  if (len < 4) return;
  uint8_t        slave_id = data[0];
  uint8_t        fc       = data[1];
  const uint8_t *body     = data + 2;
  size_t         body_len = len - 4;

  if (dampers_.count(slave_id))      dampers_[slave_id].last_seen_ms      = millis();
  if (zone_devices_.count(slave_id)) zone_devices_[slave_id].last_seen_ms = millis();

  if (fc & 0x80) {
    ESP_LOGW(TAG, "EXCEPTION s=%d fc=0x%02X err=0x%02X", slave_id, fc & 0x7F, body[0]);
    return;
  }

  // FC3/FC4 request
  if ((fc == 0x03 || fc == 0x04) && body_len == 4) {
    uint16_t start = (uint16_t)(body[0] << 8) | body[1];
    uint16_t count = (uint16_t)(body[2] << 8) | body[3];
    uint16_t key   = (uint16_t)(slave_id << 8) | fc;
    pending_requests_[key] = {slave_id, fc, start, count, millis()};
    return;
  }

  // FC3/FC4 response
  if ((fc == 0x03 || fc == 0x04) && body_len >= 1) {
    uint8_t bc = body[0];
    if (!bc || (bc % 2) || body_len != (size_t)(bc + 1)) return;
    uint16_t key = (uint16_t)(slave_id << 8) | fc;
    auto it = pending_requests_.find(key);
    if (it == pending_requests_.end()) return;
    PendingRequest &req = it->second;
    uint16_t num = bc / 2;
    if (num != req.register_count) { pending_requests_.erase(it); return; }
    for (uint16_t i = 0; i < num; i++) {
      uint16_t addr  = req.start_address + i;
      uint16_t value = (uint16_t)(body[1 + i*2] << 8) | body[1 + i*2 + 1];
      on_register_value(slave_id, fc, addr, value);
    }
    pending_requests_.erase(it);
    return;
  }

  // FC6
  if (fc == 0x06 && body_len == 4) {
    uint16_t reg   = (uint16_t)(body[0] << 8) | body[1];
    uint16_t value = (uint16_t)(body[2] << 8) | body[3];
    on_write_single(slave_id, reg, value);
    return;
  }

  // FC16
  if (fc == 0x10) {
    if (body_len == 4) return;   // response echo
    if (body_len >= 5) {
      uint16_t start      = (uint16_t)(body[0] << 8) | body[1];
      uint16_t reg_count  = (uint16_t)(body[2] << 8) | body[3];
      uint8_t  byte_count = body[4];
      if (byte_count == reg_count * 2 && body_len == (size_t)(byte_count + 5))
        on_write_multiple(slave_id, start, reg_count, &body[5]);
    }
    return;
  }

  // FC5
  if (fc == 0x05 && body_len == 4) {
    uint16_t coil  = (uint16_t)(body[0] << 8) | body[1];
    uint16_t value = (uint16_t)(body[2] << 8) | body[3];
    ESP_LOGI(TAG, "FC5 COIL s=%d coil=%d v=0x%04X [%s]",
             slave_id, coil, value, classify_slave(slave_id));
    return;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// on_register_value
// ─────────────────────────────────────────────────────────────────────────────
void FuturaBusComponent::on_register_value(uint8_t slave_id, uint8_t fc,
                                           uint16_t reg, uint16_t value) {
  snapshot_reg(slave_id, reg, value);
  maybe_publish_pressure(slave_id, reg, value);

  if (discovery_mode_) {
    const KnownReg *kr = lookup_known_reg(slave_id, reg);
    if (kr)
      ESP_LOGD(TAG, "[%s] s=%d fc=%d r=%d = %d → %.2f %s (%s)",
               classify_slave(slave_id), slave_id, fc, reg, value,
               value * kr->scale, kr->unit, kr->name);
    else
      ESP_LOGD(TAG, "[%s] s=%d fc=%d r=%d = %d (UNKNOWN)",
               classify_slave(slave_id), slave_id, fc, reg, value);
  }

  if (slave_id >= 9  && slave_id <= 13)  { decode_zone_sensor(slave_id, reg, value); return; }
  if (slave_id == 16)                    { decode_alfa_main  (slave_id, reg, value); return; }
  if (slave_id >= 17 && slave_id <= 31)  { decode_alfa_other (slave_id, reg, value); return; }
  if (slave_id >= 32 && slave_id <= 34)  { decode_peripheral (slave_id, reg, value); return; }
  if (slave_id >= 64 && slave_id <= 127) { decode_damper     (slave_id, reg, value); return; }
  if (slave_id >= 128)                   { decode_high_slave (slave_id, reg, value); return; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Write interceptors
// ─────────────────────────────────────────────────────────────────────────────
void FuturaBusComponent::on_write_single(uint8_t slave_id,
                                         uint16_t reg, uint16_t value) {
  snapshot_reg(slave_id, reg, value);
  ESP_LOGI(TAG, "FC6 WRITE s=%d r=%d v=%d [%s]",
           slave_id, reg, value, classify_slave(slave_id));

  if (reg == 102 && slave_id >= 64 && slave_id <= 127) {
    auto &d = ensure_damper(slave_id);
    int nv  = (int)value;
    if (d.target_position == -1 || d.target_position != nv) {
      ESP_LOGI(TAG, ">>> DAMPER %d (%s) pos=%d%% (FC6, was %d)",
               slave_id, d.config.room.c_str(), nv, d.target_position);
      d.target_position = nv;
      d.last_target_ms  = millis();
      if (d.position_sensor) d.position_sensor->publish_state((float)nv);
    }
  }
  // Fan setpoints via FC6 (less common but possible)
  if (slave_id == 16) {
    if (reg == 198) { system_.exhaust_fan_pct = value; if (system_.exhaust_fan_pct_sensor) system_.exhaust_fan_pct_sensor->publish_state((float)value); }
    if (reg == 199) { system_.supply_fan_pct  = value; if (system_.supply_fan_pct_sensor)  system_.supply_fan_pct_sensor->publish_state((float)value); }
  }
}

void FuturaBusComponent::on_write_multiple(uint8_t slave_id,
                                           uint16_t start_addr,
                                           uint16_t reg_count,
                                           const uint8_t *reg_bytes) {
  ESP_LOGI(TAG, "FC16 WRITE s=%d start=%d count=%d [%s]",
           slave_id, start_addr, reg_count, classify_slave(slave_id));

  // ── ALFA16 specific writes ──────────────────────────────────────────────────
  if (slave_id == 16) {
    uint16_t val1 = (reg_count >= 1) ? ((uint16_t)(reg_bytes[0] << 8) | reg_bytes[1]) : 0;

    // reg296 = boost start command (discovered in bathroom boost log)
    if (start_addr == 296 && reg_count == 1) {
      system_.last_boost_ms = millis();
      ESP_LOGW(TAG, ">>> BOOST START CMD (reg296=%d) – damper sequence follows", val1);
    }
    // reg14 = pacing signal between damper openings during boost sequence
    if (start_addr == 14 && reg_count == 1) {
      ESP_LOGI(TAG, "  PACE reg14=%d (0x%04X) bits_changed=0x%04X", val1, val1,
               (system_.last_r14 >= 0) ? (val1 ^ (uint16_t)system_.last_r14) : 0);
      system_.last_r14 = (int)val1;
    }
    // reg194 = active zones — normally constant 6, but counts 1→6 during zone analyze
    if (start_addr == 194 && reg_count == 1) {
      int prev = system_.active_zones;
      system_.active_zones = (int)val1;
      if (prev != (int)val1) {
        if (val1 < (uint16_t)prev || val1 == 1)
          ESP_LOGW(TAG, ">>> ZONE ANALYZE START zone=%d (was %d)", val1, prev);
        else
          ESP_LOGI(TAG, ">>> ZONE ANALYZE step zone=%d (was %d)", val1, prev);
      }
    }
    // reg198/199 = fan setpoints (periodic, ~every 30s)
    if (start_addr == 198 && reg_count == 1) {
      system_.exhaust_fan_pct = (int)val1;
      if (system_.exhaust_fan_pct_sensor) system_.exhaust_fan_pct_sensor->publish_state((float)val1);
      // NOTE: value can exceed 100 — this is an absolute setpoint, not a percentage
      ESP_LOGI(TAG, ">>> FAN SETPOINT exhaust=%d (abs. units, not %%)", val1);
      log_damper_correlation("exhaust_setpoint_change", val1);
    }
    if (start_addr == 199 && reg_count == 1) {
      system_.supply_fan_pct = (int)val1;
      if (system_.supply_fan_pct_sensor) system_.supply_fan_pct_sensor->publish_state((float)val1);
      ESP_LOGI(TAG, ">>> FAN SETPOINT supply=%d (abs. units, not %%)", val1);
      log_damper_correlation("supply_setpoint_change", val1);
    }
    // reg10+11 = older boost command format (observed in earlier session)
    if (start_addr == 10 && reg_count == 2) {
      uint16_t r11 = (uint16_t)(reg_bytes[2] << 8) | reg_bytes[3];
      system_.last_boost_ms = millis();
      ESP_LOGW(TAG, ">>> BOOST CMD (reg10=0x%04X reg11=0x%04X)", val1, r11);
    }
  }

  for (uint16_t i = 0; i < reg_count; i++) {
    uint16_t addr  = start_addr + i;
    uint16_t value = (uint16_t)(reg_bytes[i * 2] << 8) | reg_bytes[i * 2 + 1];
    snapshot_reg(slave_id, addr, value);
    maybe_publish_pressure(slave_id, addr, value);  // publish FC16 writes to HA sensors too
    ESP_LOGI(TAG, "  r=%d v=%d", addr, value);

    // ALFA16 r256: active ramp/countdown — written every ~2s via FC16
    // Observed: 59→0 over ~57s during fan speed transitions
    if (slave_id == 16 && addr == 256) {
      ESP_LOGI(TAG, ">>> ALFA16 ramp r256=%d", value);
      if (value == 0 && system_.last_r256 > 0)
        ESP_LOGI(TAG, ">>> ALFA16 ramp r256 completed (was %d)", system_.last_r256);
      system_.last_r256 = (int)value;
    }

    if (addr == 102 && slave_id >= 64 && slave_id <= 127) {
      auto &d = ensure_damper(slave_id);
      int nv  = (int)value;
      if (d.target_position == -1 || d.target_position != nv) {
        ESP_LOGI(TAG, ">>> DAMPER %d (%s) pos=%d%% (FC16, was %d)",
                 slave_id, d.config.room.c_str(), nv, d.target_position);
        d.target_position = nv;
        d.last_target_ms  = millis();
        if (d.position_sensor) d.position_sensor->publish_state((float)nv);
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pressure helper
// ─────────────────────────────────────────────────────────────────────────────
void FuturaBusComponent::maybe_publish_pressure(uint8_t slave_id,
                                                uint16_t reg, uint16_t value) {
  uint32_t key = ((uint32_t)slave_id << 16) | reg;
  auto it = pressure_sensors_.find(key);
  if (it == pressure_sensors_.end()) return;
  float fv = (float)value;
  if (it->second.value_pa != fv) {
    it->second.value_pa     = fv;
    it->second.last_seen_ms = millis();
    if (it->second.pressure_sensor)
      it->second.pressure_sensor->publish_state(fv);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Decoders
// ─────────────────────────────────────────────────────────────────────────────

// Wall panels slaves 9-13
// reg91: status (1=plain, 3=has boost button — confirmed)
// reg92: out_temp × 0.01 °C
// reg93: room_temp × 0.01 °C
// reg94: humidity × 0.01 %RH — CONFIRMED from log analysis
// reg95: CO₂ ppm
void FuturaBusComponent::decode_zone_sensor(uint8_t slave_id,
                                            uint16_t reg, uint16_t value) {
  auto &z = ensure_zone_device(slave_id);
  switch (reg) {
    case 91:
      z.status           = (int)value;
      z.has_boost_button = (value & 0x02) != 0;
      break;
    case 92:
      z.temp_out = value * 0.01f;
      break;
    case 93: {
      float t = value * 0.01f;
      z.temp_room = t;
      if (z.temp_sensor) z.temp_sensor->publish_state(t);
      break;
    }
    case 94: {
      float h = value * 0.01f;  // CONFIRMED
      z.humidity = h;
      if (z.humidity_sensor) z.humidity_sensor->publish_state(h);
      break;
    }
    case 95: {
      float co2 = (float)value;
      z.co2 = co2;
      if (z.co2_sensor) z.co2_sensor->publish_state(co2);
      break;
    }
    default:
      ESP_LOGD(TAG, "ZONE_SENSOR s=%d unknown r=%d v=%d", slave_id, reg, value);
      break;
  }
}

// ALFA main panel slave 16
void FuturaBusComponent::decode_alfa_main(uint8_t slave_id,
                                          uint16_t reg, uint16_t value) {
  auto &zd = ensure_zone_device(slave_id);

  if (reg == 37) {
    system_.alfa_co2 = (float)value;
    ESP_LOGD(TAG, "ALFA16 own_co2=%.0f ppm", system_.alfa_co2);
    return;
  }
  if (reg == 49) {
    int step  = (int)value;
    int level = (step >= 64) ? (step / 64) : 0;
    bool changed = (system_.fan_airflow != step);
    int prev = system_.fan_airflow;
    system_.fan_airflow = step;
    if (changed) {
      // Detect characteristic 64→320 spike during mode transitions
      if (step >= 256 && prev >= 0 && prev < 128)
        ESP_LOGW(TAG, ">>> AIRFLOW SPIKE %d→%d (mode transition pulse)", prev, step);
      else if (step < 128 && prev >= 256)
        ESP_LOGW(TAG, ">>> AIRFLOW SPIKE END %d→%d", prev, step);
      else
        ESP_LOGI(TAG, "ALFA16 fan_airflow=%d (level~%d)", step, level);
      log_damper_correlation("fan_airflow_change", step);
    }
    if (system_.fan_airflow_sensor)
      system_.fan_airflow_sensor->publish_state((float)step);
    return;
  }
  // reg14: boost pacing signal — written between each damper opening
  if (reg == 14) {
    ESP_LOGD(TAG, "ALFA16 reg14=0x%04X (%d) readback", value, value);
    return;
  }
  // Regs 50-66: duct pressures (all 0 in stable state)
  // reg67 = 0xFC22 = constant marker, always skip
  if (reg == 67) return;
  if (reg >= 50 && reg <= 66) {
    if (value != 0)
      ESP_LOGI(TAG, "ALFA16 duct_pressure r%d = %d (non-zero!)", reg, value);
    return;
  }
  if (reg == 68) {
    float t = value * 0.1f;
    zd.temp_room = t;
    if (zd.temp_sensor) zd.temp_sensor->publish_state(t);
    return;
  }
  if (reg == 69) {
    float h = (float)value;
    zd.humidity = h;
    if (zd.humidity_sensor) zd.humidity_sensor->publish_state(h);
    return;
  }
  if (reg == 70) {
    float co2 = (float)value;
    zd.co2 = co2;
    if (zd.co2_sensor) zd.co2_sensor->publish_state(co2);
    return;
  }
  if (reg == 181) { system_.mode_status    = (int)value; return; }
  if (reg == 193) { system_.nominal_airflow = (int)value; return; }
  if (reg == 194) {
    int prev = system_.active_zones;
    system_.active_zones = (int)value;
    if (prev >= 0 && prev != (int)value)
      ESP_LOGI(TAG, "ALFA16 active_zones %d→%d", prev, (int)value);
    return;
  }
  if (reg == 296) {
    // reg296 = boost start command (written by Futura before opening exhaust dampers)
    system_.last_boost_ms = millis();
    ESP_LOGI(TAG, "ALFA16 reg296=%d (boost cmd read back)", value);
    return;
  }
  // reg255: fan speed closed-loop register
  // Futura writes every ~2s then reads back (confirmed). Range 796-1058.
  // Slowly decreasing = fan ramping down. Unit TBD: likely RPM or proportional.
  if (reg == 255) {
    ESP_LOGD(TAG, "ALFA16 transition_timer=%d", value);
    return;
  }
  // reg253/254: always 1439 — calibration constant, ignore
  if (reg == 253 || reg == 254) {
    ESP_LOGV(TAG, "ALFA16 r%d=1439 (calibration constant)", reg);
    return;
  }
  // reg256: ramp/countdown — read back via FC3 (usually 0), actively written via FC16
  if (reg == 256) {
    ESP_LOGD(TAG, "ALFA16 ramp_readback r256=%d", value);
    return;
  }
  if (reg >= 249 && reg <= 260) {
    ESP_LOGD(TAG, "ALFA16 diag r%d=%d", reg, value);
    return;
  }
  ESP_LOGD(TAG, "ALFA16 r%d=%d", reg, value);
}

// ALFA other: slaves 17-31
void FuturaBusComponent::decode_alfa_other(uint8_t slave_id,
                                           uint16_t reg, uint16_t value) {
  ESP_LOGD(TAG, "ALFA_OTHER s=%d r%d=%d", slave_id, reg, value);
}

// Boost controllers: slaves 32-34
// reg105: purpose unknown (always 0)
// reg106: boost button event indicator
//   slave 32 reg106 = kitchen boost button
//   slave 33 reg106 = WC/toilet boost button
//   slave 34 reg106 = bathroom boost button
// maybe_publish_pressure() handles HA sensor publishing for these
void FuturaBusComponent::decode_peripheral(uint8_t slave_id,
                                           uint16_t reg, uint16_t value) {
  if (reg == 106) {
    if (value != 0) {
      const char *zone = "unknown";
      if (slave_id == 32) zone = "KITCHEN";
      else if (slave_id == 33) zone = "WC";
      else if (slave_id == 34) zone = "BATHROOM";
      ESP_LOGW(TAG, ">>> BOOST EVENT: %s button pressed (s=%d r106=%d)",
               zone, slave_id, value);
    } else {
      ESP_LOGD(TAG, "BOOST_CTRL s=%d r106=0 (idle)", slave_id);
    }
  } else {
    ESP_LOGD(TAG, "BOOST_CTRL s=%d r%d=%d", slave_id, reg, value);
  }
}

// VarioBreeze dampers: slaves 64-127
// STATUS CODES (confirmed from log):
//   1 = stable (reached position / idle)
//   4 = actuator in motion (transient during boost sequence)
//   0, 2 = TBD
void FuturaBusComponent::decode_damper(uint8_t slave_id,
                                       uint16_t reg, uint16_t value) {
  auto &d = ensure_damper(slave_id);
  d.last_seen_ms = millis();

  if (reg == 107) {
    int nv = (int)value;
    if (d.status_code == -1 || d.status_code != nv) {
      // Status meanings: 1=stable, 4=moving, 0/2=TBD
      const char *meaning = (nv==1) ? "stable" : (nv==4) ? "moving" : "?";
      ESP_LOGI(TAG, ">>> DAMPER %d (%s) status=%d (%s, was %d)",
               slave_id, d.config.room.c_str(), nv, meaning, d.status_code);
      d.status_code    = nv;
      d.last_status_ms = millis();
      if (d.status_sensor) d.status_sensor->publish_state((float)nv);
    }
  } else {
    ESP_LOGI(TAG, "DAMPER s=%d UNKNOWN r=%d v=%d", slave_id, reg, value);
  }
}

// High slaves 128+
void FuturaBusComponent::decode_high_slave(uint8_t slave_id,
                                           uint16_t reg, uint16_t value) {
  uint32_t key = ((uint32_t)slave_id << 16) | reg;
  auto it = reg_snapshots_.find(key);
  if (it != reg_snapshots_.end() && it->second.last_value != value)
    ESP_LOGI(TAG, "HIGH_SLAVE s=%d (mirror %d?) r=%d %d→%d",
             slave_id, slave_id-128, reg, it->second.last_value, value);
  else
    ESP_LOGV(TAG, "HIGH_SLAVE s=%d r=%d v=%d", slave_id, reg, value);
}

// ─────────────────────────────────────────────────────────────────────────────
// Discovery
// ─────────────────────────────────────────────────────────────────────────────
void FuturaBusComponent::snapshot_reg(uint8_t slave_id, uint16_t reg, uint16_t value) {
  uint32_t key = ((uint32_t)slave_id << 16) | reg;
  auto it = reg_snapshots_.find(key);
  if (it == reg_snapshots_.end()) {
    RegSnapshot s;
    s.last_value = s.min_value = s.max_value = value;
    s.last_seen_ms = millis(); s.seen_count = 1; s.ever_changed = false;
    reg_snapshots_[key] = s;
  } else {
    if (it->second.last_value != value) it->second.ever_changed = true;
    it->second.last_value = value;
    if (value < it->second.min_value) it->second.min_value = value;
    if (value > it->second.max_value) it->second.max_value = value;
    it->second.last_seen_ms = millis();
    it->second.seen_count++;
  }
}

void FuturaBusComponent::log_discovery_summary() {
  ESP_LOGI(TAG, "=== DISCOVERY: %d slaves, %d (slave,reg) pairs ===",
           (int)seen_slaves_.size(), (int)reg_snapshots_.size());
  uint8_t prev = 0xFF;
  for (auto &kv : reg_snapshots_) {
    uint8_t  slave = (uint8_t)(kv.first >> 16);
    uint16_t reg   = (uint16_t)(kv.first & 0xFFFF);
    auto    &snap  = kv.second;
    if (slave != prev) {
      ESP_LOGI(TAG, "  ── slave %d [%s] ──", slave, classify_slave(slave));
      prev = slave;
    }
    const KnownReg *kr = lookup_known_reg(slave, reg);
    if (kr)
      ESP_LOGI(TAG, "    r%-3d %-18s last=%-6d (%.2f %s) min=%-6d max=%-6d n=%u %s",
               reg, kr->name, snap.last_value, snap.last_value * kr->scale, kr->unit,
               snap.min_value, snap.max_value, snap.seen_count,
               snap.ever_changed ? "CHANGED" : "-");
    else
      ESP_LOGI(TAG, "    r%-3d %-18s last=%-6d min=%-6d max=%-6d n=%u %s",
               reg, "???", snap.last_value,
               snap.min_value, snap.max_value, snap.seen_count,
               snap.ever_changed ? "CHANGED" : "-");
  }
  ESP_LOGI(TAG, "=== END DISCOVERY ===");
}

void FuturaBusComponent::publish_discovery_summary() {
  if (!discovery_sensor_) return;
  char buf[255];
  size_t pos = 0;
  uint8_t prev = 0xFF;
  bool first = true;
  for (auto &kv : reg_snapshots_) {
    uint8_t  slave = (uint8_t)(kv.first >> 16);
    uint16_t reg   = (uint16_t)(kv.first & 0xFFFF);
    if (reg == 0xFFFF) continue;
    auto &snap = kv.second;
    if (slave != prev) {
      if (!first && pos < sizeof(buf)-2) buf[pos++] = '|';
      first = false;
      int n = snprintf(buf+pos, sizeof(buf)-pos, "S%d:", slave);
      if (n > 0) pos = (pos+n < sizeof(buf)) ? pos+n : sizeof(buf)-1;
      prev = slave;
    } else {
      if (pos < sizeof(buf)-2) buf[pos++] = ',';
    }
    int n = snprintf(buf+pos, sizeof(buf)-pos, "r%d=%d", reg, snap.last_value);
    if (n > 0) pos = (pos+n < sizeof(buf)) ? pos+n : sizeof(buf)-1;
    if (pos >= sizeof(buf)-20) { snprintf(buf+pos, sizeof(buf)-pos, "..."); break; }
  }
  buf[sizeof(buf)-1] = '\0';
  discovery_sensor_->publish_state(std::string(buf));
}

void FuturaBusComponent::update_discovery_text_() {
  if (!discovery_sensor_) return;
  std::string out;
  for (auto &kv : dampers_) {
    auto &d = kv.second;
    char line[64];
    snprintf(line, sizeof(line), "s%d %s%s\n",
             d.config.slave_id, d.config.room.c_str(),
             d.position_sensor ? "" : " [no YAML]");
    out += line;
  }
  for (auto &kv : zone_devices_) {
    auto &z = kv.second;
    char line[64];
    snprintf(line, sizeof(line), "s%d %s%s\n",
             z.slave_id, z.room.c_str(),
             z.temp_sensor ? "" : " [no YAML]");
    out += line;
  }
  discovery_sensor_->publish_state(out);
}

// ─────────────────────────────────────────────────────────────────────────────
// Correlation snapshot — called when fan setpoint or airflow changes.
// Logs every damper's current known position and status at that instant.
// This reveals which zones react to fan changes and helps decode reg50-66.
// ─────────────────────────────────────────────────────────────────────────────
void FuturaBusComponent::log_damper_correlation(const char *trigger, int trigger_val) {
  ESP_LOGI(TAG, "=== CORRELATION [%s=%d] exhaust=%d supply=%d ===",
           trigger, trigger_val,
           system_.exhaust_fan_pct, system_.supply_fan_pct);

  // Log all supply dampers
  for (auto &kv : dampers_) {
    auto &d = kv.second;
    if (!d.config.is_supply()) continue;
    if (d.target_position >= 0)
      ESP_LOGI(TAG, "  SUPPLY s=%d %-20s pos=%d%% status=%d",
               d.config.slave_id, d.config.room.c_str(),
               d.target_position, d.status_code);
    else
      ESP_LOGI(TAG, "  SUPPLY s=%d %-20s pos=? (never written) status=%d",
               d.config.slave_id, d.config.room.c_str(), d.status_code);
  }

  // Log all exhaust dampers
  for (auto &kv : dampers_) {
    auto &d = kv.second;
    if (!d.config.is_exhaust()) continue;
    if (d.target_position >= 0)
      ESP_LOGI(TAG, "  EXHAUST s=%d %-20s pos=%d%% status=%d",
               d.config.slave_id, d.config.room.c_str(),
               d.target_position, d.status_code);
    else
      ESP_LOGI(TAG, "  EXHAUST s=%d %-20s pos=? status=%d",
               d.config.slave_id, d.config.room.c_str(), d.status_code);
  }

  // Log zone sensors CO2 at this moment (helps correlate CO2 → fan response)
  for (auto &kv : zone_devices_) {
    auto &z = kv.second;
    if (z.co2 >= 0)
      ESP_LOGI(TAG, "  ZONE   s=%d %-20s co2=%.0fppm temp=%.1f°C rh=%.0f%%",
               z.slave_id, z.room.c_str(), z.co2, z.temp_room, z.humidity);
  }

  // Log ALFA16 internal reg102 and reg50-66 snapshot from reg_snapshots_
  // This will show if reg50-66 correlate with setpoint changes
  ESP_LOGI(TAG, "  ALFA16 reg102=%d reg49=%d r256=%d",
           (int)get_snapshot_value(16, 102),
           (int)get_snapshot_value(16, 49),
           system_.last_r256);
  bool any_nonzero = false;
  for (uint16_t r = 50; r <= 66; r++) {
    int v = (int)get_snapshot_value(16, r);
    if (v != 0) {
      ESP_LOGI(TAG, "  ALFA16 r%d=%d (NON-ZERO!)", r, v);
      any_nonzero = true;
    }
  }
  if (!any_nonzero)
    ESP_LOGI(TAG, "  ALFA16 regs50-66 all zero");

  ESP_LOGI(TAG, "=== END CORRELATION ===");
}

int FuturaBusComponent::get_snapshot_value(uint8_t slave_id, uint16_t reg) {
  uint32_t key = ((uint32_t)slave_id << 16) | reg;
  auto it = reg_snapshots_.find(key);
  if (it == reg_snapshots_.end()) return -1;
  return (int)it->second.last_value;
}


void FuturaBusComponent::log_diagnostics() {
  uint32_t now = millis();
  if (now - last_diag_time_ < 10000) return;
  last_diag_time_ = now;
  if (bytes_received_ == 0) {
    ESP_LOGW(TAG, "DIAG: NO DATA — check GPIO32→Waveshare TXD, 3V3, GND, A+/B-");
  } else {
    uint32_t pct = frames_total_ ? (frames_valid_crc_ * 100 / frames_total_) : 0;
    ESP_LOGI(TAG, "DIAG: bytes=%u frames=%u valid=%u(%u%%) discarded=%u slaves=%d "
             "fan=%d%% press_sensors=%d",
             bytes_received_, frames_total_, frames_valid_crc_, pct,
             buffers_discarded_, (int)seen_slaves_.size(),
             system_.fan_airflow, (int)pressure_sensors_.size());
    if (frames_total_ > 0 && frames_valid_crc_ == 0)
      ESP_LOGW(TAG, "  All frames bad CRC — try swapping A+/B- wires");
  }
  if (frames_sensor_) frames_sensor_->publish_state((float)frames_total_);
  if (valid_sensor_)  valid_sensor_->publish_state((float)frames_valid_crc_);
  if (bytes_sensor_)  bytes_sensor_->publish_state((float)bytes_received_);
}

}  // namespace futura_bus
}  // namespace esphome
