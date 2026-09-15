# picorccar

RC car built on two RP2350 (Raspberry Pi Pico 2 W) boards, talking over a private Wi-Fi UDP link.

- **car** — drives two DC motors through a TB6612FNG dual H-bridge. Runs as the Wi-Fi access point, receives joystick commands over UDP, applies slew-limited differential drive, and fails safe (motors stopped) on command timeout or malformed input.
- **controller** — reads a KY-023 joystick, connects to the car's AP as a station, and streams commands with a session handshake (arm/disarm) so a stray or replayed packet can't drive the car.

## Hardware

| Car (TB6612FNG)      | Pico 2 W GPIO |
|-----------------------|---------------|
| Motor A `PWM`         | GPIO 2        |
| Motor A `IN2`         | GPIO 3        |
| Motor A `IN1`         | GPIO 4        |
| `STBY`                | GPIO 5        |
| Motor B `IN1`         | GPIO 6        |
| Motor B `IN2`         | GPIO 7        |
| Motor B `PWM`         | GPIO 8        |

| Controller (KY-023)   | Pico 2 W GPIO |
|-------------------------|---------------|
| Button (`SW`)          | GPIO 0        |
| X axis (`VRx`)         | GPIO 26 (ADC0) |
| Y axis (`VRy`)         | GPIO 27 (ADC1) |

Pin assignments are in [car/include/pinout.h](car/include/pinout.h) and [controller/include/pinout.h](controller/include/pinout.h).

## Prerequisites

- CMake ≥ 3.28, Ninja, an `arm-none-eabi` toolchain (whatever your `pico-sdk` checkout expects).
- [`pico-sdk`](https://github.com/raspberrypi/pico-sdk) ≥ 2.2.0, checked out at `~/pico-sdk` (or point `PICO_SDK_PATH` elsewhere).
- The SDK's bundled `cyw43-driver` submodule must include [georgerobotics/cyw43-driver#151](https://github.com/georgerobotics/cyw43-driver/pull/151) (`pr-151` branch). This corresponds to any cyw43 version that has commit `commit 889e4ccc892327c5b6c1a83552811d70d84ccba0`. You may need to manually update this submodule.
- `picotool`, checked out at `~/pico-tool` (or set `PICOTOOL_FETCH_FROM_GIT_PATH`), used by the SDK to build `.uf2`/`.elf2uf2` tooling.

## Build

Both firmwares are built from one top-level CMake configure — the AP SSID/PSK are randomly generated once per configure (see below) and shared by both subprojects, so build them from the same preset/build directory rather than configuring each separately.

```sh
# from the repo root
make            # builds both car and controller, dev preset
make car        # car firmware only
make controller # controller firmware only
```

This is equivalent to:

```sh
cmake --preset dev
cmake --build build/dev --target picorccar_car
cmake --build build/dev --target picorccar_controller
```

Outputs:

```
build/dev/car/picorccar_car.uf2
build/dev/controller/picorccar_controller.uf2
```

### Wi-Fi credentials

`PICORCCAR_WIFI_AP_SSID` / `PICORCCAR_WIFI_AP_PSK` are auto-generated random strings on first configure and cached in `CMakeCache.txt`, so a given build directory keeps using the same pair across rebuilds.

## Flash

Both boards enumerate as a USB mass-storage drive in BOOTSEL mode.

1. Hold the **BOOTSEL** button on the Pico 2 W while plugging it into USB (or while pressing reset if already plugged in).
2. It mounts as a drive (typically `RPI-RP2`). Copy the matching `.uf2` onto it:

   ```sh
   cp build/dev/car/picorccar_car.uf2 /media/$USER/RPI-RP2/
   cp build/dev/controller/picorccar_controller.uf2 /media/$USER/RPI-RP2/
   ```

   The board unmounts and reboots into the new firmware automatically.

Alternatively, `cmake --install` copies the `.uf2` for you once the board is already mounted:

```sh
make install-car        # installs to $PICO_MOUNT_PATH (default: /media/$USER/RP2350)
make install-controller
```

Adjust `PICO_MOUNT_PATH` if your board mounts under a different name/path.

## Running

1. Flash `picorccar_car.uf2` to the motor-driver board and power it — it brings up the Wi-Fi AP and waits for UDP commands on port 12345.
2. Flash `picorccar_controller.uf2` to the joystick board and power it — it connects to the car's AP, arms a session, and starts streaming joystick state.
3. Both boards log over USB serial (`pico_enable_stdio_usb`, UART disabled). Connect at any baud (USB CDC) to watch startup and runtime logs, e.g.:

   ```sh
   screen /dev/ttyACM0
   ```

   Which device node it lands on (`/dev/ttyACM0`, `/dev/ttyACM1`, ...) depends on the order the boards enumerate.

Static IPs: car AP at `192.168.4.1`, controller station at `192.168.4.2`, netmask `255.255.255.0` (see top-level [CMakeLists.txt](CMakeLists.txt)).

## Repo layout

```
car/            car firmware
controller/     controller firmware
include/        headers shared by both firmwares (protocol, logger, pico_common)
third_party/    vendored ulog
```
