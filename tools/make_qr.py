# Makes the QR code of the Bluetooth app for a sticker on the clock:
#   docs/images/app-qr.png          the code alone
#   docs/images/app-qr-sticker.png  the code with a caption, ready to print
# Run: python tools/make_qr.py      (needs Pillow; the QR encoder itself is below, no QR library)
#
# The encoder follows the QR specification (ISO/IEC 18004) the way Project Nayuki's reference
# implementation does: byte mode, error correction level Q (25% of the code may be damaged), the
# smallest version that fits, and the mask with the lowest penalty. selftest() checks it against a
# published example and the standard's format-bit table, and the result is read back before saving.
import os

URL = "https://amir684.github.io/esp32-matrix-clock/"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "images")

# ---------------- tables (index = version, 1..10 is plenty for a URL) ----------------
ECL_BITS = {"L": 1, "M": 0, "Q": 3, "H": 2}
ECC_PER_BLOCK = {
    "L": [-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18],
    "M": [-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26],
    "Q": [-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24],
    "H": [-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28],
}
NUM_BLOCKS = {
    "L": [-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4],
    "M": [-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5],
    "Q": [-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8],
    "H": [-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8],
}


def raw_modules(ver):
    r = (16 * ver + 128) * ver + 64
    if ver >= 2:
        n = ver // 7 + 2
        r -= (25 * n - 10) * n - 55
        if ver >= 7:
            r -= 36
    return r


def data_codewords(ver, ecl):
    return raw_modules(ver) // 8 - ECC_PER_BLOCK[ecl][ver] * NUM_BLOCKS[ecl][ver]


# ---------------- Reed-Solomon over GF(256), polynomial 0x11D ----------------
def gf_mul(x, y):
    z = 0
    for i in reversed(range(8)):
        z = (z << 1) ^ ((z >> 7) * 0x11D)
        z ^= ((y >> i) & 1) * x
    return z


def rs_divisor(degree):
    res = [0] * (degree - 1) + [1]
    root = 1
    for _ in range(degree):
        for j in range(degree):
            res[j] = gf_mul(res[j], root)
            if j + 1 < degree:
                res[j] ^= res[j + 1]
        root = gf_mul(root, 2)
    return res


def rs_remainder(data, divisor):
    res = [0] * len(divisor)
    for b in data:
        f = b ^ res.pop(0)
        res.append(0)
        for i, c in enumerate(divisor):
            res[i] ^= gf_mul(c, f)
    return res


# ---------------- encoder ----------------
def format_bits(ecl, mask):
    data = ECL_BITS[ecl] << 3 | mask
    rem = data
    for _ in range(10):
        rem = (rem << 1) ^ ((rem >> 9) * 0x537)
    return (data << 10 | rem) ^ 0x5412


def alignment_positions(ver):
    if ver == 1:
        return []
    n = ver // 7 + 2
    step = (ver * 4 + n * 2 + 1) // (n * 2 - 2) * 2
    size = ver * 4 + 17
    return [6] + sorted(size - 7 - i * step for i in range(n - 1))


def interleave(ver, ecl, data):
    nb, ecc = NUM_BLOCKS[ecl][ver], ECC_PER_BLOCK[ecl][ver]
    raw = raw_modules(ver) // 8
    short = nb - raw % nb
    short_len = raw // nb
    div = rs_divisor(ecc)
    blocks, k = [], 0
    for i in range(nb):
        n = short_len - ecc + (0 if i < short else 1)
        d = data[k:k + n]
        k += n
        e = rs_remainder(d, div)
        if i < short:
            d = d + [0]
        blocks.append(d + e)
    out = []
    for i in range(len(blocks[0])):
        for j, b in enumerate(blocks):
            if i != short_len - ecc or j >= short:
                out.append(b[i])
    return out


