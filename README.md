# ESP32 Multi-Sensor Data Logger

A real-time embedded data acquisition system built in C on ESP-IDF v6.0.2 and FreeRTOS. Integrates a VL53L0X time-of-flight distance sensor and a MAX30102 PPG heart rate sensor over a shared I2C bus, with an onboard signal processing pipeline for BPM extraction.

Built as a practical exercise in bare-metal embedded systems: custom register-level drivers, C/C++ interoperability at the FFI boundary, shared bus arbitration, and real-time DSP under hard timing constraints.

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

- Continuously polls a VL53L0X ToF sensor for millimeter-precision distance readings
- Samples the MAX30102 photodiode at 100 Hz via a FreeRTOS timer callback
- Runs the raw IR signal through a DC-removal stage, a low-pass IIR filter, and an adaptive peak detector
- Outputs instantaneous BPM estimates over UART at each detected heartbeat

**Why it exists:**

Most ESP32 sensor tutorials lean on pre-packaged Arduino libraries that hide the I2C register map, FIFO management, and interrupt logic. This project implements both drivers from their respective datasheets to make every layer of the stack explicit — useful for understanding what "driver code" actually does, and as a reference for integrating sensors in production firmware without external dependencies.

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

The MAX30102 driver has **no external dependencies** — it is written entirely against the register map in the [MAX30102 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max30102.pdf) using the ESP-IDF `i2c_master` driver.

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
│   │   app_main task         │   │  100Hz timer callback     │    │
│   │   (startup + ToF loop)  │   │  (MAX30102 sample + DSP)  │    │
│   └────────────┬────────────┘   └──────────────┬───────────┘    │
│                │                               │                  │
└────────────────┼───────────────────────────────┼─────────────────┘
                 │                               │
        ┌────────▼────────┐             ┌────────▼────────┐
        │  tof_wrapper.h  │             │  pulse_sensor.h  │
        │  (C API, .h)    │             │  (C API, .h)     │
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
                    │   ESP-IDF I2C master  │
                    │   (shared bus,        │
                    │    GPIO22/GPIO23)     │
                    └───────────────────────┘
```

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

Both sensors share the same ESP-IDF `i2c_master_bus_handle_t`. The bus handle is initialized once in `app_main` and passed to both driver init functions. Each driver holds its own `i2c_master_dev_handle_t` bound to its 7-bit address.

Because the FreeRTOS timer callback and the `app_main` task both issue I2C transactions, access is serialized with a mutex (`SemaphoreHandle_t i2c_mutex`). Each driver acquires the mutex before any transaction sequence and releases it on completion or error. This prevents interleaved start/stop conditions from corrupting in-flight transfers.

### FreeRTOS Timer-Based Sampling

The MAX30102 is sampled in an `esp_timer` periodic callback at 100 Hz (10 ms period). Using `esp_timer` rather than a FreeRTOS software timer gives microsecond-resolution scheduling and avoids jitter introduced by the FreeRTOS tick granularity.

The callback:
1. Acquires the I2C mutex
2. Reads the FIFO write pointer and overflow counter
3. Bursts the available samples from the FIFO
4. Releases the mutex
5. Runs each sample through the DSP pipeline
6. If a peak is detected, computes and logs BPM

The callback executes in the `esp_timer` task context, which runs at a high priority. Keep work inside the callback minimal and non-blocking.

---

## Driver Implementation Details

### VL53L0X (Time-of-Flight)

The vendor driver handles the sensor's full single-ranging initialization sequence: SPAD calibration, reference calibration, and timing budget configuration. The wrapper exposes two calls:

- `tof_init()` — runs the full initialization sequence, sets 33 ms timing budget
- `tof_read_mm(uint16_t *out_mm)` — triggers a single ranging measurement and blocks until the result register is populated (typically < 40 ms)

Return value is 0 on success, negative errno on failure. The caller in `app_main` logs the distance and loops with a `vTaskDelay`.

### MAX30102 (PPG / Heart Rate)

No vendor library is used. The driver directly addresses the MAX30102 register map over I2C.

**Initialization sequence:**

1. Reset the device (`REG_MODE_CONFIG`, bit 6)
2. Wait for reset to complete (poll bit 6 until clear)
3. Configure FIFO: sample averaging = 4, FIFO rollover enabled, FIFO almost-full threshold = 17
4. Set mode to SpO2 (both Red and IR LEDs active)
5. Set SpO2 ADC range = 4096 nA, sample rate = 100 SPS, LED pulse width = 411 µs (18-bit resolution)
6. Set LED drive currents (Red = 0x24, IR = 0x24, approximately 7.2 mA each)
7. Clear FIFO write/read pointers and overflow counter

**FIFO management:**

The MAX30102 FIFO holds up to 32 samples. Each sample contains one Red word and one IR word at 18-bit resolution, packed into 3 bytes each (6 bytes per sample). On each callback invocation:

```c
uint8_t wr_ptr, rd_ptr, overflow;
// read FIFO_WR_PTR, OVF_COUNTER, FIFO_RD_PTR in one burst
// num_available = (wr_ptr - rd_ptr) & 0x1F
```

Samples are burst-read in a single I2C transaction (up to 6 × 32 = 192 bytes). Each 3-byte word is unpacked to 18 bits:

```c
uint32_t sample = ((buf[0] & 0x03) << 16) | (buf[1] << 8) | buf[2];
```

Only the IR channel is used for pulse detection; the Red channel is read but currently discarded.

---

## Signal Processing Pipeline

The pipeline runs once per sample inside the 100 Hz timer callback. All processing is fixed-point arithmetic using 32-bit integers to avoid floating-point overhead in the ISR context.

```
Raw IR sample (18-bit)
        │
        ▼
