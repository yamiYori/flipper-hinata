# Manual hardware checks

Use these steps to check compatibility with your setup.

1. Use firmware matching the release target. Connect only the Flipper reader and open the app. Run one host client at a time.
2. Select Physical NFC with CardIO off. Check a supported FeliCa card, then a supported four-byte-UID Classic card with your authorized host client.
3. For each card, confirm discovery/read, remove it at least 10 cm for 5 seconds, replace it for 5 seconds, and repeat. Confirm the host reports removal and clears the previous card's data.
4. In HINATA Go, check recognition and removal. A card-read error is distinct from a no-card result.
5. For virtual Lite-S, select a supported saved file, confirm it starts removed, and exercise OK present/remove.
6. Under ongoing host polling, open/scroll/cancel the file picker and return to diagnostics. Record CFG, Drop, TO and command/status codes, without card IDs or raw data.

Record app version, firmware/API, OS, client version, broad card type and exact behavior. Stop if the device hangs or the USB connection becomes unreliable. Game version and backend/save validation should be recorded separately from reader identity checks.
