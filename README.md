# 知意输入法 · Linux 版（Fcitx 4 / Fcitx 5）

Windows 版 [知意输入法](https://github.com/dd1000001000/zhiyi_ime) 的 Linux 移植，
前端为 **Fcitx 4 与 Fcitx 5 双输入法插件**，最终产物是一个可直接 `dpkg -i`
安装的 `.deb`。

## 双框架自动识别

同一个源码树、同一个引擎（`src/core/`），为两个框架各提供一层薄适配：

| 前端 | 源码 | 产物 | 安装位置 |
|---|---|---|---|
| Fcitx 4 | `src/fcitx/zhiyi_addon.cpp` | `fcitx-zhiyi.so` | `/usr/lib/<triplet>/fcitx/` |
| Fcitx 5 | `src/fcitx5/zhiyi_addon.cpp` | `libzhiyi.so` | `/usr/lib/<triplet>/fcitx5/` |

构建时自动检测主机可用的框架 SDK（`ZHIYI_ENABLE_FCITX4` /
`ZHIYI_ENABLE_FCITX5`，默认 `AUTO`）：fcitx5 机器只编 fcitx5 插件，fcitx4
机器照旧只编 fcitx4 插件，两者都装了就都编（deb 的 `Depends` 按实际编出的
前端自动生成 `fcitx-bin | fcitx5` 二选一）。两个前端喂给引擎的按键流完全
一致，行为、配置、词典、用户词库完全相同；`tools/zhiyi_fcitx5_test.cpp`
用 fcitx5 官方 testfrontend 在进程内回放按键断言提交串，与 fcitx4 的 ABI
契约测试一并跑在 `ctest` 里。

## 为什么最初是 Fcitx 4

目标机器（Ubuntu 20.04）默认且正在运行的输入法框架是 `fcitx 4.2.9.7`，
GTK/Qt/XIM 前端与 `im-config` 都已配置好；而源里的 fcitx5 是 2020 年的早期
git 快照。用 Fcitx 4 可以做到「装完 `fcitx -r` 就能用」，同时引擎与框架完全
解耦；现在 fcitx5 适配层补齐后，同一份代码在两种框架上都能用。

## 从 Windows 版复用了什么

| 资产 | 复用方式 |
|---|---|
| 146 万条拼音词库 (`pinyin.dict.db.zip`) | 构建期由 `scripts/build_dictionary.py` 转成 Linux 二进制索引 |
| 拼音/双拼方案 `schemas/*.schema.json` | 原样随包安装，运行时直接解析双拼映射 |
| 合法音节集合 (`spellings` 表) | 生成 `zhiyi.syllables`，供音节切分使用 |
| `default.json` / `themes.json` / `punctuation.json` / 英文词表 | 原样随包安装 |
| 二进制词典的设计（定宽 POD + mmap + 前缀/简拼索引） | 按 `docs/dictionary.md` 的思路重新实现为自描述格式 |
| 组句、简拼、模糊音、自学习、中文标点、双拼的产品行为 | 按上游文档与 schema 实现 |

Windows 版的 TSF/IMM 前端、命名管道 + IOCP、Direct2D 候选窗、Win32 设置程序、
NSIS 安装器、WinHTTP/BCrypt 更新、DirectML 推理在本移植中**不复用**：
它们分别由 Fcitx 插件、框架自带候选窗、`zhiyi-config`、deb 包替代。

## 架构

```
按键 (Fcitx KeySym)
   │  src/fcitx/zhiyi_addon.cpp     —— Fcitx 4 适配层
   │  src/fcitx5/zhiyi_addon.cpp    —— Fcitx 5 适配层（keyEvent → 引擎 → commit/preedit）
   ▼
src/core/engine.cpp     —— 会话状态、候选生成、组句、自学习、标点
   ├── dictionary.cpp   —— mmap 只读索引（全拼 / 首字母两套）
   ├── pinyin.cpp       —— 音节切分、模糊音变体、双拼转换
   └── config.cpp       —— XDG 配置 (nlohmann/json)
   ▲
data/                   —— 构建期从上游词库生成的二进制词典
```

`src/core/` 不依赖 Fcitx、X11、GLib，`tools/query.cpp` 可以脱离图形环境
完整回放按键序列，因此核心逻辑可以用 `ctest` 做回归测试；fcitx5 前端另有
进程内端到端测试（`fcitx5_end_to_end`）和全栈会话测试（见下文）。

## 功能

- 全拼、首字母简拼、模糊音（7 组）、5 种双拼方案
- 整词精确匹配、未输完的前缀补全、逐音节组句、部分选择（选单字后继续输入剩余拼音）
- 候选翻页、数字 / 空格选词、回车上屏原始编码、退格、Esc 清空
- 中文标点、全角、轻按 Shift 切换中英文
- 用户词频自学习（`$XDG_DATA_HOME/zhiyi/user.tsv`）
- 候选数、候选窗横排/竖排、中文标点、模糊音、双拼等可配置

未移植（Windows 版特有或依赖未公开组件）：Laya 上文推荐模型、离线翻译、
语音输入、任务栏菜单、自动更新、备份迁移 GUI、IMM 兼容层。

已知限制：

- 中英文切换在两个框架上的机制不同：
  - **Fcitx 4**：轻按 Shift 由知意自身处理（引擎内置英文子模式）。
  - **Fcitx 5**：遵循 fcitx5 全局惯例——轻按 Shift（全局 `AltTriggerKeys`，
    默认 `Shift_L`）或 `Ctrl+Space` 在核心层切换整个输入法的激活状态，与本机
    其它 fcitx5 输入法一致；知意插件内不再做英文子模式，配置里的
    `default_english` 仅对 fcitx4 生效。
- 简拼索引由上游 `syllable_ids` 字段生成，个别词条切分有误，因此简拼候选
  排在全拼候选之后；全拼与组句不受影响。
- 打包体积主要来自 46 MiB 二进制词典（约 72 万条主索引 + 72 万条简拼索引）。

## 构建（不需要 root）

环境要求：CMake ≥ 3.15、C++17 编译器、Python 3、`dpkg-deb`，以及 fcitx4 或
fcitx5 之一（开发头文件可由脚本免 root 下载解包到 `.deps/sysroot`，fcitx5 的
distro 开发包已装时直接使用系统包）：

```bash
bash scripts/fetch-sdk.sh     # 仅在首次构建时需要（下载 fcitx4/fcitx5 SDK）
bash scripts/build-deb.sh     # configure -> build -> ctest -> staging -> dpkg-deb
```

打包用 `cmake --install` 到 staging 目录再 `dpkg-deb -b -Zgzip -z1`：
CPack 默认的 xz 压缩在 46 MiB 二进制词典上会耗时过久，快速 gzip 只需几秒。

产物在 `dist/zhiyi-ime_<版本>_<架构>.deb`。单独编译/测试：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
(cd build && ctest --output-on-failure)
./build/zhiyi-query --data build/data --keys nihao
```

`zhiyi-query` 的按键转义：`\B` 退格、`\R` 回车、`\E` Esc、`\S` 空格、
`\N`/`\P` 翻页、`\T` 切换中英文；`--expect` / `--expect-commit` 用于断言。

强制只编某一端（模拟单框架机器）：

```bash
cmake -S . -B build-fc4 -DZHIYI_ENABLE_FCITX5=OFF   # fcitx4 机器
cmake -S . -B build-fc5 -DZHIYI_ENABLE_FCITX4=OFF   # fcitx5 机器
```

## 安装与使用

```bash
sudo dpkg -i dist/zhiyi-ime_*.deb
fcitx5 -r                   # 重启正在运行的 fcitx5；fcitx4 机器则 fcitx -r
```

用 `Ctrl+Space` 开启输入法，`Ctrl+Shift` 切换到「知意输入法」。
若系统默认框架不是 fcitx，先执行 `im-config -n fcitx` 并重新登录。

安装内容（以双前端包为例）：

| 路径 | 内容 |
|---|---|
| `/usr/lib/<triplet>/fcitx/fcitx-zhiyi.so` | Fcitx 4 输入法插件 |
| `/usr/lib/<triplet>/fcitx5/libzhiyi.so` | Fcitx 5 输入法插件 |
| `/usr/share/fcitx/addon/fcitx-zhiyi.conf` | fcitx4 插件描述 |
| `/usr/share/fcitx/inputmethod/zhiyi.conf` | fcitx4 输入法注册 |
| `/usr/share/fcitx5/addon/zhiyi.conf` | fcitx5 插件描述（OnDemand） |
| `/usr/share/fcitx5/inputmethod/zhiyi.conf` | fcitx5 输入法注册（Label=知） |
| `/usr/share/fcitx5/themes/zhiyi-*` | fcitx5 classic-ui 主题 ×5（移植自青简） |
| `/usr/share/fcitx/skin/zhiyi-qingjian*/` | fcitx4 经典皮肤 ×5（移植自青简） |
| `/usr/share/zhiyi/data/` | 词典、音节表、双拼方案、出厂配置 |
| `/usr/bin/zhiyi-config` | 配置工具（自动通知运行中的框架热加载） |

用户数据遵循 XDG：配置 `~/.config/zhiyi/config.json`（两框架共用），
学习记录 `~/.local/share/zhiyi/user.tsv`。卸载 deb 不会删除用户数据。

## 配置

```bash
zhiyi-config --show
zhiyi-config --set page_size 9
zhiyi-config --set fuzzy 5            # 位掩码: z_zh=1 c_ch=2 s_sh=4 ...
zhiyi-config --set shuangpin xiaohe   # 空字符串 = 全拼
zhiyi-config --set layout horizontal  # 候选横排显示（默认 vertical 竖排）
zhiyi-config --set chinese_punctuation 1
```

配置文件只由 `zhiyi-config` 或手工编辑修改，引擎不会回写（框架退出时
只保存用户词频 `user.tsv`）。`zhiyi-config` 写完会自动探测运行中的框架并
通知热加载：fcitx5 走 `Controller1.ReloadAddonConfig("zhiyi")`（插件另有
inotify 监视 `~/.config/zhiyi/` 兜底），fcitx4 走 `ReloadConfig`；均立即
生效，无需 `fcitx -r` / `fcitx5 -r`。

## 皮肤（移植自青简）

5 套皮肤同时提供了两个框架的格式，配色、字号、边距与青简
`crates/qingjian-render/themes/qingjian/theme.json` 默认主题逐字段一致：

| 主题 id | 外观 | fcitx4（classic-ui 皮肤） | fcitx5（classicui 主题） |
|---|---|---|---|
| `zhiyi-qingjian` | 知意青简绿：淡绿底、深绿字 | `/usr/share/fcitx/skin/` | `/usr/share/fcitx5/themes/` |
| `zhiyi-qingjian-light` | 知意简白：白底黑字 | 同上 | 同上 |
| `zhiyi-qingjian-dark` | 知意深黑：黑底白字 | 同上 | 同上 |
| `zhiyi-qingjian-orange` | 知意青简橙：暖橙底、深棕字 | 同上 | 同上 |
| `zhiyi-qingjian-blue` | 知意青简蓝：晴蓝底、深蓝字 | 同上 | 同上 |

- fcitx5：写 `~/.config/fcitx5/conf/classicui.conf` 的 `Theme=zhiyi-qingjian`，
  或用 `zhiyi-config`/托盘菜单切换（托盘会补发
  `ReloadAddonConfig("classicui")` 即时换肤）。
- fcitx4：`fcitx-config-gtk3` 的「外观 → 皮肤」里选择，或写
  `~/.config/fcitx/config` 的 `[Appearance] SkinName=zhiyi-qingjian`，
  改完 `fcitx -r` 生效。

fcitx5 主题由 `scripts/generate_fcitx5_themes.py` 从 fcitx4 皮肤转换生成后
提交在 `data/fcitx5-themes/`（背景图复用皮肤的 `input.png` 做 9-patch，
`Background/Margin` 即圆角边距）。皮肤 id 统一带 `zhiyi-` 前缀，与系统里
青简官方皮肤并存，互不覆盖。

## 托盘设置（图形界面）

`zhiyi-tray` 是一个 AppIndicator 托盘小程序（登录后经
`/etc/xdg/autostart/zhiyi-tray.desktop` 自启动），启动时通过 dbus 服务名
自动识别正在运行的框架（`org.fcitx.Fcitx5` / `org.fcitx.Fcitx`）：**当输入法
切换到知意时**，系统托盘出现「知」图标（英文子模式为灰色 `EN` 图标），菜单
覆盖全部设置：

```
中 / 英切换
输入法     ▸  知意 / 青简 / 搜狗 切换（与 qingjian-tray 对等）
候选词个数 ▸  1..10 单选
候选窗布局 ▸  竖排 / 横排 单选
双拼      ▸  全拼 / 小鹤 / 自然码 / 微软 / 搜狗 / ABC 单选
模糊音    ▸  7 组复选
中文标点 ✓  全角 ✓  简拼 ✓  组句 ✓  默认英文 ✓
皮肤      ▸  已安装皮肤/主题单选（含 fcitx 自带与青简系列）
重新启动 Fcitx4 / Fcitx5 / 退出托盘
```

点击菜单项即写 `~/.config/zhiyi/config.json` 并触发热加载（fcitx4 走
`ReloadConfig`，fcitx5 走 `ReloadAddonConfig`，插件 inotify 兜底），**立即
生效**；皮肤在 fcitx4 下写 `SkinType=`、fcitx5 下写 `Theme=`，即时换肤。

```bash
zhiyi-tray --check        # 无界面检查：框架 / 当前 IM / 状态 / 配置 / 皮肤 / 依赖
zhiyi-tray --setup        # 禁用框架自带托盘（fcitx4 另含搜狗 dbusmenu 插件）
zhiyi-tray --undo-setup   # 恢复
```

`--setup` 与青简 `qingjian-tray --setup` 的做法相同：fcitx4 的
notificationitem 在未激活时会画错图标，搜狗的 dbusmenu 插件会抢菜单点击
事件，托盘由 zhiyi-tray 自己注册；fcitx5 下则是禁用 `notificationitem`
插件（用户级覆盖文件，`zhiyi-tray --check` 可查看生效状态）。托盘图标只在
知意激活时显示。注意 deb 安装后的 autostart 要**重新登录**才生效；当前
会话可先手动运行 `zhiyi-tray &`（未运行时托盘上只有 fcitx 自带图标，
右键它当然没有知意的皮肤菜单）。

实现备注：启动时若发现 `~/.local/share/dbus-1/services/` 里没有
`org.kde.StatusNotifierWatcher.service` 会自动补一个——长时间运行的会话
里 dbus-daemon 会对该名字的激活调用错答 `ServiceUnknown`，导致
libappindicator 静默放弃注册、图标永不出现（libappindicator 的 dbus-glib
缺陷，2026-11-13 在 Ubuntu 20.04 实测绕过）；该文件在正常环境下无副作用。

## 测试

`ctest` 覆盖：核心词典/组句/标点/中英切换（10 项）、fcitx4 候选词 ABI 契约
（fcitx4 构建时）、fcitx5 插件进程内端到端（`fcitx5_end_to_end`：激活、
组句、数字/鼠标选词、翻页、退格、Esc、标点、配置热加载（inotify 与
`reloadConfig` 两路径）、Ctrl+Space 去激活/再激活）。

全栈人工级验证（可选，需 Xvfb 与 xdotool，对用户会话零影响）：

```bash
apt-get download xdotool && dpkg-deb -x xdotool*.deb /tmp/xdotool-bin
xvfb-run -a bash scripts/test-fcitx5-session.sh   # 真实 fcitx5 守护进程 +
                                                  # GTK 输入模块 + XTEST 打字，
                                                  # 截图验证两套主题渲染
```

## 许可证

本移植沿用上游的 **GPL-3.0-only**；词库数据来自 rime-ice（GPL-3.0-only），
完整声明见 `vendor/licenses/`、上游 `NOTICE` 与 `THIRD_PARTY_NOTICES.txt`。
Fcitx 4 / Fcitx 5 头文件与链接桩来自系统软件源，仅用于构建，不随 deb 分发。
皮肤部分取自 [qingjian](https://github.com/qingjian-team/qingjian)
（GPL-3.0-or-later，见 `scripts/generate_skin.py` 与
`scripts/generate_fcitx5_themes.py` 文件头及第三方声明）。