class QR:
    def __init__(self, text, ecl="Q"):
        raw = text.encode("utf-8")
        for ver in range(1, 11):
            if len(raw) <= (data_codewords(ver, ecl) * 8 - 12) // 8:
                break
        else:
            raise ValueError("text too long")
        self.ver, self.ecl, self.size = ver, ecl, ver * 4 + 17
        cap = data_codewords(ver, ecl)
        bits = [0, 1, 0, 0] + [(len(raw) >> i) & 1 for i in reversed(range(8))]  # byte mode, 8-bit count
        for b in raw:
            bits += [(b >> i) & 1 for i in reversed(range(8))]
        bits += [0] * min(4, cap * 8 - len(bits))
        bits += [0] * (-len(bits) % 8)
        cw = [int("".join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8)]
        pad = 0xEC
        while len(cw) < cap:
            cw.append(pad)
            pad ^= 0xEC ^ 0x11
        self.data = cw
        self.codewords = interleave(ver, ecl, cw)
        best = None
        for mask in range(8):
            m = self.build(mask)
            p = penalty(m)
            if best is None or p < best[0]:
                best = (p, mask, m)
        self.mask, self.modules = best[1], best[2]

    def build(self, mask):
        n = self.size
        mod = [[False] * n for _ in range(n)]
        fn = [[False] * n for _ in range(n)]

        def put(x, y, dark):
            mod[y][x] = dark
            fn[y][x] = True

        for i in range(n):
            put(6, i, i % 2 == 0)
            put(i, 6, i % 2 == 0)
        for cx, cy in ((3, 3), (n - 4, 3), (3, n - 4)):
            for dy in range(-4, 5):
                for dx in range(-4, 5):
                    x, y = cx + dx, cy + dy
                    if 0 <= x < n and 0 <= y < n:
                        put(x, y, max(abs(dx), abs(dy)) not in (2, 4))
        al = alignment_positions(self.ver)
        for i, ax in enumerate(al):
            for j, ay in enumerate(al):
                if (i, j) in ((0, 0), (0, len(al) - 1), (len(al) - 1, 0)):
                    continue
                for dy in range(-2, 3):
                    for dx in range(-2, 3):
                        put(ax + dx, ay + dy, max(abs(dx), abs(dy)) != 1)
        self.draw_format(put, mask)
        # data, in two-column zigzag from the bottom right
        i, total = 0, len(self.codewords) * 8
        right = n - 1
        while right >= 1:
            if right == 6:
                right = 5
            for vert in range(n):
                for j in range(2):
                    x = right - j
                    upward = ((right + 1) & 2) == 0
                    y = n - 1 - vert if upward else vert
                    if not fn[y][x] and i < total:
                        mod[y][x] = (self.codewords[i >> 3] >> (7 - (i & 7))) & 1 == 1
                        i += 1
            right -= 2
        for y in range(n):
            for x in range(n):
                if not fn[y][x] and mask_bit(mask, x, y):
                    mod[y][x] = not mod[y][x]
        return mod

    def draw_format(self, put, mask):
        n, b = self.size, format_bits(self.ecl, mask)
        bit = lambda i: (b >> i) & 1 == 1
        for i in range(6):
            put(8, i, bit(i))
        put(8, 7, bit(6))
        put(8, 8, bit(7))
        put(7, 8, bit(8))
        for i in range(9, 15):
            put(14 - i, 8, bit(i))
        for i in range(8):
            put(n - 1 - i, 8, bit(i))
        for i in range(8, 15):
            put(8, n - 15 + i, bit(i))
        put(8, n - 8, True)  # the dark module


