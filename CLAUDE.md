# CLAUDE.md

## Project
Dual RP2350 (Pico 2 W) firmware: one drives two DC motors via TB6612FNG, the other reads a KY-023 joystick. They communicate over Wi-Fi UDP.

**Priorities:** correctness and hardware safety > deterministic behavior > minimal blast radius > RAM/flash > maintainability.

## Claude code 
- Avoid using the Task tool/subagents unless a task requires parallel independent exploration. Prefer direct execution for single-file edits, debugging, and sequential work.

## Code conventions
- Member prefix: `m_`, parameter prefix: `p_`, locals: no prefix.
- Named constants for all pins, timing, and hardware values. No magic numbers.
- Extract functions only for: reuse, hardware/API boundaries, state-machine transitions, or safety-critical isolation. Not to name a block.
- Use `std::clamp`, `std::optional`, and other C++20 stdlib where appropriate — don't reimplement them.
- Follow existing style in the file being edited.

## Changes
- Make the smallest correct change. Don't refactor unrelated code.
- Don't add dependencies unless asked.
- Don't change: pin assignments, PWM config, protocol wire format, task timing, or boot flow — unless explicitly required.
- Flag obvious bugs; don't silently work around them.

## Build
- Default: `dev` CMake preset. Check release only if affected.
- Validation order: affected target → lint/format → unit tests → full build.
- If hardware logic changed, say bench validation is needed.

## Project specific
- `STBY` must be low (driver disabled) until motor outputs are in a safe state.
- Direction changes: zero PWM → change IN pins → re-enable PWM. Never slam through a reversal.
- Braking/coast must be intentional and commented.
- Any change to PWM frequency, duty, braking, or ramping must note effects on: current draw, thermals, audible noise, brownout risk, Wi-Fi stability.
- Failsafe (lost command, timeout, invalid input) must always stop or brake — never leave motors in an unknown drive state.
- ISRs must be short; no blocking calls in ISRs, timer callbacks, or control loops.
- Review on every shared-state change: IRQ/main-loop races, shared buffer access, command/state atomicity.
- Use monotonic timing and timeout state machines. Prefer `to_ms_since_boot` over `sleep_ms` in control paths.
- Wi-Fi/CYW43/lwIP work must not starve motor safety handling.
- Use CYW43 + lwIP APIs. Don't couple motor-control timing to network code.
- Boot must be safe if Wi-Fi init fails or the command source never connects.

## Debugging order
1. GPIO mapping or PWM slice/channel wrong
2. `STBY` not asserted at the right time
3. Direction pins inverted or swapped
4. PWM duty or frequency out of range
5. Init order problem
6. Timeout/failsafe logic disabling output unexpectedly
7. Motor supply dip causing reset
8. CYW43 work interfering with control timing
