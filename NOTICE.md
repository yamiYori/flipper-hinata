# License and provenance

This project is licensed under GPL-3.0-or-later. See LICENSE.

The portable SEGA reader/frame/virtual-card core, NFC backend, virtual loader, virtual-card tests and build/bootstrap basis were developed from the maintainer's GPL-3.0-or-later Aime USB Reader sources. Its reference archive SHA256 is `6fc60c19729bb3f4819b3de92cfa1986d7d3a014da486d3cb0d279a7984bbdda`. The complete corresponding source is included in this repository. The HINATA adapter, composite USB transport, UI and additional tests are provided under the same license.

Public HINATA clients and documentation, aic_pico CardIO, Segatools, NXP documentation and Flipper firmware were consulted for protocol/API behavior. Build dependencies are pinned in build-lock.json and build-lock-official.json; the optional public-client test inputs are pinned in sources-lock.json. Third-party source required for the optional public-client test is obtained separately and hash-checked.

HINATA, Flipper, SEGA and other product names identify compatibility targets. USB identifiers are used for host compatibility.
