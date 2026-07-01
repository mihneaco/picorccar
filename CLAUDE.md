# CLAUDE.md

## Project
Dual RP2350 (Pico 2 W) firmware: one drives two DC motors via TB6612FNG, the other reads a KY-023 joystick. They communicate over Wi-Fi UDP.

**Priorities:** correctness and hardware safety > deterministic behavior > minimal blast radius > RAM/flash > maintainability.

## Code conventions
- Member prefix: `m_`, parameter prefix: `p_`, locals: no prefix.
- Named constants for all pins, timing, and hardware values. No magic numbers.
- Create new functions only for: reuse, hardware/API boundaries, state-machine transitions, or safety-critical isolation. Not to name a block.
- Use `std::clamp`, `std::optional`, and other C++20 stdlib where appropriate — don't reimplement them.
- Follow existing style in the file being edited.
- Use doxygen style for comments. Doc Comments bigger than 2 lines should be a javadoc block comment. Comments that have no doc value should stay as // comments. Multiline comments that have no doc value should use normal /* */ block comment syntax
- Prefer short concise comments inside cpp. Use longer comments inside headers to describe apis.

## Changes
- Make the smallest correct change. Don't refactor unrelated code.
- Challenge me when I suggest something that doesn't make sense. Only apply when I insist.
- Don't add dependencies unless asked.
- Don't change: pin assignments, PWM config, protocol wire format, task timing, or boot flow — unless explicitly required.
- Flag obvious bugs; don't silently work around them.
- Work with existing abstractions if possible. Don't write new ones when there are already available ones that can be adjusted.

## Build
- Default: `dev` CMake preset. Check release only if affected.
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
