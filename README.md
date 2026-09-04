# ogr-node-core

Platform-independent protocol engine for an [OpenGardenRack](https://github.com/vvapnik/open-garden-rack)
node (I2C slave): the register map, `EP_SELECT` endpoint paging, CRC-8/SMBUS
frame integrity, PLANT_UID persistence, and the EN_IN/EN_OUT enumeration
state machine — with **zero hardware dependencies**.

This library alone won't run on a real board. It talks to hardware only
through three small interfaces (`IOgrTransport`, `IOgrGpio`, `IOgrStorage`)
defined here; a HAL package supplies the implementations for a given MCU:

| Package | Targets |
|---|---|
| [ogr-node-hal-avr](https://github.com/vvapnik/ogr-node-hal-avr) | ATmega328 (TWI), ATtiny85 (USI) |
| ogr-node-hal-esp32 | ESP32 / ESP32-S3 |
| ogr-node-hal-ch32v003 | CH32V003 |

## Usage

```cpp
#include <OgrNode.h>

OgrNode node(ogr::ClassId::PlantContainer, /*isAnchor=*/true);

int moistureEp;

uint32_t readMoisture(void *) { return analogRead(A0); }

void setup() {
  moistureEp = node.addEndpoint(ogr::TypeId::Moisture, ogr::Mode::Read,
                                 ogr::Format::Raw16, readMoisture);

  // transport/gpio/storage come from a ogr-node-hal-* package.
  node.begin(transport, gpio, storage);
}

void loop() {
  node.update();
}
```

See [OgrNode.h](src/OgrNode.h) for the full API (restart/enumeration hooks,
PLANT_UID, status).

## Design notes

- **No dynamic allocation.** Endpoints live in a fixed-size array
  (`OGR_MAX_ENDPOINTS`, default 8, override with a build flag) sized for
  ATtiny85-class RAM budgets.
- **Callbacks, not cached values.** An endpoint's value is never stored in
  RAM — `onRead`/`onWrite` are called live from the data-window transaction,
  exactly matching the "push callbacks, not state" style the OGR-node
  library is meant to offer.
- **No per-byte ACK/NACK control needed.** Per the (revised) spec §5.3, a
  node always ACKs a write and reports a bad PEC via `STATUS` instead of an
  in-band NACK. That means any transport that can hand over one buffered
  received frame and fill one buffered response is enough — no HAL needs
  low-level bit-banged bus control just to satisfy this library.
- **Storage writes never happen on the transport's call path.** A PLANT_UID
  write stages into RAM immediately (fast) and is flushed to `IOgrStorage`
  from `update()` (called from `loop()`), since a flash/EEPROM write can
  take milliseconds — long enough to be a problem if it happened inside an
  I2C ISR.

## Testing

Protocol logic is covered by host-based unit tests (no hardware/toolchain
required) using PlatformIO's `native` platform + Unity, with mock
`IOgrTransport`/`IOgrGpio`/`IOgrStorage` implementations in `test/mocks/`:

```sh
pio test -e native
```
