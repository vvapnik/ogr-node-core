# AGENTS.md

Guidance for AI agents working in this repo.

Project map: `ARCHITECTURE.takesix.json` — read it first. It shows how the
node engine (`src/OgrNode.*`), the three HAL interfaces, the protocol
constants and the host tests fit together, with file paths for each part.

## What this is

Platform-independent protocol engine for an OpenGardenRack node (I2C
slave). The protocol itself is specified in
[open-garden-rack](https://github.com/vvapnik/open-garden-rack); this
library follows it. No hardware code lives here: HAL packages
(`ogr-node-hal-*`) implement `IOgrTransport`, `IOgrGpio` and `IOgrStorage`.

## Rules

- No dynamic allocation; keep RAM use within ATtiny85-class budgets.
- Nothing slow on the transport call path (it may be an ISR): storage
  writes and transport reconfiguration are deferred to `update()`.
- Cover protocol changes with host tests: `pio test -e native`.
- When files or responsibilities move, update `ARCHITECTURE.takesix.json`
  so its paths still match the code.

## AI assistance

This project is developed with the help of Claude Code (Anthropic).
