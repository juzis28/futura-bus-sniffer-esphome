# Futura VarioBreeze — ESPHome (ESP32)

Passive RS485 bus sniffer for the **Jablotron Futura** ventilation unit with **VarioBreeze** dampers.  
Runs on **ESP32-WROOM-32D/E** with **ESPHome** — no Raspberry Pi, no Python, no Linux required.

Based on reverse-engineering work from [JOhugo6/jablotron-variobreeze](https://github.com/JOhugo6/jablotron-variobreeze).

---

## What it does

The ESP32 passively listens to the internal RS485 bus between the Futura controller and VarioBreeze dampers.  
It decodes Modbus RTU frames and extracts:

| Data | Source | Description |
|------|--------|-------------|
| **Target position** | FC16 / FC6 → register 102 | Damper opening percentage (0–100%), written by Futura when recalculating airflow |
| **Status code** | FC4 → register 107 | Damper status (observed values: 0, 1, 2, 4) |

Each damper appears in **Home Assistant** as an ESPHome sensor — no MQTT needed (though ESPHome supports MQTT too).

---

## Required files

```
futura_esphome.yaml                      ← configuration (YAML)
secrets.yaml                             ← WiFi / API keys (create your own)
components/futura_bus/__init__.py         ← ESPHome component schema
components/futura_bus/futura_bus.h        ← C++ header
components/futura_bus/futura_bus.cpp      ← C++ sniffer logic
components/futura_bus/sensor/__init__.py  ← sensor platform
```

All files are required — `futura_esphome.yaml` alone is not enough.  
The `components/` directory contains C++ code that ESPHome compiles into firmware.

---

## Hardware

### Bill of materials

| # | Component | Notes |
|---|-----------|-------|
| 1 | **ESP32-WROOM-32D** or **32E** dev board (e.g. ESP32-DevKitC) | Any ESP32 board with free UART pins. 32E is the newer revision. |
| 2 | **Waveshare TTL TO RS485 (B)** | Galvanically isolated, screw terminals, 120Ω termination switch |
| 3 | Jumper wires (Female↔Male) | 3 pcs: VCC, GND, TXD→RX |
| 4 | 5 V USB power supply for ESP32 | Any micro-USB or USB-C (depends on board) |

### Wiring diagram

```
ESP32 WROOM32                 Waveshare TTL TO RS485 (B)            Futura RS-485

3V3           ──────────────> VCC
GND           ──────────────> GND
GPIO32 (RX)   <────────────── TXD
                              RXD   DO NOT CONNECT

                              A+  ──────────────────────────────────> A
                              B-  ──────────────────────────────────> B
                              SGND ─── optional ────────────────────> GND / COM

                              120R switch = OFF
```

### Important rules

- This is a **passive sniffer** — the ESP32 never transmits. Only the RX pin is used.
- Waveshare **RXD must not be connected** — the module should not receive data from the ESP32.
- Termination **120R = OFF** — the sniffer is connected in parallel to the existing bus.
- The Waveshare TTL side is powered from **3.3 V** (ESP32 3V3 pin), **not** 5 V.
- The default pin is **GPIO32** — safe on all ESP32 variants including WROVER. Change `rx_pin` in the YAML if needed.

### Waveshare module → Futura

The Futura unit has **two RS485 ports**. One connects to the peripheral bus (VarioBreeze dampers, wall panels, ALFA panel), the other is typically unused or reserved.

**Connect the sniffer in parallel to the peripheral bus** — the same RS485 port where the dampers and wall panels are already connected. The sniffer taps into the existing bus passively.

- `A+` → Futura peripheral bus `A`
- `B-` → Futura peripheral bus `B`
- `SGND` → Futura `GND/COM` (optional — try without first)

If you see CRC errors — swap the `A+` and `B-` wires.

---

## Software installation

### Method 1: via ESPHome Dashboard (Home Assistant)

If you have Home Assistant with the ESPHome add-on:

1. Copy the entire contents of this repository to the HA config directory (e.g. `/config/esphome/`).
2. Open the ESPHome Dashboard inside HA.
3. Create `secrets.yaml` (see below).
4. Edit `futura_esphome.yaml` — adapt the damper map to your home.
5. Click **Install** → **Plug into this computer** (first time via USB).

### Method 2: via command line

#### Step 1: Install ESPHome

```bash
pip install esphome
```

#### Step 2: Clone the repository

```bash
git clone https://github.com/juzis28/futura-bus-sniffer-esphome.git
cd futura-bus-sniffer-esphome
```

#### Step 3: Create secrets.yaml

Create a file `secrets.yaml` next to `futura_esphome.yaml`:

```yaml
wifi_ssid: "YOUR_WIFI_SSID"
wifi_password: "YOUR_WIFI_PASSWORD"
api_key: "GENERATED_KEY"
ota_password: "OTA_PASSWORD"
fallback_password: "FALLBACK_PASSWORD"
```

Generate the API key:

```bash
python3 -c "import secrets, base64; print(base64.b64encode(secrets.token_bytes(32)).decode())"
```

#### Step 4: Edit the damper map

Open `futura_esphome.yaml` and edit the `dampers:` section to match your home's damper configuration.

For each damper you need to know the `slave_id`, which is determined by the DIP switches on the damper:

```
slave_id = 64 + DIP1×1 + DIP2×2 + DIP3×4 + DIP4×8 + DIP5×16 + DIP6×32
```

**DIP → slave_id examples:**

| DIP1 | DIP2 | DIP3 | DIP4 | DIP5 | DIP6 | slave_id | Note |
|------|------|------|------|------|------|----------|------|
| OFF | OFF | OFF | OFF | OFF | OFF | 64 | Base supply address |
| ON | OFF | OFF | OFF | OFF | OFF | 65 | DIP1 = +1 |
| OFF | ON | OFF | OFF | OFF | OFF | 66 | DIP2 = +2 |
| ON | ON | OFF | OFF | OFF | OFF | 67 | DIP1+DIP2 = +3 |
| OFF | OFF | OFF | ON | OFF | OFF | 72 | DIP4 = +8 |
| OFF | OFF | OFF | OFF | ON | OFF | 80 | DIP5 = +16 |
| OFF | OFF | OFF | OFF | OFF | ON | 96 | DIP6 = +32 (base exhaust address) |
| OFF | ON | OFF | ON | OFF | ON | 106 | DIP2+DIP4+DIP6 = +42 |

Also edit the `sensor:` section — change the names to match your rooms.

#### Step 5: Compile and flash

First time — via USB cable:

```bash
esphome run futura_esphome.yaml
```

Subsequent updates — over the air (OTA):

```bash
esphome run futura_esphome.yaml --device futura-bus.local
```

#### Step 6: Add to Home Assistant

The device will appear automatically: **Settings → Devices & Services → ESPHome**.

Each damper will have two sensors:
- **Position** — target damper position in percent (0–100%)
- **Status** — status code (0, 1, 2, 4)

---

## YAML configuration structure

### futura_bus (component)

```yaml
futura_bus:
  id: futura
  uart_id: rs485_uart
  frame_gap_ms: 3.0            # inter-frame gap (ms)
  dampers:
    - slave_id: 64
      room: "Living room"      # room name (for logs)
      zone: 1                  # zone number in Futura
      damper_type: privod      # "privod" (supply) or "odtah" (exhaust)
      damper_index: 1          # index if zone has multiple dampers
```

### sensor (platform: futura_bus)

```yaml
sensor:
  - platform: futura_bus
    futura_bus_id: futura
    slave_id: 64
    sensor_type: position      # "position" or "status"
    name: "Living room supply 1 position"
    unit_of_measurement: "%"
    icon: "mdi:valve"
```

| Parameter | Required | Description |
|-----------|----------|-------------|
| `futura_bus_id` | yes | `futura_bus` component ID |
| `slave_id` | yes | Damper Modbus address (1–247) |
| `sensor_type` | yes | `position` (register 102, position %) or `status` (register 107, status code) |
| `name` | yes | Name shown in the Home Assistant dashboard |

---

## Protocol details

Futura communicates with VarioBreeze dampers over RS485 using the **Modbus RTU** protocol: **19200 baud, 8 data bits, no parity, 1 stop bit (8N1)**.

| Operation | FC | Register | Direction | Value |
|-----------|-----|----------|-----------|-------|
| Read damper status | FC4 | 107 | Futura → damper → Futura | Status code (0, 1, 2, 4) |
| Write target position | FC16 | 102 | Futura → damper | Opening percentage (0–100) |
| Write target position | FC6 | 102 | Futura → damper | Alternative single-register write |

**The sniffer never transmits** — it only listens. The ESP32 TX pin must be left disconnected.

### Data flow

1. Futura periodically sends **FC4** requests to each damper — reading register 107 (status).
2. When Futura recalculates airflow (e.g. due to a CO₂ level change in a zone), it sends **FC16** to register 102 with the new target position.
3. The ESP32 sees both of these messages on the bus, decodes them, and publishes them as sensor values to Home Assistant.

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| **No frames visible** | Check wiring — the RX pin (GPIO32 by default) must be connected to Waveshare **TXD** (not RXD). Check that Waveshare VCC is getting 3.3V. |
| **Frames visible but CRC errors** | Swap A+ and B- wires. Try a different baud rate: start with 19200, then try 9600. |
| **Frames OK but no damper data** | Check that the `slave_id` values match the DIP switch settings on the dampers. |
| **WiFi unstable** | If the ESP32 is inside a metal enclosure — use a U.FL variant with an external antenna, or mount the ESP32 outside the enclosure. |
| **ESP32 won't connect to WiFi** | Check `secrets.yaml` — SSID and password must be exact. ESP32 only supports 2.4 GHz WiFi (not 5 GHz). |
| **Home Assistant doesn't see the device** | **Settings → Devices & Services → + Add Integration → ESPHome**. Enter `futura-bus.local` or the ESP32 IP address. |

---

## ESP32-WROOM-32D vs 32E

| | WROOM-32D | WROOM-32E |
|---|---|---|
| Chip | ESP32-D0WD (rev 1) | ESP32-D0WDR2-V3 (rev 3) |
| Flash | External SPI, 4 MB | Embedded, 4 MB |
| GPIO32 | Free | Free |
| Compatibility | Full | Full |
| Recommendation | Works fine | **Recommended** (newer revision) |

Both modules work identically. The 32E is a newer revision with hardware bug fixes.

**Note on WROVER:** ESP32-WROVER (with PSRAM) uses GPIO16/17 for PSRAM. The default `rx_pin: GPIO32` avoids this — WROVER works without changes.

---

## Firmware resource usage

Compiled with ESPHome 2026.3, 15 dampers, 30 sensors:

| Resource | Used | Total | Percentage |
|----------|------|-------|------------|
| Flash | 925 KB | 1835 KB | 50.4% |
| RAM | 36.5 KB | 328 KB | 11.1% |
| CPU load | < 1% | — | — |

Plenty of room left for additional components (web server, bluetooth proxy, etc.).

---

## License

MIT License. See [LICENSE](LICENSE).  
Based on protocol research from [JOhugo6/jablotron-variobreeze](https://github.com/JOhugo6/jablotron-variobreeze).