def mask_bit(mask, x, y):
    return [
        (x + y) % 2 == 0, y % 2 == 0, x % 3 == 0, (x + y) % 3 == 0,
        (x // 3 + y // 2) % 2 == 0, x * y % 2 + x * y % 3 == 0,
        (x * y % 2 + x * y % 3) % 2 == 0, ((x + y) % 2 + x * y % 3) % 2 == 0,
    ][mask]


def penalty(m):
    n, p = len(m), 0
    lines = [row for row in m] + [[m[y][x] for y in range(n)] for x in range(n)]
    finder = [True, False, True, True, True, False, True]
    for line in lines:
        run = 1
        for i in range(1, n + 1):
            if i < n and line[i] == line[i - 1]:
                run += 1
            else:
                if run >= 5:
                    p += 3 + run - 5
                run = 1
        for i in range(n - 6):
            if line[i:i + 7] == finder:
                before = line[max(0, i - 4):i]
                after = line[i + 7:i + 11]
                if (len(before) < 4 or not any(before)) or (len(after) < 4 or not any(after)):
                    p += 40
    for y in range(n - 1):
        for x in range(n - 1):
            if m[y][x] == m[y][x + 1] == m[y + 1][x] == m[y + 1][x + 1]:
                p += 3
    dark = sum(map(sum, m))
    total = n * n
    k = (abs(dark * 20 - total * 10) + total - 1) // total - 1
    return p + k * 10


# ---------------- reading it back ----------------
def decode(m):
    """Reads a module matrix the way a scanner would after locating it: format bits, unmask,
    zigzag, de-interleave, Reed-Solomon check, byte-mode text."""
    n = len(m)
    ver = (n - 17) // 4
    fb = 0
    for i in range(6):
        fb |= m[i][8] << i
    fb |= m[7][8] << 6
    fb |= m[8][8] << 7
    fb |= m[8][7] << 8
    for i in range(9, 15):
        fb |= m[8][14 - i] << i
    match = [(e, k) for e in ECL_BITS for k in range(8) if format_bits(e, k) == fb]
    assert match, "format bits do not match any level/mask"
    ecl, mask = match[0]
    probe = QR.__new__(QR)
    probe.ver, probe.ecl, probe.size, probe.codewords = ver, ecl, n, []
    fn = [[False] * n for _ in range(n)]
    probe.build_fn = None
    # function-pattern map: build with no data and record which cells were set as function modules
    marks = [[False] * n for _ in range(n)]

    def put(x, y, dark):
        marks[y][x] = True

    for i in range(n):
        put(6, i, 0)
        put(i, 6, 0)
    for cx, cy in ((3, 3), (n - 4, 3), (3, n - 4)):
        for dy in range(-4, 5):
            for dx in range(-4, 5):
                if 0 <= cx + dx < n and 0 <= cy + dy < n:
                    put(cx + dx, cy + dy, 0)
    al = alignment_positions(ver)
    for i, ax in enumerate(al):
        for j, ay in enumerate(al):
            if (i, j) in ((0, 0), (0, len(al) - 1), (len(al) - 1, 0)):
                continue
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    put(ax + dx, ay + dy, 0)
    probe.draw_format(put, mask)
    bits = []
    right = n - 1
    while right >= 1:
        if right == 6:
            right = 5
        for vert in range(n):
            for j in range(2):
                x = right - j
                y = n - 1 - vert if ((right + 1) & 2) == 0 else vert
                if not marks[y][x]:
                    bits.append(int(m[y][x] ^ mask_bit(mask, x, y)))
        right -= 2
    raw = [int("".join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits) // 8 * 8, 8)]
    # de-interleave and check every block's Reed-Solomon remainder
    nb, ecc = NUM_BLOCKS[ecl][ver], ECC_PER_BLOCK[ecl][ver]
    total = raw_modules(ver) // 8
    short = nb - total % nb
    short_len = total // nb
    lens = [short_len - ecc + (0 if i < short else 1) for i in range(nb)]
    blocks = [[] for _ in range(nb)]
    idx = 0
    for i in range(max(lens)):
        for j in range(nb):
            if i < lens[j]:
                blocks[j].append(raw[idx])
                idx += 1
    for i in range(ecc):
        for j in range(nb):
            blocks[j].append(raw[idx])
            idx += 1
    data = []
    div = rs_divisor(ecc)
    for j, b in enumerate(blocks):
        d, e = b[:lens[j]], b[lens[j]:]
        assert rs_remainder(d, div) == e, "Reed-Solomon check failed"
        data += d
    bits = "".join(format(c, "08b") for c in data)
    assert bits[:4] == "0100", "not byte mode"
    length = int(bits[4:12], 2)
    return bytes(int(bits[12 + i * 8:20 + i * 8], 2) for i in range(length)).decode("utf-8"), ver, ecl, mask


def selftest():
    # Error-correction codewords of the "HELLO WORLD" 1-M example (thonky.com QR tutorial)
    data = [32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17]
    assert rs_remainder(data, rs_divisor(10)) == [196, 35, 39, 119, 235, 215, 231, 226, 93, 23], "RS"
    # Format information strings from the standard's table
    assert format(format_bits("M", 0), "015b") == "101010000010010"
    assert format(format_bits("Q", 0), "015b") == "011010101011111"
    assert format(format_bits("L", 0), "015b") == "111011111000100"
    assert format(format_bits("H", 0), "015b") == "001011010001001"
    assert alignment_positions(4) == [6, 26] and alignment_positions(7) == [6, 22, 38]


# ---------------- images ----------------
def heb(s):
    return s[::-1]  # Pillow without libraqm lays text out left to right


def main():
    from PIL import Image, ImageDraw, ImageFont

    selftest()
    qr = QR(URL, "Q")
    text, ver, ecl, mask = decode(qr.modules)
    assert text == URL, text
    print("QR version %d, level %s, mask %d, %dx%d modules, reads back OK" % (ver, ecl, mask, qr.size, qr.size))

    px, quiet = 24, 4
    side = (qr.size + 2 * quiet) * px
    code = Image.new("L", (side, side), 255)
    d = ImageDraw.Draw(code)
    for y, row in enumerate(qr.modules):
        for x, dark in enumerate(row):
            if dark:
                d.rectangle([(x + quiet) * px, (y + quiet) * px, (x + quiet + 1) * px - 1, (y + quiet + 1) * px - 1], 0)
    os.makedirs(OUT, exist_ok=True)
    code.save(os.path.join(OUT, "app-qr.png"), dpi=(600, 600))

    # Sticker: the code, then the name and a line of instructions. 1000 px wide at 700 dpi = 3.6 cm.
    fonts = "C:/Windows/Fonts/"
    bold = ImageFont.truetype(fonts + "arialbd.ttf", 92)
    small = ImageFont.truetype(fonts + "arial.ttf", 50)
    W = 1000
    qr_img = code.resize((W - 60, W - 60), Image.NEAREST)
    H = qr_img.height + 250
    st = Image.new("RGB", (W, H), "white")
    st.paste(qr_img, (30, 20))
    sd = ImageDraw.Draw(st)
    for txt, font, y in ((heb("שעון מטריצה"), bold, qr_img.height + 20),
                         (heb("סרקו להתקנת האפליקציה"), small, qr_img.height + 140)):
        w = sd.textlength(txt, font=font)
        sd.text(((W - w) / 2, y), txt, font=font, fill="black")
    sd.rounded_rectangle([4, 4, W - 5, H - 5], radius=40, outline=(190, 190, 190), width=4)  # cutting guide
    st.save(os.path.join(OUT, "app-qr-sticker.png"), dpi=(700, 700))
    print("saved docs/images/app-qr.png and docs/images/app-qr-sticker.png")


if __name__ == "__main__":
    main()
