# HINATA USB Reader 1.0.0

[Simplified Chinese](RELEASE-1.0.0-zh-cn.md)

The first public release turns a Flipper Zero into a HINATA-compatible USB NFC reader. It supports physical FeliCa and compatible MIFARE Classic cards, saved Lite-S files, present/remove controls and on-device diagnostics.

Choose the build matching your firmware:

- **Official 1.5.1 / API 88.2:** `hinata_usb-official-1.5.1.fap`
- **Unleashed unlshd-093 / API 88.9:** `hinata_usb-unleashed-093.fap`

Rename the chosen file to `hinata_usb.fap` and copy it to `/ext/apps/USB/`. Both builds target Flipper Zero F7. Use one host client at a time and leave CardIO off unless needed.

The ZIP includes both FAPs, complete source, documentation and the GPL-3.0-or-later license. Verify downloads with `SHA256SUMS`.

See the [README](../readme.md), [compatibility checks](VALIDATION.md) and [contribution guide](../CONTRIBUTING.md).
