# ESP32 Multi-Sensor Data Logger

A real-time embedded data acquisition system built in C on ESP-IDF v6.0.2 and FreeRTOS. Drives a MAX30102 PPG heart rate sensor over I2C with an onboard signal processing pipeline for BPM extraction. A VL53L0X time-of-flight distance sensor is wired into the build (driver, C++ wrapper, CMake target) but is **not currently invoked from `app_main`** — see [Known Issues and Limitations](#known-issues-and-limitations).

Built as a practical exercise in bare-metal embedded systems: custom register-level drivers and C/C++ interoperability at the FFI boundary. Shared-bus arbitration and hard real-time DSP are targeted future work, not yet implemented — see below.

---

## Table of Contents

- [Project Overview](#project-overview)
- [Hardware Requirements](#hardware-requirements)
- [Wiring](#wiring)
- [Software Dependencies](#software-dependencies)
- [ESP-IDF Setup (Windows)](#esp-idf-setup-windows)
- [Build and Flash](#build-and-flash)
- [Architecture Overview](#architecture-overview)
- [Driver Implementation Details](#driver-implementation-details)
- [Signal Processing Pipeline](#signal-processing-pipeline)
- [Known Issues and Limitations](#known-issues-and-limitations)
- [Future Improvements](#future-improvements)

---

## Project Overview

This project demonstrates end-to-end sensor integration on the ESP32 — from hardware bring-up to real-time signal processing — without relying on Arduino or high-level abstraction libraries.

**What it does:**

- Samples the MAX30102 photodiode at roughly 100 Hz via a FreeRTOS software timer callback
- Runs the raw IR signal through a DC-removal stage, a low-pass IIR filter, and an adaptive peak detector
- Outputs instantaneous BPM estimates over UART at each detected heartbeat
- Includes a VL53L0X ToF driver (register-level init + read, wrapped for C linkage) that builds successfully but is currently dead code — its call site in `app_main` is commented out

**Why it exists:**

Most ESP32 sensor tutorials lean on pre-packaged Arduino libraries that hide the I2C register map, FIFO management, and interrupt logic. This project implements the MAX30102 driver directly from its datasheet to make that layer of the stack explicit, and integrates ST's official VL53L0X ranging API behind a C wrapper — useful for understanding what "driver code" actually does, and as a reference for integrating sensors in production firmware without external dependencies.

---

## Hardware Requirements

| Component | Notes |
|---|---|
| ESP32 DevKit (any variant) | Tested on ESP32-WROOM-32 |
| VL53L0X breakout board | 3.3V logic; most breakouts include onboard regulator |
| MAX30102 breakout board | 3.3V; includes onboard 1.8V LDO for the sensor core |
| Breadboard + jumper wires | |
| USB-A to Micro-USB cable | For flashing and UART output |

Both sensors run from 3.3V and tolerate 3.3V logic levels on SDA/SCL. No level shifting required for the ESP32.

---

## Wiring

All four pins (VCC, GND, SDA, SCL) are shared between both sensors on the same I2C bus. Their I2C addresses are distinct (0x29 and 0x57), so no address conflict occurs.

```
ESP32 Pin    Signal     VL53L0X Pin    MAX30102 Pin
---------    ------     -----------    ------------
3V3          VCC        VIN            VIN
GND          GND        GND            GND
GPIO23       SDA        SDA            SDA
GPIO22       SCL        SCL            SCL
```

> **Note:** The VL53L0X breakout's XSHUT pin is left floating (pulled high internally), keeping the sensor active. If you need to dynamically reassign I2C addresses to run multiple VL53L0X units, wire XSHUT to a GPIO and toggle it before `vl53l0x_init()`.

Pull-up resistors (4.7 kΩ to 3.3V) on SDA and SCL are required. Most breakout boards include them; if you are wiring bare sensors, add them externally.

---

## Software Dependencies

| Dependency | Version | Source |
|---|---|---|
| ESP-IDF | v6.0.2 | Espressif official |
| FreeRTOS | Bundled with ESP-IDF | — |
| VL53L0X vendor driver | Espressif component mirror | `components/vl53l0x/` |
| MAX30102 driver | Custom (this repo) | `main/pulse_sensor.c` |

The MAX30102 driver has **no external dependencies** — it is written entirely against the register map in the [MAX30102 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max30102.pdf) using ESP-IDF's legacy `driver/i2c.h` API (`i2c_param_config` / `i2c_driver_install` / `i2c_master_write_to_device` / `i2c_master_write_read_device`), not the newer handle-based `i2c_master_bus_handle_t` API.

The VL53L0X vendor driver is a C++ library. A thin `extern "C"` wrapper (`tof_wrapper.cpp`) exposes an opaque C API so the rest of the firmware stays in C.

---

## ESP-IDF Setup (Windows)

These steps use the ESP-IDF Windows offline installer, which bundles Python, CMake, Ninja, and the Xtensa toolchain.

**1. Download and install ESP-IDF v6.0.2**

Download the offline installer from the [Espressif releases page](https://dl.espressif.com/dl/esp-idf/). Run the installer and accept the default installation path (`C:\esp\v6.0.2\esp-idf`).

**2. Open the ESP-IDF Command Prompt**

Use the "ESP-IDF v6.0.2 CMD" shortcut installed to your Start Menu. This sets `IDF_PATH`, activates the Python virtual environment, and adds the toolchain to `PATH`.

**3. Verify the environment**

```
idf.py --version
```

Expected output: `ESP-IDF v6.0.2`

**4. Clone this repository**

```
git clone <repo-url> sensor-logger
cd sensor-logger
```

**5. Set the target chip**

```
idf.py set-target esp32
```

---

## Build and Flash

All commands run from the `sensor-logger/` project root inside the ESP-IDF Command Prompt.

**Build**

```
idf.py build
```

Build output goes to `build/`. The compiled firmware binary is `build/sensor-logger.bin`.

**Flash**

Connect the ESP32 via USB and identify the COM port (Device Manager → Ports). Then:

```
idf.py -p COM3 flash
```

Replace `COM3` with your actual port. If the flash stalls waiting for the bootloader, hold the BOOT button on the devkit while the tool attempts to connect, then release it.

**Monitor UART output**

```
idf.py -p COM3 monitor
```

Or combine flash and monitor in one step:

```
idf.py -p COM3 flash monitor
```

Exit the monitor with `Ctrl+]`.

**Full clean build**

```
idf.py fullclean && idf.py build
```

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        FreeRTOS Scheduler                        │
│                                                                   │
│   ┌─────────────────────────┐   ┌──────────────────────────┐    │
│   │   app_main task         │   │  ~100Hz software timer    │    │
│   │   (startup, I2C scan,   │   │  callback                 │    │
│   │    idle loop)           │   │  (MAX30102 sample + DSP)  │    │
│   └────────────┬────────────┘   └──────────────┬───────────┘    │
│                │                               │                  │
└────────────────┼───────────────────────────────┼─────────────────┘
                 │                               │
        ┌────────▼────────┐             ┌────────▼────────┐
        │  tof_wrapper.h  │             │  pulse_sensor.h  │
        │  (C API, .h)    │             │  (C API, .h)     │
        │  built, unused  │             │                  │
        └────────┬────────┘             └────────┬────────┘
                 │                               │
        ┌────────▼────────┐             ┌────────▼────────┐
        │ tof_wrapper.cpp │             │ pulse_sensor.c   │
        │  extern "C"     │             │  register-level  │
        │  C++ boundary   │             │  I2C driver      │
        └────────┬────────┘             └────────┬────────┘
                 │                               │
        ┌────────▼────────┐                      │
        │ VL53L0X vendor  │                      │
        │ C++ driver      │                      │
        │ (components/)   │                      │
        └────────┬────────┘                      │
                 │                               │
                 └──────────────┬────────────────┘
                                │
                    ┌───────────▼───────────┐
                    │  ESP-IDF legacy I2C   │
                    │  driver (I2C_NUM_0,   │
                    │  GPIO22/GPIO23)       │
                    └───────────────────────┘
```

Both drivers target the same I2C bus, but at runtime only the MAX30102 path is active — the ToF branch in `app_main` is commented out (see `main/sensor-logger.c`), so the two never actually contend for the bus today.

### C/C++ Interoperability Boundary

The VL53L0X vendor driver is written in C++ and uses classes and templates. The rest of this firmware is in C. Mixing compilation units at link time requires that any symbol the C code calls must be declared with C linkage.

`tof_wrapper.cpp` achieves this with `extern "C"` blocks:

```c
// tof_wrapper.h — included by C code
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

int tof_init(void);
int tof_read_mm(uint16_t *out_mm);

#ifdef __cplusplus
}
#endif
```

The `.cpp` file includes the C++ vendor headers freely, then implements the two functions under the `extern "C"` linkage spec. CMakeLists.txt compiles `tof_wrapper.cpp` with `g++` and the rest of the sources with `gcc`; the linker sees C-mangled names for the wrapper symbols and resolves them correctly.

### I2C Bus Sharing

Both drivers target `I2C_NUM_0` (initialized once in `app_main` via the legacy `driver/i2c.h` API), but at runtime only the MAX30102 driver is ever called — the ToF init/read call site is commented out. **There is currently no mutex or other arbitration around the bus.** If the ToF path were re-enabled while the sampling timer is running, its I2C transactions and the timer callback's transactions would have no serialization between them, risking interleaved start/stop conditions. Adding a `SemaphoreHandle_t` around each driver's transaction sequence is required before both sensors can safely run concurrently — see [Future Improvements](#future-improvements).

### FreeRTOS Timer-Based Sampling

The MAX30102 is sampled via `xTimerCreate`, a FreeRTOS **software** timer, with a nominal 10 ms period (`pdMS_TO_TICKS(10)`, ~100 Hz). `CONFIG_FREERTOS_HZ` in `sdkconfig` is set to 100, meaning the FreeRTOS tick itself is also 10 ms — the timer period equals the tick granularity, so actual callback firing is subject to tick-level jitter rather than the microsecond-resolution scheduling an `esp_timer`-based callback would give. Migrating to `esp_timer` is targeted future work.

The callback (`sampling_callback` in `main/sensor-logger.c`):
1. Reads one sample from the MAX30102 FIFO (`max30102_read`) — not a burst of all available samples
2. Runs the sample through the DSP pipeline
3. If a peak is detected, computes and logs BPM

The callback executes in the FreeRTOS timer service task context. Keep work inside the callback minimal and non-blocking.

---

## Driver Implementation Details

### VL53L0X (Time-of-Flight)

The vendor driver (`components/vl53l0x/`) handles the sensor's full single-ranging initialization sequence: SPAD calibration, reference calibration, and timing budget configuration. `main/tof_wrapper.cpp` wraps it behind a C API:

- `tof_init(void)` — calls the vendor driver's `i2cMasterInit()` then `init()`; returns `bool`
- `tof_read(uint16_t *range_mm)` — triggers a single ranging measurement via the vendor driver's `read()`; returns `bool`

Both functions build and link correctly, but **neither is currently called** — the call site in `app_main` (`main/sensor-logger.c`) is commented out, so this driver and the vendor calibration path it depends on never execute at runtime today. XSHUT is left floating (pulled high), matching the wiring notes above, since no GPIO toggling is implemented.

### MAX30102 (PPG / Heart Rate)

No vendor library is used. The driver directly addresses the MAX30102 register map over I2C.

**Initialization sequence** (`max30102_init` in `main/pulse_sensor.c`):

1. Reset the device (`REG_MODE_CONFIG`, bit 6)
2. Wait a fixed 100 ms for reset to complete (`vTaskDelay` — not polled)
3. Set LED1 (IR) drive current (`REG_LED1_PA` = `0x1F`, ≈6.2 mA)
4. Clear FIFO write pointer, overflow counter, and read pointer
5. Configure SpO2 config register (`REG_SPO2_CONFIG` = `0x47`)
6. Set mode config to **HR-only mode** (`REG_MODE_CONFIG` = `0x02`) — only the IR LED channel is active; this is not SpO2 mode, and only one LED register (`LED1_PA`) is ever written

**FIFO management:**

The MAX30102 FIFO holds up to 32 samples. `max30102_read` (in `main/pulse_sensor.c`) on each call:

```c
uint8_t available = (wr_reg - rd_reg + 32) % 32;
if (available == 0) return false;
// reads exactly one 3-byte FIFO word, regardless of how many are available
```

The write and read pointers are fetched in two separate I2C transactions (not a single burst), and only **one sample per call** is popped from the FIFO — `available` is computed but not used to drain multiple entries. If the timer callback runs slower than the sensor fills the FIFO, samples can back up toward overflow. A single 3-byte word is unpacked to 18 bits:

```c
uint32_t sample = ((data[0] & 0x03) << 16) | (data[1] << 8) | data[2];
```

Because the device is in HR-only mode, there is no Red channel to discard — only IR is sampled at all.

---

## Signal Processing Pipeline

The pipeline runs once per sample inside `sampling_callback` (`main/sensor-logger.c`). All processing uses `float` arithmetic — there is no fixed-point/integer implementation.

```
Raw IR sample (18-bit)
        │
        ▼
┌───────────────────┐
│  DC Offset Removal │  EMA: dc = α·dc + (1-α)·sample,  α = 0.99
│  (EMA filter)      │  signal = sample - dc
└────────┬──────────┘
         │
         ▼
┌───────────────────┐
│  Low-Pass IIR     │  y[n] = β·y[n-1] + (1-β)·x[n],  β = 0.85
│  (2.4 Hz cutoff)  │  Attenuates motion artifact and high-frequency noise
└────────┬──────────┘
         │
         ▼
┌───────────────────┐
│  Adaptive Peak    │  threshold = midpoint + γ·(range),  γ = 0.3
│  Detection        │  threshold = (max+min)/2 + 0.3·(max-min)
│                   │  Peak confirmed on rising edge crossing the threshold
└────────┬──────────┘
         │
         ▼
┌───────────────────┐
│  IBI → BPM        │  bpm = 60,000,000 / ibi_us
│  (sanity bounded) │  Valid range: 40–200 BPM
└───────────────────┘
```

**Filter coefficients and their meaning (as implemented in `main/sensor-logger.c`):**

| Symbol | Value | Role |
|---|---|---|
| α (EMA DC) | 0.99 | DC tracking time constant (`dc_mean = (dc_mean*99 + sample)/100`). Higher = slower DC adaptation, more stable baseline. |
| β (IIR LP) | 0.85 | Low-pass pole location (`filtered = filtered*0.85 + ac_signal*0.15`). Cutoff ≈ (1-β)·Fs/(2π) ≈ 2.4 Hz. Passes heart rate fundamentals (0.7–3.3 Hz) while rejecting motion artifacts above 2.4 Hz. |
| γ (threshold) | 0.30 | Adaptive threshold set at the midpoint between the running min/max of the filtered signal, offset by 30% of the min-max range. `signal_min`/`signal_max` are running extrema that are never decayed or reset, so the threshold adapts slowly to changing signal amplitude. |

**No refractory period is implemented.** There is no minimum inter-beat-interval gate on the rising-edge detection itself — the only guard against double-triggering is the BPM sanity bound below, applied after IBI is computed.

**BPM sanity bounds:** IBI values (computed from `esp_timer_get_time()`, in microseconds) outside 300,000–1,500,000 µs (40–200 BPM) are discarded and no BPM is printed for that beat. This eliminates spurious detections during sensor placement and motion.

---

## Known Issues and Limitations

**VL53L0X is built but not wired up at runtime**
`tof_init()`/`tof_read()` exist and compile, but the call site in `app_main` (`main/sensor-logger.c`) is commented out. Re-enabling it requires adding the bus arbitration described below.

**No I2C bus arbitration**
There is no mutex or other serialization around I2C access. This is currently harmless only because the ToF path is disabled — if it were re-enabled alongside the timer-driven MAX30102 sampling, the two would have no protection against interleaved transactions on the shared bus.

**Software-timer sampling instead of `esp_timer`**
Sampling uses `xTimerCreate` (a FreeRTOS software timer) with a 10 ms period, and `CONFIG_FREERTOS_HZ=100` makes the FreeRTOS tick itself 10 ms — the timer period equals the tick granularity, a plausible source of sampling jitter. `esp_timer` would give microsecond-resolution, jitter-free scheduling.

**No interrupt-driven sampling**
The MAX30102 has an INT pin that asserts when the FIFO almost-full threshold is reached. This project polls instead on a timer, which wastes CPU and risks FIFO overflow at higher sample rates — worsened by the fact that only one sample is drained from the FIFO per callback (see below).

**Single-sample FIFO reads**
`max30102_read()` computes how many samples are available in the FIFO but only pops one per call, regardless of that count. If the callback ever runs slower than the sensor fills the FIFO, unread samples accumulate toward overflow. A burst read of all available samples would fix this.

**No refractory period on peak detection**
The rising-edge threshold crossing has no minimum time gate before the IBI/BPM sanity check; a noisy signal near the threshold could register spurious edge crossings between real beats (though only crossings that produce an IBI within 300–1500 ms are ever printed).

**Fixed LED drive current**
The MAX30102 LED current is set once at init (`0x1F` on `LED1_PA`) and never adjusted. In practice, adequate perfusion varies by finger placement and skin pigmentation. An AGC loop that targets a mid-scale ADC value would improve reliability across users.

**Single-sample peak detection**
The peak detector operates on individual filtered samples without windowed amplitude tracking. Noisy waveforms can produce false positives or missed peaks. A template-matching or Pan-Tompkins-style detector would be more robust.

**No SpO2 computation**
The device is configured in HR-only mode (IR channel only) — no Red LED channel is enabled, so no Red/IR ratio can be computed. Full SpO2 support would require reconfiguring `REG_MODE_CONFIG` to SpO2 mode and adding a second LED drive register.

**Leftover bring-up code in `app_main`**
`app_main` runs a full I2C address scan (1–126) on every boot before initializing the MAX30102. This was useful during hardware bring-up but has no functional purpose in normal operation.

---

## Future Improvements

- **Re-enable the VL53L0X path** — uncomment the ToF call site in `app_main` and add an `i2c_mutex` (`SemaphoreHandle_t`) around each driver's transaction sequence before running both sensors concurrently
- **Switch to `esp_timer`** for the sampling callback to get microsecond-resolution, jitter-free scheduling independent of the FreeRTOS tick rate
- **Burst FIFO reads** — drain all `available` samples per callback instead of one, using the write/read pointer delta already computed in `max30102_read()`
- **Migrate to the `i2c_master` bus-handle API** — replace the legacy `driver/i2c.h` calls with ESP-IDF's newer handle-based I2C driver
- **INT-driven MAX30102 sampling** — wire the INT pin to a GPIO, register an ISR, and use a FreeRTOS queue to hand samples to a processing task
- **AGC for LED current** — target 50–70% ADC full-scale; adjust LED drive register each second
- **SpO2 estimation** — reconfigure to SpO2 mode with both LEDs active, then compute R = (AC_red/DC_red) / (AC_ir/DC_ir) and map to SpO2 via calibration curve
- **BLE notifications** — stream BPM and distance over BLE using the ESP-IDF NimBLE stack
- **MQTT / Wi-Fi logging** — publish samples to an MQTT broker for real-time dashboarding
- **Multiple VL53L0X sensors** — use XSHUT GPIO toggling to reassign I2C addresses at boot, enabling multi-zone distance sensing
- **SD card logging** — buffer samples to FAT32 on an SD card via SPI for post-hoc analysis
- **Pan-Tompkins peak detector** — replace the adaptive threshold with a proper ECG/PPG QRS-style detector for better SNR performance
