#!/usr/bin/env python3
"""Build the Linux runtime dictionaries from the upstream SQLite dictionary.

The upstream project (zhiyi_ime) ships `data/pinyin.dict.db.zip`, a SQLite
database with `dict(text, code, frequency, syllable_ids)` and a `spellings`
table.  Windows builds turn that into a set of proprietary binary indexes via
a much larger Python pipeline (repair, shuangpin expansion, top-n trie...).

For the Linux port we generate a simpler, self-describing format documented in
src/core/dict_format.h:

    zhiyi.dict.bin      code = full pinyin spelling, e.g. "nihao"
    zhiyi.initials.bin  code = syllable initials,     e.g. "nh"
    zhiyi.syllables     newline separated legal syllables

Both .bin files share the same (header, sorted entries, string blob) layout so
the C++ side only needs one mmap based reader.  All data stays little-endian
fixed width PODs, matching x86_64/aarch64 Linux.
"""

import argparse
import functools
import json
import os
import re
import sqlite3
import struct
import sys
import tempfile
import zipfile

MAGIC = b"ZYDICT01"
VERSION = 1
HEADER_STRUCT = struct.Struct("<8sIIII2I")   # 32 bytes
ENTRY_STRUCT = struct.Struct("<IIiHH")       # 16 bytes
CODE_RE = re.compile(r"[^a-z]")


def load_sqlite(source):
    """Return a path to an on-disk SQLite database, unpacking a zip if needed."""
    if source.lower().endswith(".zip"):
        with zipfile.ZipFile(source) as archive:
            names = [n for n in archive.namelist() if n.endswith(".db")]
            if not names:
                raise SystemExit("no .db file inside %s" % source)
            tmpdir = tempfile.mkdtemp(prefix="zhiyi-dict-")
            target = os.path.join(tmpdir, "source.db")
            with open(target, "wb") as out:
                out.write(archive.read(names[0]))
            return target, tmpdir
    return source, None


def clean_code(raw):
    return CODE_RE.sub("", raw.lower())


def meaningful(text):
    """Drop empty strings, control characters and pure punctuation/emoji rows."""
    if not text:
        return False
    for ch in text:
        cp = ord(ch)
        if cp < 32 or cp == 127:
            return False
    return any(
        "\u4e00" <= ch <= "\u9fff" or ch.isascii() and ch.isalnum()
        for ch in text
    )


def build_syllable_data(connection):
    rows = connection.execute(
        "SELECT DISTINCT syllable FROM spellings WHERE type = 0 AND syllable != ''"
    ).fetchall()
    syllables = sorted({r[0].lower() for r in rows if r[0].isalpha()})
    # Some builds only have the input column populated; fall back to a small
    # standard syllable list so initials can still be derived.
    if len(syllables) < 100:
        syllables = sorted(set(DEFAULT_SYLLABLES))
    return syllables


DEFAULT_SYLLABLES = """
a ai an ang ao ba bai ban bang bao bei ben beng bi bian biao bie bin bing bo bu
ca cai can cang cao ce cen ceng cha chai chan chang chao che chen cheng chi chong
chou chu chua chuai chuan chuang chui chun chuo ci cong cou cu cuan cui cun cuo
da dai dan dang dao de dei den deng di dia dian diao die ding diu dong dou du duan
dui dun duo e en er fa fan fang fei fen feng fo fou fu ga gai gan gang gao ge gei
gen geng gong gou gu gua guai guan guang gui gun guo ha hai han hang hao he hei
hen heng hong hou hu hua huai huan huang hui hun huo ji jia jian jiang jiao jie
jin jing jiong jiu ju juan jue jun ka kai kan kang kao ke ken keng kong kou ku kua
kuai kuan kuang kui kun kuo la lai lan lang lao le lei leng li lia lian liang liao
lie lin ling liu long lou lu luan luo lun lv lve ma mai man mang mao me mei men
meng mi mian miao mie min ming miu mo mou mu na nai nan nang nao ne nei nen neng
ni nian niang niao nie nin ning niu nong nou nu nuan nuo nv nve o ou pa pai pan
pang pao pei pen peng pi pian piao pie pin ping po pou pu qi qia qian qiang qiao
qie qin qing qiong qiu qu quan que qun ran rang rao re ren reng ri rong rou ru
ruan rui run ruo sa sai san sang sao se sen seng sha shai shan shang shao she shei
shen sheng shi shou shu shua shuai shuan shuang shui shun shuo si song sou su suan
sui sun suo ta tai tan tang tao te teng ti tian tiao tie ting tong tou tu tuan tui
tun tuo wa wai wan wang wei wen weng wo wu xi xia xian xiang xiao xie xin xing
xiong xiu xu xuan xue xun ya yan yang yao ye yi yin ying yo yong you yu yuan yue
yun za zai zan zang zao ze zei zen zeng zha zhai zhan zhang zhao zhe zhei zhen
zheng zhi zhong zhou zhu zhua zhuai zhuan zhuang zhui zhun zhuo zi zong zou zu
zuan zui zun zuo
""".split()


