#!/usr/bin/env python3
"""Reference AGI picture renderer (visual + priority), independent of the C engine.

Fill semantics follow ScummVM's GfxPicture: when the visual screen is enabled the
visual screen alone bounds the fill (priority is painted wherever it goes); a
priority-only fill is bounded by priority != 4.
"""
import sys, struct
from collections import deque

def read_dir(path, resno):
    d = open(path, 'rb').read()
    off = resno * 3
    b0, b1, b2 = d[off], d[off+1], d[off+2]
    if (b0, b1, b2) == (0xFF, 0xFF, 0xFF):
        raise SystemExit('resource not present')
    vol = b0 >> 4
    pos = ((b0 & 0x0F) << 16) | (b1 << 8) | b2
    return vol, pos

def read_res(gamedir, dirfile, resno):
    import os
    def find(name):
        for f in os.listdir(gamedir):
            if f.lower() == name:
                return os.path.join(gamedir, f)
        raise SystemExit('missing ' + name)
    vol, pos = read_dir(find(dirfile), resno)
    data = open(find('vol.%d' % vol), 'rb').read()
    sig = struct.unpack_from('<H', data, pos)[0]
    assert sig == 0x3412, hex(sig)
    size = struct.unpack_from('<H', data, pos + 3)[0]
    return data[pos + 5: pos + 5 + size]

W, H = 160, 168

class Pic:
    def __init__(self):
        self.vis = bytearray([15]) * 0 or bytearray([15] * (W * H))
        self.pri = bytearray([4] * (W * H))
        self.vis_on = False
        self.pri_on = False
        self.vis_color = 0
        self.pri_color = 0

    def pset(self, x, y):
        if not (0 <= x < W and 0 <= y < H):
            return
        if self.vis_on:
            self.vis[y * W + x] = self.vis_color
        if self.pri_on:
            self.pri[y * W + x] = self.pri_color

    def line(self, x1, y1, x2, y2):
        dx, dy = abs(x2 - x1), -abs(y2 - y1)
        sx = 1 if x1 < x2 else -1
        sy = 1 if y1 < y2 else -1
        err = dx + dy
        while True:
            self.pset(x1, y1)
            if x1 == x2 and y1 == y2:
                break
            e2 = err * 2
            if e2 >= dy:
                err += dy; x1 += sx
            if e2 <= dx:
                err += dx; y1 += sy

    def fill_check(self, x, y):
        """True = boundary (stop)."""
        if not (0 <= x < W and 0 <= y < H):
            return True
        if self.pri_on and not self.vis_on:
            return self.pri[y * W + x] != 4
        return self.vis[y * W + x] != 15

    def fill(self, x, y):
        if not self.vis_on and not self.pri_on:
            return
        if self.vis_on and self.vis_color == 15:
            return
        q = deque([(x, y)])
        while q:
            x, y = q.popleft()
            if self.fill_check(x, y):
                continue
            self.pset(x, y)
            if x > 0:     q.append((x - 1, y))
            if x < W - 1: q.append((x + 1, y))
            if y > 0:     q.append((x, y - 1))
            if y < H - 1: q.append((x, y + 1))

def draw(data):
    p = Pic()
    i = 0
    n = len(data)
    while i < n:
        op = data[i]; i += 1
        if op == 0xF0:
            p.vis_on = True; p.vis_color = data[i]; i += 1
        elif op == 0xF1:
            p.vis_on = False
        elif op == 0xF2:
            p.pri_on = True; p.pri_color = data[i]; i += 1
        elif op == 0xF3:
            p.pri_on = False
        elif op in (0xF4, 0xF5):           # Y-corner / X-corner
            x1, y1 = data[i], data[i+1]; i += 2
            p.pset(x1, y1)
            y_first = (op == 0xF4)
            while i < n and data[i] < 0xF0:
                if y_first:
                    y2 = data[i]; i += 1
                    p.line(x1, y1, x1, y2); y1 = y2
                    if i >= n or data[i] >= 0xF0: break
                    x2 = data[i]; i += 1
                    p.line(x1, y1, x2, y1); x1 = x2
                else:
                    x2 = data[i]; i += 1
                    p.line(x1, y1, x2, y1); x1 = x2
                    if i >= n or data[i] >= 0xF0: break
                    y2 = data[i]; i += 1
                    p.line(x1, y1, x1, y2); y1 = y2
        elif op == 0xF6:                   # absolute line
            x1, y1 = data[i], data[i+1]; i += 2
            p.pset(x1, y1)
            while i < n and data[i] < 0xF0:
                x2, y2 = data[i], data[i+1]; i += 2
                p.line(x1, y1, x2, y2); x1, y1 = x2, y2
        elif op == 0xF7:                   # relative line
            x1, y1 = data[i], data[i+1]; i += 2
            p.pset(x1, y1)
            while i < n and data[i] < 0xF0:
                d = data[i]; i += 1
                xd = (d >> 4) & 7
                if d & 0x80: xd = -xd
                yd = d & 7
                if d & 8: yd = -yd
                x2, y2 = x1 + xd, y1 + yd
                p.line(x1, y1, x2, y2); x1, y1 = x2, y2
        elif op == 0xF8:                   # fill
            while i < n and data[i] < 0xF0:
                x, y = data[i], data[i+1]; i += 2
                p.fill(x, y)
        elif op == 0xF9:                   # set pen
            i += 1
        elif op == 0xFA:                   # plot pen
            while i < n and data[i] < 0xF0:
                i += 2
        elif op == 0xFF:
            break
        else:
            raise SystemExit('unknown opcode %02X at %d' % (op, i - 1))
    return p

if __name__ == '__main__':
    gamedir, picno, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    p = draw(read_res(gamedir, 'picdir', picno))
    with open(out, 'wb') as f:
        for k in range(W * H):
            f.write(bytes([p.vis[k], p.pri[k]]))
