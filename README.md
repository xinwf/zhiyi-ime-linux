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
- 候选数、中文标点、模糊音、双拼等可配置

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
| `/usr/bin/zhiyi-config` | 配置工具 |

用户数据遵循 XDG：配置 `~/.config/zhiyi/config.json`，
学习记录 `~/.local/share/zhiyi/user.tsv`。卸载 deb 不会删除用户数据。

## 配置

```bash
zhiyi-config --show
zhiyi-config --set page_size 9
zhiyi-config --set fuzzy 5            # 位掩码: z_zh=1 c_ch=2 s_sh=4 ...
zhiyi-config --set shuangpin xiaohe   # 空字符串 = 全拼
zhiyi-config --set chinese_punctuation 1
fcitx -r
```

## 许可证

本移植沿用上游的 **GPL-3.0-only**；词库数据来自 rime-ice（GPL-3.0-only），
完整声明见 `vendor/licenses/`、上游 `NOTICE` 与 `THIRD_PARTY_NOTICES.txt`。
Fcitx 4 头文件/链接桩来自系统软件源，仅用于构建，不随 deb 分发。
