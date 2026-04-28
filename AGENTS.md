# AGENTS.md

## Project overview
This repository contains firmware for a Raspberry Pi Pico 2 W (RP2350) controlling 2 DC motors through a TB6612FNG motor driver.

Primary priorities:
1. Correctness and hardware safety
2. Deterministic behavior and clean startup/shutdown
3. Minimal blast radius for changes
4. Reasonable RAM/flash usage
5. Maintainability

## Hardware overview
- Target board is Raspberry Pi Pico 2 W.
- Motor driver is TB6612FNG.

## Core rules
- Make the smallest correct change.
- Do not refactor unrelated code.
- Preserve pin assignments, PWM slice/channel choices, protocol formats, and task timing unless the task explicitly requires changing them.
- Do not add new dependencies unless explicitly requested.
- Prefer the most likely/common failure path first when debugging.
- Code should assume electrically noisy conditions due to motor switching and possible brownout/reset risk.

## Build and validation
- First parse the build system from `CMakeLists.txt`, presets, and config headers.
- Run the dev CMake preset as a default. Only check the release preset if it was affected by the change.
- After changes, run the narrowest relevant checks first:
  1. build of affected target
  2. lint/format on touched files if configured
  3. unit tests for touched modules if present
  4. full firmware build if the change is non-trivial
- If the change affects Wi-Fi, motor timing, or hardware startup behavior, explicitly state what still requires bench validation.

## Pico 2 W specific guidance
- Use the Pico SDK CYW43 APIs for Wi-Fi and onboard LED behavior.
- Avoid unnecessary interaction between motor-control timing and Wi-Fi/network code.
- Keep interrupt handlers short.
- Avoid blocking calls in control loops, ISRs, or timing-sensitive callbacks.
- Be explicit about shared-state protection between IRQ context, main loop, timers, and network callbacks.
- Do not modify clocking, watchdog, boot flow, flash layout, or linker behavior unless required. State this fact when doing so.
- Mention wether certain features significantly increase flash/ RAM use

## TB6612FNG specific guidance
- STBY must be driven to a known-safe state during boot.
- On startup and reset, default motor outputs to a non-driving safe condition before enabling STBY.
- Never allow ambiguous direction-state transitions that could briefly drive both directions during switching.
- Prefer explicit state transitions:
  - disable PWM or set duty to 0
  - change direction pins
  - re-enable PWM if needed
- Keep braking/coast behavior intentional and documented.
- Respect TB6612FNG limits:
  - continuous and peak current margins matter
  - avoid code changes that can increase stall-current dwell time
- Keep PWM frequency in the intended configured range and do not change it casually.
- Comment any assumptions about inversion, braking mode, and duty-cycle scaling.

## Electrical and control constraints
- Assume motors inject noise into supply and ground.
- Be cautious with code that increases simultaneous switching, startup surge, or sustained full-duty operation.
- When changing PWM, startup ramps, braking, or retry logic, mention possible effects on:
  - current draw
  - audible noise
  - thermals
  - brownout/reset behavior
  - radio/Wi-Fi stability
- Prefer bounded acceleration/deceleration ramps over sudden full-scale changes unless the existing design intentionally uses step changes.
- Preserve failsafe behavior on lost command, timeout, or invalid input.

## GPIO and peripheral safety
- Do not reassign pins without checking the full board mapping and all peripheral users.
- Before changing PWM configuration, verify:
  - which GPIOs are mapped to PWM outputs
  - slice/channel sharing implications
  - whether another subsystem already depends on the same slice
- For SPI/I2C/UART additions, avoid pin conflicts with motor control and debug access.

## Concurrency and timing
- Explicitly review:
  - interrupt/main-loop races
  - timer callback reentrancy
  - shared buffer access
  - atomicity of command/state updates
  - watchdog interaction
- For control paths, prefer monotonic timing and timeout-based state machines over long sleeps.
- Do not put Wi-Fi connection logic or network retries in paths that can starve motor safety handling.

## Initialization and shutdown expectations
- Boot should be safe even if:
  - Wi-Fi init fails
  - motor driver is present but motor supply is unstable
  - command source never appears
- Initialize outputs to a safe state before enabling the driver.
- On error paths, prefer disabling drive output or entering a clearly defined safe mode.
- On shutdown/reboot/crash-prone paths, ensure motors do not keep driving unintentionally.

## Code style
- Follow existing repository conventions first.
- Prefer simple, embedded-friendly C/C++.
- Keep hardware-facing functions small and explicit.
- Separate policy from hardware access where practical:
  - low-level pin/PWM driver code
  - motor state machine / safety logic
  - command / comms / Wi-Fi layer
- Add comments for non-obvious hardware behavior, timing assumptions, and safety decisions.
- Use named constants for pins, timing values and other constants. Avoid magic numbers.
- Prefix class and struct data members with `m_`.
- Prefix function parameters with `p_`.
- Do not prefix local variables; use descriptive names.

## Debugging priorities
When debugging, check these common failure paths first:
1. wrong GPIO mapping or PWM slice/channel selection
2. STBY not asserted correctly
3. direction pins inverted or swapped
4. PWM duty or frequency out of expected range
5. initialization order problem at boot
6. timeout/failsafe logic disabling output
7. motor supply dip causing reset or unstable behavior
8. Wi-Fi/CYW43 work interfering with control timing

## Files that require extra caution
Treat edits to these as high risk:
- board/pin config
- PWM setup
- motor driver abstraction
- startup/init code
- watchdog/reset handling
- CYW43 / Wi-Fi integration
- persistent config / flash storage
- safety timeout/state-machine code

## Output expectations
For each non-trivial change, provide:
- what changed
- likely root cause
- why this fix was chosen over alternatives
- impact on RAM/flash/performance if non-trivial
- hardware risks/regressions to watch
- what was validated locally
- what still needs bench testing on Pico 2 W + TB6612FNG hardware
