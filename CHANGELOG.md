# Changelog

The project follows [Semantic Versioning](https://semver.org/).

## [1.1.0] - 2026-11-14

### Added

- Persistent failed-attempt counter and changed PIN in MB85RC256V FRAM.
- Persistent alarm deadline calculated from the DS1307 RTC.
- Restoration of the remaining alarm duration after an ESP32 reboot.

### Changed

- Enabled persistent storage by default with
  `APP_PERSISTENT_STORAGE_ENABLED=1`.

## [1.0.0] - 2026-11-14

### Added

- Event-driven safe state machine: `LOCKED`, `ENTERING_CODE`, `OPEN`, `ALARM`.
- Encoder and button input, OLED output, NFC administrator reset.
- Relay and SG90 lock actuator control.
- Audio, status LEDs and access audit log.

### Limitations

- Security state and changed PIN are kept only in RAM.
- Rebooting the controller resets failed attempts and an active alarm.
