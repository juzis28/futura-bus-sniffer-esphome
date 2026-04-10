# Futura VarioBreeze — RS485 Register Map

Technical reference for the Modbus RTU registers observed on the Futura VarioBreeze RS485 bus.  
Compiled through passive sniffing and testing. See [README.md](README.md) for installation instructions.

## Entity naming

All entity IDs use **generic English** room names so the code is
internationally reusable:

| Entity suffix | Physical room (this install) |
|---------------|------------------------------|
| `living` | Living room (ALFA main panel) |
| `room1` | Bedroom |
| `room2` | Child room 1 |
| `room3` | Child room 2 |
| `room4` | Child room 3 |
| `office` | Office / study |

Rename in the dashboard YAML to your own room labels — entity IDs stay stable.

## Known register map

### Wall panel sensors (slaves 9-13, FC3)

| Reg | Meaning | Scale |
|-----|---------|-------|
| 91 | status: 1=plain, 3=has boost button | — |
| 92 | outdoor temp | ×0.01 °C |
| 93 | room temp | ×0.01 °C |
| 94 | humidity | ×0.01 %RH |
| 95 | CO₂ | 1 ppm (max 5000) |

### ALFA main panel (slave 16)

| Reg | FC | Meaning | Notes |
|-----|-----|---------|-------|
| 37 | FC4 | ALFA own CO₂ | ppm |
| 49 | FC4 | fan airflow step | 64=L1…320=L5/boost |
| 50-66 | FC4 | duct registers | always 0 in tests |
| 67 | FC4 | constant marker | 0xFC22, skip |
| 68-70 | FC4 | temp/humidity/CO₂ | ×0.1°C / %RH / ppm |
| 102 | FC3 | internal param | range ~23-44, unknown |
| 181 | FC3 | mode | 1=normal |
| 193 | FC3 | nominal airflow | 230 m³/h (constant) |
| 194 | FC3 | active zones | 6 (constant) |
| 198 | FC16 write | exhaust abs. setpoint | m³/h (NOT %) |
| 199 | FC16 write | supply abs. setpoint | m³/h (NOT %) |
| 252 | FC3 | unknown constant | =75, NOT filter wear |
| 253-254 | FC3 | calibration const | =1439 |
| 255 | FC3 | transition timer | countdown ~59→0 during ramp-up & circulation |
| 256 | FC16 write | ramp countdown | 59→0 over ~57s during fan transitions |
| 296 | FC16 write | boost start cmd | triggers exhaust opening |
| 14 | FC16 write | boost pacing | written between damper ops |

### Dampers (slaves 64-127)

| Reg | FC | Meaning |
|-----|-----|---------|
| 102 | FC16 write | target position % (Futura→damper) |
| 107 | FC4 | status: 0=starting·1=stable·4=moving |

### Boost controllers (slaves 32-34)

| Reg | Meaning |
|-----|---------|
| 105 | always 0, purpose unknown |
| 106 | boost button pulse (0→1→0 on press) |

## Zone map (this installation)

### Wall sensors
| Slave | Room | Boost button |
|-------|------|-------------|
| 9 | Bedroom | No |
| 10 | Child room 1 | No |
| 11 | Child room 2 | Yes |
| 12 | Child room 3 | Yes |
| 13 | Office | Yes |
| 16 | Living room (ALFA panel) | — |

### Supply dampers
| Slave | Room | Notes |
|-------|------|-------|
| 64 | Living room A | 1st supply point |
| 65 | Child room 1 | ✓ CO₂ test |
| 66 | Child room 2 | ✓ CO₂ test |
| 67 | Child room 3 | ✓ CO₂ test |
| 68 | Bedroom B | 2nd supply point |
| 69 | Office | ✓ confirmed |
| 72 | Living room B | 2nd supply point |
| 73 | Bedroom A | primary supply point |
| 80 | Living room C | 3rd supply point |

### Exhaust dampers

Exhaust only in: kitchen, WC, bathroom, hallway, utility room.
Bedrooms, office, and living room have supply only — no exhaust.

| Slave | Room | Confirmed |
|-------|------|-----------|
| 96 | Kitchen A | ✓ boost test |
| 97 | WC A | ✓ boost test |
| 98 | Bathroom A | ✓ boost test |
| 99 | Bedroom | ✓ DIP verified |
| 100 | Utility room | ✓ DIP verified |
| 101 | Hallway | ✓ DIP verified |
| 104 | Kitchen B | ✓ boost test |
| 105 | WC B | ✓ boost test |
| 106 | Bathroom B | ✓ boost test |

### Boost buttons
| Slave | Zone |
|-------|------|
| 32 | Kitchen |
| 33 | WC / Toilet |
| 34 | Bathroom |

## Confirmed findings

1. **r=255 = transition timer** — countdown ~59→0 during:
   - Fan speed ramp-up (auto→3, etc.) — synced with setpoint increases
   - Circulation mode — countdown of remaining seconds
   - Does NOT count during ramp-down (speed 3→auto)
2. **r=256 = ramp countdown** — written via FC16, observed 59→0 in earlier test
   - FC3 readback always 0; real values only from FC16 writes
3. **reg49 (fan_airflow)** pulses 64→320→64 during mode transitions and zone analyze
4. **reg194 (active_zones)** counts 1→6 during zone analyze sequences
   - Zone analyze triggers automatically after mode changes
5. **reg14** alternates 0x7023↔0x7021 during zone analyze pacing
6. **Modes visible on RS485**: only circulation (r=255 countdown)
   - Boost, overpressure, night, party — commands go via Modbus TCP only,
     RS485 bus shows only indirect effects (setpoint changes, zone analyze)

## What still needs investigation

1. ~~Exhaust slaves 99/100/101~~ — **RESOLVED**: 99=bedroom, 100=utility room, 101=hallway (DIP verified)
2. **ALFA16 regs 50-66** — always 0, even during boost. Purpose unknown.
3. **ALFA16 reg102** (~23-44) — internal Futura parameter, meaning unclear
4. **reg252=75** — constant, NOT filter wear (Modbus TCP shows 66% separately)
5. **r=256 vs r=255** — both show countdowns; need more tests to clarify
   when each is used (r=256 not seen in latest tests)

## Correlation log format

When fan setpoint changes, the code logs a `CORRELATION` snapshot:
```
=== CORRELATION [trigger=value] exhaust=X supply=Y ===
  SUPPLY s=64 ...   pos=35% status=1
  EXHAUST s=96 ...  pos=? status=1
  ZONE   s=9  ...   co2=550ppm temp=23.8°C rh=27%
  ALFA16 reg102=29 reg49=64
  ALFA16 regs50-66 all zero
=== END CORRELATION ===
```
Use 24h logs with natural occupancy to correlate damper positions with CO₂ levels.

## License

MIT. See [LICENSE](LICENSE).
