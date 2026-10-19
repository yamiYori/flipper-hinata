# 参与贡献

[English](CONTRIBUTING.md)

欢迎提交问题、兼容性结果、文档、翻译和代码改进。

## 报告问题

先搜索已有 Issue，再提供应用版本、固件／API、操作系统、上位机及版本、卡片大致类型、复现步骤、预期和实际表现。CFG、Drop、TO 等诊断数据有助于定位问题；请移除卡号、密钥和原始卡数据。

## 提交修改

1. Fork 仓库、克隆到本地，并为一个改动创建分支。
2. 按 [README](readme-zh-cn.md) 准备固定版本的构建工具，构建受影响的固件目标。
3. 保持修改集中；涉及操作或安装变化时，同步中英文 README。
4. 提交 Pull Request，说明解决的问题、修改后的行为和执行过的检查，并写明实际检查过的固件或硬件组合。

固定工具链使用 Windows x64 和 Python 3.12。代码修改应执行适用的本地检查：

```powershell
python tools/test.py --firmware unleashed
# 准备好官方 SDK 后，也可检查官方固件目标。
python tools/test.py --firmware official
```

涉及上位机兼容性的修改，还可执行公开客户端回归。另行获取 `sources-lock.json` 中指定版本的 HINATA Go，并使用 Dart 3.13.5：

```powershell
python tools/test_public_client.py --dart C:/path/to/dart.exe --source C:/path/to/hinata_go
```

自动检查使用合成数据。实机检查步骤见 [HARDWARE-TEST.md](docs/HARDWARE-TEST.md)。仅修改文档不需要实机测试。

## 本地打包

先构建两种固件目标，并完成本地检查和公开客户端回归，再运行：

```powershell
python tools/package.py --out release/1.0.0
```

跳过测试打包：

```powershell
python tools/package.py --without-tests --out release/1.0.0
```

输出目录必须为空或尚未存在。

## 范围与许可证

NFC、上位机兼容和 USB 修改应保持职责清晰。不支持的操作应明确报错，移卡后应清除旧卡状态。欢迎完善当前限制中列出的功能，请在 Pull Request 中说明新增行为、兼容性影响和验证方式。

请勿提交卡文件、账号数据、密钥、第三方 DLL、SDK 或游戏文件。贡献采用 GPL-3.0-or-later 许可。
