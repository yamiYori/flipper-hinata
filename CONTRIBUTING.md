# Contributing

[Simplified Chinese](CONTRIBUTING-zh-cn.md)

Contributions can include bug reports, compatibility results, documentation, translations and code.

## Report a problem

Search existing issues first. Include the app version, firmware/API, OS, host client/version, broad card type, reproduction steps, expected behavior and actual behavior. Diagnostics such as CFG, Drop and TO are useful; remove card identifiers, keys and raw card data.

## Make a change

1. Fork the repository, clone your fork and create a branch for one change.
2. Follow the [README](readme.md) to install the pinned build tools. Build each affected firmware target.
3. Keep changes focused. Update both READMEs when behavior or installation changes.
4. Open a pull request describing the problem, the resulting behavior and checks performed. State which firmware or hardware combinations you actually checked.

Use Windows x64 with Python 3.12 for the pinned toolchain. Run applicable local checks for code changes:

```powershell
python tools/test.py --firmware unleashed
# Optional: also check the official SDK after bootstrapping it.
python tools/test.py --firmware official
```

Changes to host compatibility should also use the optional public-client regression. Obtain the HINATA Go revision in `sources-lock.json` separately and use Dart 3.13.5:

```powershell
python tools/test_public_client.py --dart C:/path/to/dart.exe --source C:/path/to/hinata_go
```

Tests use synthetic data. Manual checks are described in [HARDWARE-TEST.md](docs/HARDWARE-TEST.md). Documentation-only changes do not require device testing.

## Package locally

Build both firmware targets and complete the local and public-client checks, then run:

```powershell
python tools/package.py --out release/1.0.0
```

To package without running tests:

```powershell
python tools/package.py --without-tests --out release/1.0.0
```

The output directory must be empty or new.

## Scope and license

Keep NFC, host compatibility and USB changes focused. Report unsupported operations clearly and clear card data after removal. Contributions that address the current limitations are welcome. Describe the new behavior, compatibility impact and validation in your pull request.

Do not commit card dumps, account data, keys, third-party DLLs, SDKs or game files. Contributions are licensed under GPL-3.0-or-later.
