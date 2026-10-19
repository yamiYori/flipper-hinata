# HINATA USB Reader

[Simplified Chinese](readme-zh-cn.md)

Use a Flipper Zero as a HINATA-compatible USB NFC reader.

- Read Aime, BanaPass, supported Amusement IC and MIFARE Classic cards.
- Select a saved FeliCa Lite-S `.nfc` file from the SD card and present it over USB.
- This project is not affiliated with HINATA.

## Compatibility

- **Physical cards:** official Aime, BanaPass and compatible four-byte-UID MIFARE Classic cards. Classic keys are supplied by the host.
- **Saved cards:** manual present/remove controls. A saved BanaPass file can access the same save as the physical card in the private-network setup that was checked.
- **Hosts:** compatible with the original Windows HINATA DLL and the HINATA Go web client.
- **Games:** version 1.0.0 works with ONGEKI and CHUNITHM in the tested setup.

## Download and install

Choose the file matching the firmware installed on your Flipper:

| Firmware | Download |
| --- | --- |
| Official 1.5.1 · API 88.2 | `hinata_usb-official-1.5.1.fap` |
| Unleashed unlshd-093 · API 88.9 | `hinata_usb-unleashed-093.fap` |

Official 1.5.1 is the latest stable release as of 2026-10-19. Both builds target Flipper Zero F7; see [validation](docs/VALIDATION.md) for coverage. Other firmware versions may require a rebuild.

1. Download the matching FAP from Releases. The ZIP includes both builds, complete source and the license.
2. Rename the downloaded file to `hinata_usb.fap` and copy it to `/ext/apps/USB/` on the SD card.
3. Open **Apps → USB → HINATA USB Reader** and connect USB.
4. Run one reader client at a time. Select **Physical NFC**, leave **CardIO off**, and place the card on the NFC antenna.

For a saved card, press Down to open the picker and select a supported file from `/ext/nfc`. The app switches to **Virtual Lite-S**; press OK to present the card. Files load in the removed state.

| Button | Action |
| --- | --- |
| Up | Switch physical / virtual source |
| Down | Open the saved-card picker |
| Short OK | Present / remove the virtual card |
| Long OK | Toggle CardIO; off by default |
| Right | Toggle diagnostics |
| Left | Clear the first-error record |
| Back | Leave diagnostics, or exit from the main screen |

Diagnostics show USB traffic, CFG, Drop, TO, command and RF errors. qFlipper RPC is unavailable while the app is running.

## Build

Windows x64 with Python 3.12:

```powershell
# Official firmware
python tools/bootstrap.py --firmware official
python tools/build.py --firmware official

# Unleashed firmware
python tools/bootstrap.py --firmware unleashed
python tools/build.py --firmware unleashed
```

Outputs are `dist/official/hinata_usb.fap` and `dist/unleashed/hinata_usb.fap`.

## Contributing

Bug reports, compatibility results, documentation fixes and code improvements are welcome.

1. Include the app version, firmware, host client, reproduction steps and diagnostics in bug reports.
2. Create a branch for code changes, keep the scope focused and describe your checks.
3. Open a pull request. Keep the English and Chinese READMEs in sync when changing user instructions.

See the [contribution guide](CONTRIBUTING.md) for the full workflow and build, check and packaging commands.

## Current limitations

The following features are not yet implemented, have partial support, or are outside the current main focus. Issues and pull requests to improve them are welcome.

- NFC-V, Type B, device firmware updates and persistent configuration are not yet supported.
- Persistent writes to physical cards or saved card files are currently unsupported. Virtual-card authentication requires valid card data.
- CardIO currently supports FeliCa identification only.

## License

Licensed under **GPL-3.0-or-later**; see [LICENSE](LICENSE) and [provenance](NOTICE.md).

[Changelog](CHANGELOG.md) · [Release notes](docs/RELEASE-1.0.0.md)
