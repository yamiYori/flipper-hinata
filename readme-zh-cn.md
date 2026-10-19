# HINATA USB Reader

[English](readme.md)

将 Flipper Zero 用作 HINATA 兼容 USB NFC 读卡器。

- 读取 Aime、BanaPass、受支持的 Amusement IC 和 MIFARE Classic 卡。
- 选择 SD 卡中已保存的 FeliCa Lite-S `.nfc` 文件，通过 USB 模拟刷卡。
- 本项目与 HINATA 官方并无关系。

## 兼容性

- **实体卡**：支持官方 Aime、BanaPass，以及兼容的四字节 UID MIFARE Classic 卡。Classic 密钥由上位机提供。
- **保存卡**：支持手动放卡／移卡；已保存的 BanaPass 卡文件在已验证的私网环境中可与实体卡读入同一存档。
- **上位机**：兼容 Windows 原版 HINATA DLL 和 HINATA Go 网页端。
- **游戏**：1.0.0 在音击和中二节奏中使用正常。

## 下载与安装

请按 Flipper 上安装的固件选择对应文件：

| 固件 | 下载文件 |
| --- | --- |
| 官方固件 1.5.1 · API 88.2 | `hinata_usb-official-1.5.1.fap` |
| Unleashed unlshd-093 · API 88.9 | `hinata_usb-unleashed-093.fap` |

官方 1.5.1 是截至 2026-10-19 的最新稳定版。两种成品均面向 Flipper Zero F7；验证范围见[验证记录](docs/VALIDATION.md)。其他固件版本可能需要重新构建。

1. 从 Release 下载对应的 FAP；ZIP 同时包含两种成品、完整源码和许可证。
2. 将下载的文件重命名为 `hinata_usb.fap`，复制到 SD 卡 `/ext/apps/USB/`。
3. 打开 **Apps → USB → HINATA USB Reader**，连接 USB。
4. 每次只运行一个读卡客户端；保持 **Physical NFC、CardIO off**，将实体卡贴在 NFC 感应区域。

使用已保存的卡时，按下打开选卡界面，选择 `/ext/nfc` 中支持的文件。应用会自动切到 **Virtual Lite-S**，再短按 OK 放卡。加载后默认为未放卡状态。

| 按键 | 功能 |
| --- | --- |
| 上 | 切换实体／虚拟来源 |
| 下 | 打开已保存卡选择界面 |
| 短按 OK | 虚拟放卡／移卡 |
| 长按 OK | 切换 CardIO，默认关闭 |
| 右 | 开关诊断页 |
| 左 | 清除首次错误 |
| 返回 | 退出诊断页，或从主页面退出应用 |

诊断页显示 USB 收发、CFG、Drop、TO、命令和 RF 错误。应用运行期间无法使用 qFlipper RPC。

## 构建

Windows x64、Python 3.12：

```powershell
# 官方固件
python tools/bootstrap.py --firmware official
python tools/build.py --firmware official

# Unleashed 固件
python tools/bootstrap.py --firmware unleashed
python tools/build.py --firmware unleashed
```

成品分别位于 `dist/official/hinata_usb.fap` 和 `dist/unleashed/hinata_usb.fap`。

## 参与贡献

欢迎提交问题、兼容性结果、文档修正和代码改进。

1. 报告问题时附上应用版本、固件、上位机、复现步骤和诊断信息。
2. 修改代码时，从仓库创建分支，保持改动集中，并说明检查结果。
3. 提交 Pull Request；修改使用说明时请同步中英文 README。

完整流程和构建、检查、打包命令见[贡献指南](CONTRIBUTING-zh-cn.md)。

## 当前限制

以下是当前尚未实现、支持不完整或暂非主要目标的功能，欢迎通过 Issue 讨论或 Pull Request 完善。

- 尚未支持 NFC-V、Type B、设备固件更新或持久配置。
- 当前不支持持久写入实体卡或修改已保存的卡文件；虚拟卡认证需要有效的卡数据。
- CardIO 当前仅支持 FeliCa 身份识别。

## 许可证

采用 **GPL-3.0-or-later**，见 [LICENSE](LICENSE) 和[来源说明](NOTICE.md)。

[更新记录](CHANGELOG.md) · [发布说明](docs/RELEASE-1.0.0-zh-cn.md)
