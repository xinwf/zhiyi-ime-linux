#!/usr/bin/env python3
# 改编自 qingjian-team/qingjian 的 apps/linux/fcitx4/skin/generate.py；
# 本文件沿用其上游许可证 GPL-3.0-or-later（整体项目仍为 GPL-3.0-only，见 LICENSE）。
"""生成知意输入法的 Fcitx4 classic-ui 皮肤：fcitx_skin.conf 与背景/图标图片。

Fcitx4 的候选窗由 classic-ui 绘制，皮肤从 /usr/share/fcitx/skin/<id>/fcitx_skin.conf 读。
候选窗的配色、字号、边距取自青简（qingjian）输入法的 theme.json 默认主题：本脚本
移植自 qingjian-team/qingjian 仓库的 apps/linux/fcitx4/skin/generate.py，5 套调色板
与模板与其逐字段一致；唯一的刻意差异是托盘/主栏图标改画「知」字（原脚本引用
qingjian 仓库的美术资源 assets/icon/fcitx4.png）。皮肤 id 统一加 zhiyi- 前缀：
若系统里装有青简官方的 qingjian-fcitx4 包，/usr/share/fcitx/skin/qingjian*
已归它所有，同名会在 dpkg 安装时报文件冲突。

生成产物直接提交在 data/skins/，构建期不需要运行本脚本。再生成需要
Python Pillow >= 8.2（rounded_rectangle）：

    python3 scripts/generate_skin.py

五套皮肤：简白（白底黑字）、深黑（黑底白字）、青简绿、青简橙、青简蓝。
前两套与青简渲染器的浅色/深色一致；后三套把 accent 铺成背景，绿/橙/蓝能一眼分辨。
"""
import shutil
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
OUT = ROOT / "data" / "skins"

# 简白 / 深黑：与 qingjian crates/qingjian-render/themes/qingjian/theme.json 的
# surface、text 对齐
LIGHT = {
    "surface": (255, 255, 255),
    "text": (40, 40, 40),
    "gloss": (90, 90, 90),
    "faint": (130, 130, 130),
    "fresh": (255, 141, 40),
    "cloud": (0, 195, 208),
    "accent": (148, 190, 82),
}
DARK = {
    "surface": (30, 30, 30),
    "text": (235, 235, 235),
    "gloss": (200, 200, 200),
    "faint": (140, 140, 140),
    "fresh": (255, 146, 48),
    "cloud": (0, 210, 224),
    "accent": (51, 111, 51),
}
# 青简绿：原主题的 accent #94be52 铺成淡绿底，文字转深绿
GREEN = {
    "surface": (238, 245, 226),
    "text": (34, 51, 15),
    "gloss": (74, 97, 43),
    "faint": (122, 143, 96),
    "fresh": (224, 106, 10),
    "cloud": (0, 145, 130),
    "accent": (123, 168, 62),
}
# 青简橙：暖橙底，首候选加深成橙红
ORANGE = {
    "surface": (255, 244, 232),
    "text": (74, 43, 16),
    "gloss": (122, 82, 43),
    "faint": (168, 128, 90),
    "fresh": (240, 96, 16),
    "cloud": (0, 145, 150),
    "accent": (226, 122, 44),
}
# 青简蓝：晴蓝底，首候选与强调都用蓝
BLUE = {
    "surface": (238, 245, 255),
    "text": (22, 50, 79),
    "gloss": (58, 88, 120),
    "faint": (120, 150, 180),
    "fresh": (22, 119, 255),
    "cloud": (0, 150, 190),
    "accent": (47, 111, 237),
}

SKINS = (
    ("zhiyi-qingjian-light", "知意简白", "白底黑字（青简浅色配色）", LIGHT),
    ("zhiyi-qingjian-dark", "知意深黑", "黑底白字（青简深色配色）", DARK),
    ("zhiyi-qingjian", "知意青简绿", "青简绿：淡绿底、深绿字", GREEN),
    ("zhiyi-qingjian-orange", "知意青简橙", "暖橙底、深棕字", ORANGE),
    ("zhiyi-qingjian-blue", "知意青简蓝", "晴蓝底、深蓝字", BLUE),
)

SKIN_CONF = """[SkinInfo]
Name={name}
Version=1
Author=青简
Desc={desc}

[SkinFont]
FontSize={font_size}
MenuFontSize=13
RespectDPI=True
TipColor={gloss}
InputColor={text}
IndexColor={faint}
FirstCandColor={fresh}
UserPhraseColor={cloud}
CodeColor={text}
OtherColor={text}
ActiveMenuColor={on_accent}
InactiveMenuColor={gloss}

[SkinMainBar]
BackImg=bar.png
Logo=logo.png
Eng=en.png
Active=cn.png
MarginLeft=4
MarginRight=4
MarginTop=4
MarginBottom=4
UseCustomTextIconColor=True
ActiveTextIconColor={accent}
InactiveTextIconColor={faint}

[SkinInputBar]
BackImg=input.png
MarginLeft=12
MarginRight=12
MarginTop=8
MarginBottom=8
CursorColor={fresh}
InputPos=0
OutputPos=0
BackArrow=prev.png
ForwardArrow=next.png
BackArrowX=10
BackArrowY=12
ForwardArrowX=6
ForwardArrowY=12

[SkinTrayIcon]
Active=active.png
Inactive=inactive.png

[SkinMenu]
BackImg=menu.png
MarginLeft=8
MarginRight=8
MarginTop=6
MarginBottom=6
ActiveColor={accent}
LineColor={faint}

[SkinKeyboard]
KeyColor={accent}
"""


