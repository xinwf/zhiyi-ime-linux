#!/usr/bin/env bash
# Full-stack Fcitx5 runtime test of the staged zhiyi-ime install, completely
# isolated from the user's session:
#
#   Xvfb display (xvfb-run) + private dbus-run-session
#     └── real /usr/bin/fcitx5 daemon (staged addon via FCITX_*_DIRS)
#           └── GTK3 apps with GTK_IM_MODULE=fcitx5 (real fcitx5 im module)
#                 └── typing driven by xdotool (XTEST fake input)
#
# Verifies: addon discovery, IM registration, trigger-key activation, pinyin
# composition, candidate window rendering with the packaged zhiyi themes
# (screenshots), and commit into a real application.
#
# Usage: xvfb-run -a bash scripts/test-fcitx5-session.sh [staging-root]
#   staging-root: a DESTDIR-style tree; defaults to build/deb/stage.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE="${1:-$ROOT/build/deb/stage}"
WORK="$(mktemp -d /tmp/zhiyi-session-test-XXXXXX)"
TRIPLET="$(gcc -dumpmachine)"

for tool in Xvfb import dbus-run-session fcitx5 fcitx5-remote python3; do
    command -v "$tool" >/dev/null || { echo "missing tool: $tool" >&2; exit 1; }
done
if [ ! -e "$STAGE/usr/lib/$TRIPLET/fcitx5/libzhiyi.so" ]; then
    echo "staged fcitx5 addon not found under $STAGE - run scripts/build-deb.sh or dpkg-deb -x" >&2
    exit 1
fi

export HOME="$WORK/home"
mkdir -p "$HOME/.config/fcitx5" "$HOME/.local/bin"
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_DATA_HOME="$HOME/.local/share"
export ZHIYI_DATA_DIR="$STAGE/usr/share/zhiyi/data"
export FCITX_DATA_DIRS="$STAGE/usr/share/fcitx5:/usr/share/fcitx5"
export FCITX_ADDON_DIRS="$STAGE/usr/lib/$TRIPLET/fcitx5:/usr/lib/$TRIPLET/fcitx5"

cat > "$HOME/.config/fcitx5/profile" <<'EOF'
[Groups/0]
Name=Default
Default Layout=us
DefaultIM=zhiyi

[Groups/0/Items/0]
Name=keyboard-us
Layout=

[Groups/0/Items/1]
Name=zhiyi
Layout=

[GroupOrder]
0=Default
EOF

# xdotool（免 root 解包；系统里已有 libxdo3），XTEST 合成按键
XDOTOOL="$HOME/.local/bin/xdotool"
if [ ! -x "$XDOTOOL" ]; then
    if command -v xdotool >/dev/null 2>&1; then
        XDOTOOL="$(command -v xdotool)"
    elif [ -x /tmp/xdotool-bin/usr/bin/xdotool ]; then
        cp /tmp/xdotool-bin/usr/bin/xdotool "$XDOTOOL"
    else
        echo "xdotool missing: apt-get download xdotool && dpkg-deb -x to /tmp/xdotool-bin" >&2
        exit 1
    fi
fi
export XDOTOOL

# GTK3 应用：一个 Entry，超时后把内容打到 stdout 退出
cat > "$WORK/gtk-app.py" <<'EOF'
import sys
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib

win = Gtk.Window()
win.set_title('gtk-app')
win.set_default_size(400, 100)
entry = Gtk.Entry()
win.add(entry)
win.connect('destroy', Gtk.main_quit)

def dump():
    sys.stdout.write('BUFFER:' + entry.get_text() + '\n')
    sys.stdout.flush()
    Gtk.main_quit()

win.show_all()
entry.grab_focus()
GLib.timeout_add_seconds(8, dump)
Gtk.main()
EOF