@functools.lru_cache(maxsize=200000)
def split_syllables(code, syllable_tuple):
    """Longest-match segmentation, used only when syllable_ids is unusable."""
    max_len = max(len(s) for s in syllable_tuple)
    syllables = set(syllable_tuple)
    n = len(code)
    dp = [None] * (n + 1)
    dp[0] = []
    for i in range(n):
        if dp[i] is None:
            continue
        for size in range(min(max_len, n - i), 0, -1):
            piece = code[i:i + size]
            if piece in syllables:
                if dp[i + size] is None:
                    dp[i + size] = dp[i] + [piece]
                break
    return dp[n] if dp[n] else []


def initials_of(code, syllable_ids, syllable_tuple):
    parts = [p for p in syllable_ids.split(":") if p]
    if not parts:
        parts = split_syllables(code, syllable_tuple)
    if not parts:
        return ""
    return "".join(p[0] for p in parts if p and p[0].isalpha())


def collect(connection, min_frequency, syllable_tuple, progress):
    seen_main = {}
    seen_initials = {}
    cursor = connection.execute(
        "SELECT text, code, frequency, syllable_ids FROM dict"
    )
    for index, (text, raw_code, frequency, syllable_ids) in enumerate(cursor):
        if progress and index % 250000 == 0:
            print("  scanned %d rows..." % index, file=sys.stderr)
        code = clean_code(raw_code or "")
        if not code or not meaningful(text):
            continue
        single = len(text) == 1
        if not single and (frequency or 0) < min_frequency:
            continue
        freq = int(frequency or 0)
        key = (code, text)
        if seen_main.get(key, -1) < freq:
            seen_main[key] = freq

        initials = initials_of(code, syllable_ids or "", syllable_tuple)
        if initials and initials != code:
            ikey = (initials, text)
            if seen_initials.get(ikey, -1) < freq:
                seen_initials[ikey] = freq
    return seen_main, seen_initials


def write_index(path, entries, label):
    """entries: dict (code, text) -> freq -> sorted binary file."""
    ordered = sorted(
        ((code, -freq, text, freq) for (code, text), freq in entries.items()),
        key=lambda row: (row[0], row[1], row[2]),
    )
    blob = bytearray()
    records = []
    previous_code = None
    previous_offset = 0
    previous_len = 0
    for code, _neg, text, freq in ordered:
        code_bytes = code.encode("utf-8")
        text_bytes = text.encode("utf-8")
        if code == previous_code:
            code_off, code_len = previous_offset, previous_len
        else:
            code_off = len(blob)
            blob.extend(code_bytes)
            previous_code, previous_offset, previous_len = (
                code, code_off, len(code_bytes)
            )
            code_off, code_len = previous_offset, previous_len
        text_off = len(blob)
        blob.extend(text_bytes)
        if code_len > 65535 or len(text_bytes) > 65535:
            continue
        records.append(
            ENTRY_STRUCT.pack(code_off, text_off, freq, code_len, len(text_bytes))
        )

    header = HEADER_STRUCT.pack(
        MAGIC, VERSION, len(records), len(records) * ENTRY_STRUCT.size,
        len(blob), 0, 0,
    )
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as out:
        out.write(header)
        out.writelines(records)
        out.write(blob)
    print("  %-24s %8d entries  %6.1f MiB" % (
        label, len(records), (len(header) + len(records) * ENTRY_STRUCT.size +
                              len(blob)) / 1048576.0), file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True,
                        help="pinyin.dict.db.zip or an extracted .db")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--min-frequency", type=int, default=100,
                        help="keep multi-character words at/above this weight "
                             "(single characters are always kept)")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    db_path, tmpdir = load_sqlite(args.source)
    try:
        connection = sqlite3.connect(db_path)
        syllables = build_syllable_data(connection)
        print("legal syllables: %d" % len(syllables), file=sys.stderr)
        syllable_tuple = tuple(syllables)
        main_entries, initials_entries = collect(
            connection, args.min_frequency, syllable_tuple, not args.quiet
        )
        connection.close()

        out = args.output_dir
        write_index(os.path.join(out, "zhiyi.dict.bin"), main_entries,
                    "zhiyi.dict.bin")
        write_index(os.path.join(out, "zhiyi.initials.bin"), initials_entries,
                    "zhiyi.initials.bin")
        with open(os.path.join(out, "zhiyi.syllables"), "w", encoding="utf-8") as f:
            f.write("\n".join(syllables) + "\n")
        print("dictionaries written to %s" % out, file=sys.stderr)
    finally:
        if tmpdir:
            import shutil
            shutil.rmtree(tmpdir, ignore_errors=True)
            try:
                os.rmdir(os.path.dirname(db_path))
            except OSError:
                pass


if __name__ == "__main__":
    main()
