# AGENTS.md

## Project overview
Firmware for a Raspberry Pi Pico 2 W / RP2350 controlling two DC motors through a TB6612FNG motor driver. A KY-023 joystick is connected to another RP2350 acting as the controller.

Priorities:
1. Correctness and hardware safety
2. Deterministic startup, runtime behavior, and shutdown
3. Minimal blast radius for changes
4. Reasonable RAM/flash usage
5. Maintainability

## Hardware
- Target board: Raspberry Pi Pico 2 W / RP2350
- Motor driver: TB6612FNG
- Joystick: KY-023

## Core rules
- Make the smallest correct change.
- Do not refactor unrelated code.
- Preserve pin assignments, PWM slice/channel choices, protocol formats, task timing, clocking, watchdog, boot flow, flash layout unless explicitly required otherwise.
- Do not add dependencies unless explicitly requested 
- Follow existing repository conventions first.
- Prefer simple embedded-friendly C/C++.
- Use named constants for pins, timing values, and hardware constants. No magic values.
- Prefix class/struct members with `m_`.
- Prefix function parameters with `p_`.
- Do not prefix local variables.
- Don’t extract a function just to name a block.
  Extract when there is reuse, a real hardware/API boundary, a state-machine boundary, or a safety-critical operation that benefits from being isolated.
  For one-off parsing/control flow, keep it local and comment the steps if needed.
  Prefer fewer moving parts unless the current function is genuinely becoming hard to reason about.
- Call out obvious mistakes and suggest a fix, dont write code to work around them.

## Build and validation
- First inspect `CMakeLists.txt`, presets, and config headers.
- Use the dev CMake preset by default.
- Check release only if affected.
- Run the narrowest relevant checks first:
  1. affected target build
  2. lint/format for touched files if configured
  3. unit tests for touched modules if present
  4. full firmware build for non-trivial changes
- If hardware related logic changed, be explicit about needing bench validation.

## Motor safety: TB6612FNG
- Drive `STBY` to a known-safe state during boot.
- Before enabling `STBY`, set motor outputs to a non-driving safe condition.
- Avoid ambiguous direction transitions. Prefer:
  1. disable PWM or set duty to 0
  2. change direction pins
  3. re-enable PWM if needed
- Keep braking/coast behavior intentional and documented.
- Preserve failsafe behavior on lost command, timeout, or invalid input.
- Do not change PWM frequency casually.
- Any PWM, braking, ramping, retry, or full-duty behavior change must mention effects on:
  - current draw
  - audible noise
  - thermals
  - brownout/reset risk
  - radio/Wi-Fi stability
- Avoid changes that increase stall-current dwell time.
- Comment assumptions about inversion, braking mode, and duty-cycle scaling.

## Concurrency and timing
- Keep ISRs short.
- Avoid blocking calls in control loops, ISRs, timer callbacks, and timing-sensitive paths.
- Explicitly review:
  - IRQ/main-loop races
  - timer callback reentrancy
  - shared buffer access
  - atomicity of command/state updates
  - watchdog interaction
- Prefer monotonic timing and timeout-based state machines over long sleeps.
- Do not let Wi-Fi connection logic, network retries, or CYW43/LWIP work starve motor safety handling.

## Wi-Fi / Pico SDK
- Use Pico SDK CYW43 APIs for Wi-Fi and LWIP for TCP/UDP.
- Avoid unnecessary coupling between motor-control timing and network code.
- Boot must remain safe if Wi-Fi init fails or command source never appears.

## Initialization and shutdown
- Initialize motor-related outputs to a safe state before enabling the driver.
- Boot should be safe even if:
  - Wi-Fi init fails
  - motor driver is present but motor supply is unstable
  - command source never appears
- On error, shutdown, reboot, or crash-prone paths, prefer disabling drive output or entering a clearly defined safe mode.

## High-risk files
Treat edits to these as high risk:
- board/pin config
- PWM setup
- motor driver abstraction
- startup/init code
- watchdog/reset handling
- CYW43 / Wi-Fi integration
- persistent config / flash storage
- safety timeout/state-machine code

## Debugging order
Check common failure paths first:
1. wrong GPIO mapping or PWM slice/channel selection
2. `STBY` not asserted correctly
3. direction pins inverted or swapped
4. PWM duty or frequency out of expected range
5. initialization order problem at boot
6. timeout/failsafe logic disabling output
7. motor supply dip causing reset or instability
8. Wi-Fi/CYW43 work interfering with control timing

## Output expectations for non-trivial changes
Report:
- what changed
- likely root cause
- why this fix was chosen over alternatives
- RAM/flash/performance impact if non-trivial
- hardware risks or regressions to watch
- what was validated locally
- what still needs bench testing on Pico 2 W + TB6612FNG hardware
