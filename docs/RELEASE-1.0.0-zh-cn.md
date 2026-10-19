# HINATA USB Reader 1.0.0

[English](RELEASE-1.0.0.md)

首个公开版本，将 Flipper Zero 用作 HINATA 兼容 USB NFC 读卡器。支持实体 FeliCa、兼容的 MIFARE Classic、保存的 Lite-S 文件、放卡／移卡控制和设备诊断页。

按所用固件选择成品：

- **官方 1.5.1 / API 88.2：**`hinata_usb-official-1.5.1.fap`
- **Unleashed unlshd-093 / API 88.9：**`hinata_usb-unleashed-093.fap`

将选中的文件重命名为 `hinata_usb.fap`，复制到 `/ext/apps/USB/`。两种成品均适用于 Flipper Zero F7。每次只运行一个上位机客户端，按需开启 CardIO。

ZIP 包含两种 FAP、完整源码、文档和 GPL-3.0-or-later 许可证，可使用 `SHA256SUMS` 核对下载。

参见[使用说明](../readme-zh-cn.md)、[兼容性验证](VALIDATION.md)和[贡献指南](../CONTRIBUTING-zh-cn.md)。