def rgb(color):
    return " ".join(str(c) for c in color)


def luminance(color):
    r, g, b = (c / 255 for c in color[:3])
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def border_for(palette):
    # 浅底用淡黑边，深底用淡白边
    return (0, 0, 0, 26) if luminance(palette["surface"]) > 0.5 else (255, 255, 255, 34)


def rounded(size, fill, radius, border=None):
    image = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle(
        (0, 0, size[0] - 1, size[1] - 1), radius=radius, fill=fill, outline=border, width=1
    )
    return image


def tray_icon(size, color):
    # 原脚本把青简的竹简图标染成单色；移植版改用「知」字图标，不引用
    # qingjian 仓库的美术资源。字体不可用时与 text_icon 一样退到内置位图字体。
    return text_icon(size, "知", color)


def text_icon(size, text, color, font_size=None, stroke_width=0, stroke_fill=None):
    image = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    fonts = [
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ]
    font = None
    for path in fonts:
        if Path(path).is_file():
            try:
                font = ImageFont.truetype(path, font_size or int(size[1] * 0.78))
                break
            except OSError:
                continue
    if font is None:
        font = ImageFont.load_default()
    box = draw.textbbox((0, 0), text, font=font)
    draw.text(
        ((size[0] - (box[2] - box[0])) / 2 - box[0], (size[1] - (box[3] - box[1])) / 2 - box[1]),
        text,
        font=font,
        fill=color,
        stroke_width=stroke_width,
        stroke_fill=stroke_fill,
    )
    return image


def arrow(size, direction, color):
    image = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    w, h = size
    if direction == "left":
        points = [(w - 1, 0), (0, h // 2), (w - 1, h - 1)]
    else:
        points = [(0, 0), (w - 1, h // 2), (0, h - 1)]
    draw.polygon(points, fill=color)
    return image


def build(skin_id, name, desc, palette, font_size=16):
    out = OUT / skin_id
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    border = border_for(palette)
    surface = palette["surface"] + (255,)
    rounded((200, 100), surface, 12, border).save(out / "input.png")
    rounded((200, 40), surface, 10, border).save(out / "bar.png")
    rounded((200, 40), surface, 10, border).save(out / "menu.png")
    tray_icon((22, 22), palette["text"]).save(out / "active.png")
    tray_icon((22, 22), palette["faint"]).save(out / "inactive.png")
    tray_icon((22, 22), palette["text"]).save(out / "logo.png")
    text_icon((24, 24), "中", palette["text"]).save(out / "cn.png")
    text_icon((24, 24), "EN", palette["faint"], 11).save(out / "en.png")
    arrow((6, 12), "left", palette["faint"]).save(out / "prev.png")
    arrow((6, 12), "right", palette["faint"]).save(out / "next.png")

    values = {key: rgb(value) for key, value in palette.items()}
    # 菜单高亮：深色底用白字，浅色底用深字
    values["on_accent"] = "255 255 255" if luminance(palette["accent"]) < 0.6 else "40 40 40"
    values.update(name=name, desc=desc, font_size=font_size)
    (out / "fcitx_skin.conf").write_text(SKIN_CONF.format(**values), encoding="utf-8")


def tray_badge(size, text, fill, glyph_color, font_ratio=0.62):
    """状态栏徽章：实心圆角底 + 居中字，整块亮色在黑面板上最醒目。"""
    image = Image.new("RGBA", size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((0, 0, size[0] - 1, size[1] - 1),
                           radius=max(4, size[0] // 5), fill=fill)
    font = None
    for path in [
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ]:
        if Path(path).is_file():
            try:
                font = ImageFont.truetype(path, int(size[1] * font_ratio))
                break
            except OSError:
                continue
    if font is None:
        font = ImageFont.load_default()
    box = draw.textbbox((0, 0), text, font=font)
    draw.text(((size[0] - (box[2] - box[0])) / 2 - box[0],
               (size[1] - (box[3] - box[1])) / 2 - box[1] - size[1] * 0.03),
              text, font=font, fill=glyph_color)
    return image


def main():
    for skin_id, name, desc, palette in SKINS:
        build(skin_id, name, desc, palette)
        print(skin_id, name, "->", len(list((OUT / skin_id).iterdir())), "个文件")
    icons = OUT.parent / "icons"
    icons.mkdir(parents=True, exist_ok=True)
    # zhiyi-tray 的状态图标：中 = 亮绿实心徽章 + 深色「知」（黑色状态栏上
    # 整块醒目），英文 = 暗灰绿徽章 + 浅字（空闲态）
    tray_badge((24, 24), "知", (139, 216, 84), (24, 34, 14)).save(
        icons / "zhiyi-tray-cn.png")
    tray_badge((24, 24), "EN", (96, 104, 92), (222, 230, 216), font_ratio=0.48).save(
        icons / "zhiyi-tray-en.png")
    print("zhiyi-tray icons ->", sorted(p.name for p in icons.iterdir()))


if __name__ == "__main__":
    main()
