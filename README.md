# 知意输入法 · Linux 版（Fcitx 4）

Windows 版 [知意输入法](https://github.com/dd1000001000/zhiyi_ime) 的 Linux 移植，
前端为 **Fcitx 4** 输入法插件，最终产物是一个可直接用 `dpkg -i` 安装的 `.deb`。

## 为什么是 Fcitx 4

目标机器（Ubuntu 20.04）默认且正在运行的输入法框架是 `fcitx 4.2.9.7`，
GTK/Qt/XIM 前端与 `im-config` 都已配置好；而源里的 fcitx5 是 2020 年的早期
git 快照，安装后还需要切换 `im-config` 并重新登录。用 Fcitx 4 可以做到
「装完 `fcitx -r` 就能用」，同时引擎与框架完全解耦，以后加 fcitx5 前端只需
再写一层适配。

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
它们分别由 Fcitx 4 插件、框架自带候选窗、`zhiyi-config`、deb 包替代。

## 架构

```
按键 (Fcitx KeySym)
   │  src/fcitx/zhiyi_addon.cpp   —— 唯一与框架耦合的文件
   ▼
src/core/engine.cpp     —— 会话状态、候选生成、组句、自学习、标点
   ├── dictionary.cpp   —— mmap 只读索引（全拼 / 首字母两套）
   ├── pinyin.cpp       —— 音节切分、模糊音变体、双拼转换
   └── config.cpp       —— XDG 配置 (nlohmann/json)
   ▲
data/                   —— 构建期从上游词库生成的二进制词典
```

`src/core/` 不依赖 Fcitx、X11、GLib，`tools/query.cpp` 可以脱离图形环境
完整回放按键序列，因此核心逻辑可以用 `ctest` 做回归测试。

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

- 轻按 Shift 切换中英文依赖 Fcitx 把 Shift 释放事件传给插件；若桌面环境
  先拦截了该热键，请改用 Fcitx 自带的中英文切换（Ctrl+Space / 配置里的热键）。
- 简拼索引由上游 `syllable_ids` 字段生成，个别词条切分有误，因此简拼候选
  排在全拼候选之后；全拼与组句不受影响。
- 打包体积主要来自 46 MiB 二进制词典（约 72 万条主索引 + 72 万条简拼索引）。

## 构建（不需要 root）

环境要求：CMake ≥ 3.15、C++17 编译器、Python 3、`dpkg-deb`、Fcitx 4 运行时。
开发头文件由脚本免 root 下载解包到 `.deps/sysroot`：

```bash
bash scripts/fetch-sdk.sh     # 仅在首次构建时需要
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

## 安装与使用

```bash
sudo dpkg -i dist/zhiyi-ime_*.deb
fcitx -r                    # 重启当前会话的 fcitx；或注销重新登录
```

用 `Ctrl+Space` 开启输入法，`Ctrl+Shift` 切换到「知意输入法」。
若系统默认框架不是 fcitx，先执行 `im-config -n fcitx` 并重新登录。

安装内容：

| 路径 | 内容 |
|---|---|
| `/usr/lib/<triplet>/fcitx/fcitx-zhiyi.so` | 输入法插件 |
| `/usr/share/fcitx/addon/fcitx-zhiyi.conf` | 插件描述 |
| `/usr/share/fcitx/inputmethod/zhiyi.conf` | 输入法注册 |
| `/usr/share/zhiyi/data/` | 词典、音节表、双拼方案、出厂配置 |
| `/usr/share/fcitx/skin/zhiyi-qingjian*/` | 5 套候选窗皮肤（移植自青简，见下文） |
| `/usr/bin/zhiyi-config` | 配置工具 |

用户数据遵循 XDG：配置 `~/.config/zhiyi/config.json`，
学习记录 `~/.local/share/zhiyi/user.tsv`。卸载 deb 不会删除用户数据。

## 配置

```bash
zhiyi-config --show
zhiyi-config --set page_size 9
zhiyi-config --set fuzzy 5            # 位掩码: z_zh=1 c_ch=2 s_sh=4 ...
zhiyi-config --set shuangpin xiaohe   # 空字符串 = 全拼
zhiyi-config --set layout horizontal  # 候选横排显示（默认 vertical 竖排）
zhiyi-config --set chinese_punctuation 1
fcitx -r
```

配置文件只由 `zhiyi-config` 或手工编辑修改，引擎不会回写（fcitx 退出时
只保存用户词频 `user.tsv`），所以每次改完 `fcitx -r` 总是生效。

## 皮肤（移植自青简）

候选窗由 fcitx 的 classic-ui 绘制，本项目把青简（qingjian）输入法为 Fcitx 4
生成的 5 套经典皮肤收编进 deb：配色、字号、边距与其
`crates/qingjian-render/themes/qingjian/theme.json` 默认主题逐字段一致。

| 皮肤 id | 外观 |
|---|---|
| `zhiyi-qingjian` | 知意青简绿：淡绿底、深绿字 |
| `zhiyi-qingjian-light` | 知意简白：白底黑字（青简浅色配色） |
| `zhiyi-qingjian-dark` | 知意深黑：黑底白字（青简深色配色） |
| `zhiyi-qingjian-orange` | 知意青简橙：暖橙底、深棕字 |
| `zhiyi-qingjian-blue` | 知意青简蓝：晴蓝底、深蓝字 |

切换方法：`fcitx-config-gtk3` 的「外观 → 皮肤」里选择，或者写
`~/.config/fcitx/config`：

```ini
[Appearance]
SkinName=zhiyi-qingjian
```

改完执行 `fcitx -r` 生效。注意 classic-ui 的皮肤对 fcitx 全局生效
（候选窗只有一套皮肤，所有输入法共用）。皮肤 id 统一带 `zhiyi-` 前缀，
与系统里青简官方 `qingjian-fcitx4` 包安装的 `qingjian*` 皮肤并存，
互不覆盖；两者配色一致，选哪个外观都相同。

皮肤文件由 `scripts/generate_skin.py`（改编自青简
`apps/linux/fcitx4/skin/generate.py`，需 Pillow ≥ 8.2）生成后提交在
`data/skins/`，构建期不需要重新生成；与青简原版的差异是托盘/主栏
图标换成「知」字图标（原版使用青简的竹简美术资源）。

## 托盘设置（图形界面）

`zhiyi-tray` 是一个 AppIndicator 托盘小程序（登录后经
`/etc/xdg/autostart/zhiyi-tray.desktop` 自启动）：**当输入法切换到知意时**，
系统托盘出现「知」图标（英文子模式为灰色 `EN` 图标），菜单覆盖全部设置：

```
中 / 英切换
输入法     ▸  知意 / 青简 / 搜狗 切换（与 qingjian-tray 对等）
候选词个数 ▸  1..10 单选
候选窗布局 ▸  竖排 / 横排 单选
双拼      ▸  全拼 / 小鹤 / 自然码 / 微软 / 搜狗 / ABC 单选
模糊音    ▸  7 组复选
中文标点 ✓  全角 ✓  简拼 ✓  组句 ✓  默认英文 ✓
皮肤      ▸  已安装皮肤单选（含 fcitx 自带与青简系列）
重新启动 Fcitx4 / 退出托盘
```

点击菜单项即写 `~/.config/zhiyi/config.json` 并触发 fcitx 的
`ReloadConfig`，插件热加载配置，**立即生效，无需 `fcitx -r`**；皮肤写
`~/.config/fcitx/conf/fcitx-classic-ui.config` 的 `SkinType=`，classic-ui
即时换肤。

```bash
zhiyi-tray --check        # 无界面检查：当前 IM / 状态 / 配置 / 皮肤 / 依赖
zhiyi-tray --setup        # 禁用 fcitx 自带托盘(notificationitem)与搜狗 dbusmenu 插件
zhiyi-tray --undo-setup   # 恢复
```

`--setup` 与青简 `qingjian-tray --setup` 的做法相同：fcitx4 的
notificationitem 在未激活时会画错图标，搜狗的 dbusmenu 插件会抢菜单点击
事件，托盘由 zhiyi-tray 自己注册（已在装了青简托盘的机器上验证过）。托盘
图标只在知意激活时显示，与 qingjian-tray 并存互不影响。

实现备注：启动时若发现 `~/.local/share/dbus-1/services/` 里没有
`org.kde.StatusNotifierWatcher.service` 会自动补一个——长时间运行的会话
里 dbus-daemon 会对该名字的激活调用错答 `ServiceUnknown`，导致
libappindicator 静默放弃注册、图标永不出现（libappindicator 的 dbus-glib
缺陷，2026-11-13 在 Ubuntu 20.04 实测绕过）；该文件在正常环境下无副作用。

## 许可证

本移植沿用上游的 **GPL-3.0-only**；词库数据来自 rime-ice（GPL-3.0-only），
完整声明见 `vendor/licenses/`、上游 `NOTICE` 与 `THIRD_PARTY_NOTICES.txt`。
Fcitx 4 头文件/链接桩来自系统软件源，仅用于构建，不随 deb 分发。
皮肤部分取自 [qingjian](https://github.com/qingjian-team/qingjian)
（GPL-3.0-or-later，见 `scripts/generate_skin.py` 文件头与第三方声明）。
