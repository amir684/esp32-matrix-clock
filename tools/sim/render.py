# frames.txt -> PNG of selected frames stacked, drawn as round LEDs on a dark panel
import sys, zlib, struct
src, out, names = sys.argv[1], sys.argv[2], sys.argv[3].split(",")
lines = open(src).read().split("\n")
frames = {}
i = 0
while i < len(lines):
    if lines[i].startswith("FRAME"):
        _, name, w = lines[i].split(); w = int(w)
        frames[name] = (w, [[tuple(int(l[k*6+j*2:k*6+j*2+2], 16) for j in range(3)) for k in range(w)] for l in lines[i+1:i+9]])
        i += 9
    else: i += 1
sel = [frames[n] for n in names]
P, M, G = 12, 14, 12
W = max(w for w, _ in sel) * P + 2 * M
H = len(sel) * 8 * P + (len(sel) - 1) * G + 2 * M
bg = (10, 11, 15)
img = [[bg] * W for _ in range(H)]
for n, (w, rows) in enumerate(sel):
    oy = M + n * (8 * P + G); ox = M + (W - 2 * M - w * P) // 2
    for y in range(8):
        for x in range(w):
            c = rows[y][x]
            lit = c != (0, 0, 0)
            cx, cy = ox + x * P + P // 2, oy + y * P + P // 2
            r2 = (P * 0.42) ** 2
            for dy in range(-P // 2, P // 2 + 1):
                for dx in range(-P // 2, P // 2 + 1):
                    d = dx * dx + dy * dy
                    if d <= r2:
                        img[cy + dy][cx + dx] = c if lit else (28, 30, 38)
                    elif lit and d <= r2 * 1.9:  # soft glow
                        f = 0.18
                        o = img[cy + dy][cx + dx]
                        img[cy + dy][cx + dx] = tuple(min(255, int(o[k] + c[k] * f)) for k in range(3))
raw = b"".join(b"\0" + bytes(v for px in row for v in px) for row in img)
def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
open(out, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
print(out, W, H)
