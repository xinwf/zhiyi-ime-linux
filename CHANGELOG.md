# 更新日志

这里按版本记录面向用户的发布摘要。`.github/workflows/build-deb.yml` 会提取与标签
版本对应的章节，作为 GitHub Release 的“更新内容”。打标签前请先在本文件补上该
版本的摘要；缺失时工作流会退化为按提交列表生成。

## [0.2.0] - 2026-11-27

知意输入法的首个 Linux 正式版本。在 Fcitx 4 移植的基础上补齐 Fcitx 5 支持，
两个前端共用同一套引擎、词库与配置，并加入托盘设置、青简皮肤和横排候选窗。

### 新增

- **Fcitx 5 前端**：新增独立的 `InputMethodEngineV2` 适配层，按键流与 Fcitx 4
  完全一致；中英文切换遵循 fcitx5 的全局习惯（`AltTriggerKeys` /
  `Ctrl+Space`），配置变更通过 inotify 与 `ReloadAddonConfig` 即时生效。
- **双框架自动识别**：CMake 自动检测本机可用的 SDK，可以只编 Fcitx 4、只编
  Fcitx 5，或同时生成两套插件；.deb 的依赖随实际产物自动生成。
- **图形化托盘设置**：`zhiyi-tray` 登录后自启动，仅在知意激活时显示图标；
  菜单覆盖候选数、候选窗横竖排、双拼、模糊音、中文标点、皮肤与框架重启，
  改完立即热加载，无需重启输入法。
- **5 套青简皮肤**：绿、简白、深黑、橙、蓝，同时提供 Fcitx 4 classic-ui 皮肤
  与 Fcitx 5 classicui 主题。
- **候选窗横排布局**：可用 `zhiyi-config --set layout horizontal` 或托盘菜单切换。
- **Debian 打包与自动发布**：免 root 的 SDK 获取、`ctest` 回归测试、
  `dpkg-deb` 打包，以及 GitHub Actions 自动构建、测试并发布 Release；
  同时产出 Ubuntu 20.04 与 22.04 两个安装包。

### 修复

- 修复 Fcitx 5 下 `zhiyi-tray --setup` 并未真正禁用框架自带托盘的问题：
  覆盖文件现在会正确写入 `Enabled=False`，`--check` 可查看状态。
- 修复托盘菜单项被调用两次的问题：python-dbus 对 void 方法返回 `None`
  不再被误判为调用失败，中英切换、重启、重载配置均只执行一次。
- 修复 Fcitx 5 托盘图标与皮肤菜单不正确的问题。

### 已知限制

- Fcitx 5 的中英文切换由框架全局状态控制，Fcitx 4 则由知意自身处理轻按 Shift。
- 简拼索引来自上游词库字段，个别词条切分有误，简拼候选排在全拼候选之后。
- 安装包约 20 MB，主要来自二进制词典的压缩。
