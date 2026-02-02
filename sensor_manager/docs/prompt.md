# AI Prompt Strategy for STM32 Sensor Manager

## Overview

This document describes the AI prompt strategy and interaction approach used to develop the STM32 Sensor Manager firmware project for Path A of the technical assessment.

## Tool Used

**Claude Code** (Anthropic's CLI coding assistant) with Sonnet 4.5 model

## Development Approach

### Initial Project Analysis

First, I asked Claude to analyze the existing STM32CubeIDE project:

```
"anlisa proyek stm32cube ide ini"
```

This resulted in:
- Discovery of a bare STM32F407VGTx project with minimal code
- Identification of hardware configuration (Discovery board, 1MB Flash, 128KB RAM)
- Understanding of the .cproject and linker script configuration

### Task Specification

I provided the full technical assessment requirements via a document paste, which specified:

1. **Path A Requirements**:
   - STM32 with BME280 (I2C), GPS (UART), Fuel Sensor (4-20mA ADC)
   - State Machine (INIT, IDLE, READ, TRANSMIT, ERROR)
   - STOP mode power management (30s timeout)
   - I2C timeouts and GPS checksum validation

2. **Deliverables**:
   - Source code with documentation
   - README.md with architecture diagram and pinout
   - ARCHITECTURE.md with detailed design
   - prepare-firmware.py helper script
   - Meaningful git commits

### Clarification Questions

Before implementation, Claude asked strategic questions:

| Question | Options Chosen | Impact |
|----------|----------------|--------|
| Data output method | UART transmit + internal log | Added JSON formatting |
| Measurement interval | 5 seconds | Configured in state machine |
| LED indicators | All 4 LEDs | Added LED control logic |

### Implementation Strategy

The development followed this sequence:

1. **Planning Phase** (Plan Mode)
   - Created comprehensive implementation plan
   - Designed state machine flow
   - Specified hardware pinout
   - Defined memory layout

2. **Core Implementation**
   - Created folder structure
   - Implemented configuration header
   - Developed sensor drivers (BME280, GPS, Fuel)
   - Built state machine
   - Added power management

3. **Integration**
   - Updated main.c with HAL initialization
   - Connected all modules
   - Added interrupt handlers

4. **Documentation**
   - README.md with diagrams
   - ARCHITECTURE.md with detailed design
   - This prompt strategy document

5. **Build Support**
   - prepare-firmware.py for binary preparation
   - .gitignore for clean version control

## Key Design Decisions Made with AI Assistance

### 1. BME280 I2C Driver

**Prompt consideration**: How to handle I2C timeouts?

**Decision**: Use HAL's built-in timeout parameter with 100ms limit, implementing retry logic at state machine level rather than driver level for cleaner separation of concerns.

### 2. GPS Parsing

**Prompt consideration**: Full NMEA parser or $GPRMC only?

**Decision**: Focus on $GPRMC as specified, but structure the code to be extensible for other sentence types. Use XOR checksum validation for robustness.

### 3. State Machine Architecture

**Prompt consideration**: Table-driven or switch-case?

**Decision**: Function pointer table for clean extensibility:
```c
static const state_func_t state_table[STATE_COUNT] = {
    state_init_handler,
    state_idle_handler,
    state_read_handler,
    state_transmit_handler,
    state_error_handler
};
```

### 4. Power Management

**Prompt consideration**: When to enter STOP mode?

**Decision**: Use dual criteria - both GPS timeout AND overall activity timeout, with RTC wakeup for periodic checks and UART/button for manual wakeup.

### 5. Error Handling

**Prompt consideration**: How aggressive should error recovery be?

**Decision**: Cumulative error count (threshold: 5) before entering ERROR state, with automatic retry. This handles transient failures without getting stuck.

## Prompt Patterns Used

### Pattern 1: Code Generation with Context

```
"Implement [specific feature] for [hardware]
 - Use [specific protocol/API]
 - Handle [error cases]
 - Follow [coding standard]"
```

**Example**: "Implement BME280 I2C driver with timeout handling, calibration data reading, and temperature/pressure/humidity compensation using datasheet formulas."

### Pattern 2: Architecture Review

```
"Analyze the [component] and suggest improvements for:
- Error handling
- Resource usage
- Maintainability"
```

### Pattern 3: Documentation Generation

```
"Create [document type] explaining:
- Architecture decisions
- Hardware integration
- Usage examples"
```

## What Worked Well

1. **Sequential Development**: Building from low-level drivers to high-level state machine prevented integration issues.

2. **Early Questioning**: Asking about LED behavior, data output, and intervals before coding prevented rework.

3. **Plan Mode**: Spending time in planning phase resulted in cleaner architecture with fewer changes later.

4. **Incremental Todo Tracking**: Using TodoWrite tool kept development organized and provided progress visibility.

## Challenges and Solutions

| Challenge | Solution |
|-----------|----------|
| STM32 HAL dependency | Used external declarations for handles (defined in main.c) |
| GPS parsing complexity | Broke down into helper functions for each field |
| State machine transitions | Used explicit next_state tracking for clarity |
| Power recovery | Documented context save/restore strategy |
| Binary preparation | Created Python script with CRC validation |

## Verification Strategy

The code was designed to be verifiable through:

1. **Unit testing** (not implemented but designed for):
   - Each driver can be tested independently
   - Mock HAL functions for I2C/UART/ADC

2. **Hardware in loop**:
   - LED status indicates system state
   - UART output provides real-time data visibility
   - Error LED catches failures early

3. **Code review checkpoints**:
   - I2C timeout handling
   - GPS checksum validation
   - STOP mode entry/exit
   - State machine integrity

## Future AI Collaboration Opportunities

For extending this project:

1. **FreeRTOS Integration**: "Migrate the state machine to FreeRTOS tasks, maintaining the same state transitions but using queues for inter-task communication."

2. **OTA Bootloader**: "Design a dual-bank bootloader with CRC verification of new firmware before switching banks, with automatic rollback on crash detection."

3. **Sensor Fusion**: "Implement a Kalman filter to combine GPS and IMU (if added) data for improved position estimation."

4. **Data Logging**: "Add FAT filesystem support for logging sensor data to SD card with circular buffer management."

## Lessons Learned

1. **Be Specific About Hardware**: Providing exact pin numbers and peripheral names in prompts reduces ambiguity.

2. **Think About Error Cases Early**: Asking about timeout handling and error recovery upfront produces more robust code.

3. **Document as You Code**: Generating documentation alongside implementation ensures nothing is forgotten.

4. **Use Version Control**: Even during development, meaningful commits help track progress and enable rollback.

5. **Leverage Plan Mode**: Spending time in planning phase saves significant time in implementation phase.

## Conclusion

This project demonstrates effective human-AI collaboration for embedded systems development. The AI served as:

- **Code Generator**: Producing boilerplate and driver code
- **Architect**: Suggesting design patterns and approaches
- **Documenter**: Creating comprehensive documentation
- **Reviewer**: Catching potential issues through questioning

The result is a production-grade firmware foundation that can be extended and deployed to real hardware.

---

**Generated with Claude Code (Sonnet 4.5)**
**Date**: 2026-02-02
**Project Duration**: Single development session
**Total Lines of Code**: ~2500 lines