echo "==> start isolated session (work dir $WORK)"
# 两个独立的 GTK 应用（各自输入上下文），分别验证两套主题。
# Xvfb 没有窗口管理器：commit 之后 X 焦点语义不可靠，所以不中途重聚焦，
# 也不复用应用；每阶段：预写 classicui.conf 主题 -> 开应用 -> 聚焦 ->
# Ctrl+Space 激活 -> nihao -> 截图 -> 空格上屏 -> 读回 Entry 内容。
dbus-run-session -- bash -s "$XDOTOOL" "$STAGE" "$WORK" <<'OUTER'
set -u
XDOTOOL="$1"; STAGE="$2"; WORK="$3"

# classicui 只在插件加载时读取一次配置（没有外部推送时），所以每个阶段
# 先写好主题配置再启动自己的守护进程实例。
conf="$HOME/.config/fcitx5/conf/classicui.conf"
mkdir -p "$(dirname "$conf")"
export GTK_IM_MODULE=fcitx5

start_daemon() {
    fcitx5 --disable=all --enable=dbus,dbusfrontend,keyboard,classicui,xcb,zhiyi \
        >> "$WORK/daemon.log" 2>&1 &
    DAEMON_PID=$!
    sleep 2.5
    if ! fcitx5-remote --check; then
        echo "FAIL: fcitx5 daemon did not start" >&2
        cat "$WORK/daemon.log"
        exit 1
    fi
}

stop_daemon() {
    fcitx5-remote -e 2>/dev/null || true
    sleep 0.5
    kill $DAEMON_PID 2>/dev/null || true
    wait $DAEMON_PID 2>/dev/null || true
}

run_pass() {
    local theme="$1" shot="$2" out="$3" err="$4"
    printf 'Font="Noto Sans CJK SC 13"\nTheme=%s\n' "$theme" > "$conf"
    start_daemon
    python3 "$WORK/gtk-app.py" > "$out" 2> "$err" &
    local gtk_pid=$!
    sleep 2.5
    local wid
    wid="$("$XDOTOOL" search --onlyvisible --name 'gtk-app' | head -1)"
    [ -n "$wid" ] && "$XDOTOOL" windowfocus "$wid" || true
    sleep 0.3
    "$XDOTOOL" key --delay 120 ctrl+space
    sleep 0.5
    "$XDOTOOL" key --delay 130 n i h a o
    sleep 1.3
    import -window root "$shot" 2>/dev/null
    "$XDOTOOL" key --delay 100 space
    sleep 0.8
    wait $gtk_pid 2>/dev/null || true
    stop_daemon
}

echo "==> pass 1: 知意青简绿主题"
run_pass zhiyi-qingjian "$WORK/shot-1-vertical-green.png" \
    "$WORK/gtk1.out" "$WORK/gtk1.err"

echo "==> pass 2: 知意深黑主题"
run_pass zhiyi-qingjian-dark "$WORK/shot-2-dark.png" \
    "$WORK/gtk2.out" "$WORK/gtk2.err"

echo "==> results"
grep -iE "Loaded addon zhiyi|Zhiyi|error" "$WORK/daemon.log" | head -5 || true
echo "--- gtk1 buffer:"; cat "$WORK/gtk1.out" 2>/dev/null
echo "--- gtk2 buffer:"; cat "$WORK/gtk2.out" 2>/dev/null
OUTER
DBUS_RC=$?

echo
echo "artifacts in $WORK:"
ls -la "$WORK"/shot-*.png 2>/dev/null || true
echo
if [ "$DBUS_RC" -eq 0 ] && \
   grep -q "BUFFER:你好" "$WORK/gtk1.out" 2>/dev/null && \
   grep -q "BUFFER:你好" "$WORK/gtk2.out" 2>/dev/null; then
    echo "PASS: 两次键入 nihao 均在真实 GTK 应用中上屏 你好（两套主题各验证一次）"
else
    echo "FAIL: expected 你好 in both GTK entry buffers (dbus rc=$DBUS_RC)"
    echo "--- gtk1 stderr:"; tail -3 "$WORK/gtk1.err" 2>/dev/null || true
    echo "--- gtk2 stderr:"; tail -3 "$WORK/gtk2.err" 2>/dev/null || true
    exit 1
fi