┌───────────────────┐
│  DC Offset Removal │  EMA: dc = α·dc + (1-α)·sample,  α = 0.95
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
│  Adaptive Peak    │  threshold = γ·peak_value,  γ = 0.5
│  Detection        │  Peak confirmed if signal crosses threshold
│                   │  and minimum refractory period elapsed (300 ms)
└────────┬──────────┘
         │
         ▼
┌───────────────────┐
│  IBI → BPM        │  bpm = 60000 / ibi_ms
│  (sanity bounded) │  Valid range: 40–200 BPM
└───────────────────┘
```

**Filter coefficients and their meaning:**

| Symbol | Value | Role |
|---|---|---|
| α (EMA DC) | 0.95 | DC tracking time constant. Higher = slower DC adaptation, more stable baseline. At 100 Hz, τ ≈ 200 ms. |
| β (IIR LP) | 0.85 | Low-pass pole location. Cutoff ≈ (1-β)·Fs/(2π) ≈ 2.4 Hz. Passes heart rate fundamentals (0.7–3.3 Hz) while rejecting motion artifacts above 2.4 Hz. |
| γ (threshold) | 0.50 | Adaptive threshold fraction of the last detected peak amplitude. Resets upward on each confirmed peak. |

**Refractory period:** A minimum inter-beat interval of 300 ms (= 200 BPM ceiling) is enforced to prevent the peak detector from double-triggering on the same pulse wavefront.

**BPM sanity bounds:** IBI values outside 300 ms–1500 ms (40–200 BPM) are discarded. This eliminates spurious detections during sensor placement and motion.

---

## Known Issues and Limitations

**No interrupt-driven sampling**
The MAX30102 has an INT pin that asserts when the FIFO almost-full threshold is reached. This project polls instead, which wastes CPU in the timer callback and risks FIFO overflow at high sample rates. INT-driven sampling would reduce CPU load and eliminate the overflow risk.

**I2C mutex held across FIFO burst reads**
The mutex is held for the full duration of a 192-byte burst read (~1.5 ms at 400 kHz). This blocks the ToF sensor for the entire burst window. A double-buffered approach or shorter burst reads would reduce contention.

**Fixed LED drive current**
The MAX30102 LED current is set at init and never adjusted. In practice, adequate perfusion varies by finger placement and skin pigmentation. An AGC loop that targets a mid-scale ADC value would improve reliability across users.

**Single-sample peak detection**
The peak detector operates on individual filtered samples without windowed amplitude tracking. Noisy waveforms can produce false positives or missed peaks. A template-matching or Pan-Tompkins-style detector would be more robust.

**No SpO2 computation**
Both Red and IR channels are sampled, but only IR is used. Red/IR ratio for SpO2 estimation is not implemented.

**VL53L0X blocks app_main**
`tof_read_mm()` uses a blocking ranging call. If the sensor does not respond (disconnected, power issue), `app_main` stalls. The vendor driver should be wrapped with a timeout.

---

## Future Improvements

- **INT-driven MAX30102 sampling** — wire the INT pin to a GPIO, register an ISR, and use a FreeRTOS queue to hand samples to a processing task
- **AGC for LED current** — target 50–70% ADC full-scale; adjust LED drive register each second
- **SpO2 estimation** — compute R = (AC_red/DC_red) / (AC_ir/DC_ir) and map to SpO2 via calibration curve
- **BLE notifications** — stream BPM and distance over BLE using the ESP-IDF NimBLE stack
- **MQTT / Wi-Fi logging** — publish samples to an MQTT broker for real-time dashboarding
- **Multiple VL53L0X sensors** — use XSHUT GPIO toggling to reassign I2C addresses at boot, enabling multi-zone distance sensing
- **SD card logging** — buffer samples to FAT32 on an SD card via SPI for post-hoc analysis
- **Pan-Tompkins peak detector** — replace the adaptive threshold with a proper ECG/PPG QRS-style detector for better SNR performance
