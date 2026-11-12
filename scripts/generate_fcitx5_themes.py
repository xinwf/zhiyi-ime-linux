#!/usr/bin/env python3
"""Convert the committed Fcitx4 Qingjian skins into Fcitx5 classic-ui themes.

Reads data/skins/<id>/fcitx_skin.conf (colours, margins) and emits
data/fcitx5-themes/<id>/theme.conf plus the PNG assets. Committed to git like
the fcitx4 skins, so building does not need Pillow. Run from the repo root:

    python3 scripts/generate_fcitx5_themes.py

The Fcitx5 theme format maps like this:
  - skin input.png   -> [InputPanel/Background] Image=background.png
    with [InputPanel/Background/Margin] as the 9-patch border (corner radius).
  - SkinInputBar Margin* -> [InputPanel/ContentMargin]
  - InputColor/CodeColor/OtherColor -> NormalColor
  - FirstCandColor -> HighlightColor / HighlightCandidateColor
  - (new) a slightly darker tint of the background -> HighlightBackgroundColor
  - skin prev/next.png -> page buttons, menu.png -> [Menu/Background]
Copyright: GPL-3.0-only, part of the Zhiyi IME Linux port.
"""
import configparser
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKIN_DIR = ROOT / "data" / "skins"
OUT_DIR = ROOT / "data" / "fcitx5-themes"

# 9-patch border of input.png (rounded corner radius is ~14px on the 200x100
# source image); a bit of headroom keeps the stretch seams off the corners.
CORNER_BORDER = 16

# Highlight background: a tint slightly darker than the panel background so
# the selected candidate reads clearly. Keyed by skin id.
HIGHLIGHT_BG = {
    "zhiyi-qingjian": "#dbe6bd",
    "zhiyi-qingjian-light": "#e2e2e2",
    "zhiyi-qingjian-dark": "#3c3c3c",
    "zhiyi-qingjian-orange": "#f1d9b6",
    "zhiyi-qingjian-blue": "#d0e2f5",
}

EN_NAMES = {
    "zhiyi-qingjian": "Zhiyi Qingjian Green",
    "zhiyi-qingjian-light": "Zhiyi Qingjian Light",
    "zhiyi-qingjian-dark": "Zhiyi Qingjian Dark",
    "zhiyi-qingjian-orange": "Zhiyi Qingjian Orange",
    "zhiyi-qingjian-blue": "Zhiyi Qingjian Blue",
}


def rgb_to_hex(value: str) -> str:
    r, g, b = (int(v) for v in value.split()[:3])
    return "#{:02x}{:02x}{:02x}".format(r, g, b)


def convert(skin_id: str) -> None:
    skin = SKIN_DIR / skin_id
    conf = configparser.ConfigParser()
    conf.read(skin / "fcitx_skin.conf", encoding="utf-8")
    info, font, bar, menu = (conf["SkinInfo"], conf["SkinFont"],
                             conf["SkinInputBar"], conf["SkinMenu"])

    normal = rgb_to_hex(font["InputColor"])
    highlight = rgb_to_hex(font["FirstCandColor"])
    separator = rgb_to_hex(font.get("LineColor", fallback="130 130 130"))
    tip = rgb_to_hex(font.get("TipColor", fallback=normal))
    highlight_bg = HIGHLIGHT_BG[skin_id]
    desc = info.get("Desc", fallback="")

    out = OUT_DIR / skin_id
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy(skin / "input.png", out / "background.png")
    shutil.copy(skin / "prev.png", out / "prev.png")
    shutil.copy(skin / "next.png", out / "next.png")
    shutil.copy(skin / "menu.png", out / "menu.png")

    theme = f"""[Metadata]
Name[zh_CN]={info['Name']}
Name={EN_NAMES[skin_id]}
Version=1
Author={info.get('Author', fallback='Zhiyi IME Linux port contributors')}
Description={desc}
ScaleWithDPI=True

[InputPanel]
Font=Sans 13
NormalColor={normal}
HighlightColor={highlight}
HighlightCandidateColor={highlight}
HighlightBackgroundColor={highlight_bg}
PageButtonAlignment=Last Candidate

[InputPanel/Background]
Image=background.png

[InputPanel/Background/Margin]
Left={CORNER_BORDER}
Right={CORNER_BORDER}
Top={CORNER_BORDER}
Bottom={CORNER_BORDER}

[InputPanel/Highlight]
Color={highlight_bg}

[InputPanel/Highlight/Margin]
Left=4
Right=4
Top=4
Bottom=4

[InputPanel/ContentMargin]
Left={bar['MarginLeft']}
Right={bar['MarginRight']}
Top={bar['MarginTop']}
Bottom={bar['MarginBottom']}

[InputPanel/TextMargin]
Left=5
Right=5
Top=5
Bottom=5

[InputPanel/PrevPage]
Image=prev.png

[InputPanel/PrevPage/ClickMargin]
Left=5
Right=5
Top=4
Bottom=4

[InputPanel/NextPage]
Image=next.png

[InputPanel/NextPage/ClickMargin]
Left=5
Right=5
Top=4
Bottom=4

[Menu/Background]
Image=menu.png

[Menu/Background/Margin]
Left={CORNER_BORDER}
Right={CORNER_BORDER}
Top={CORNER_BORDER}
Bottom={CORNER_BORDER}

[Menu/ContentMargin]
Left={menu['MarginLeft']}
Right={menu['MarginRight']}
Top={menu['MarginTop']}
Bottom={menu['MarginBottom']}

[Menu/TextMargin]
Left=5
Right=5
Top=5
Bottom=5

[Menu/Highlight]
Color={highlight_bg}

[Menu/Highlight/Margin]
Left=4
Right=4
Top=4
Bottom=4

[Menu/Separator]
Color={separator}
"""
    (out / "theme.conf").write_text(theme, encoding="utf-8")
    print(f"  {skin_id}: NormalColor={normal} Highlight={highlight} "
          f"HighlightBg={highlight_bg} Tip={tip}")


def main() -> int:
    ids = sorted(p.name for p in SKIN_DIR.iterdir()
                 if (p / "fcitx_skin.conf").is_file())
    if not ids:
        print("no skins found", file=sys.stderr)
        return 1
    for skin_id in ids:
        convert(skin_id)
    print(f"themes written to {OUT_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
