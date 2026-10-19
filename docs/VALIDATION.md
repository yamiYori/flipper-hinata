# Compatibility checks

| Target | Status |
| --- | --- |
| Unleashed unlshd-093 / API 88.9 | Physical and saved-card reading, host connection and card removal checked. |
| Official firmware 1.5.1 / API 88.2 | Compiled against the official SDK; not tested on a device. |
| Windows HINATA DLL and HINATA Go | Card reading and removal work with the supported card types. |
| ONGEKI and CHUNITHM | Version 1.0.0 works in the checked setup. |
| Saved BanaPass file | Accesses the same save as the physical card in the checked private-network setup. |

CardIO, other operating systems and other firmware versions remain untested. Saved-card authentication requires valid card data.

Build manifests identify the SDK, source hashes and FAP checksum for each firmware target.

For your own checks, see [the hardware checklist](HARDWARE-TEST.md).
