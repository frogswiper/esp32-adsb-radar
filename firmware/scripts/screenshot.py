# Screenshot over serial WITHOUT resetting the ESP32: raw fd, no modem-line changes (run `stty -F /dev/ttyACM0 115200 raw -echo -hupcl` first)
import os, sys, time, struct, zlib, select
fd = os.open('/dev/ttyACM0', os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
def readline(timeout=2.0):
    buf = b''; t0 = time.time()
    while time.time() - t0 < timeout:
        r, _, _ = select.select([fd], [], [], 0.2)
        if not r: continue
        try: c = os.read(fd, 1)
        except BlockingIOError: continue
        if not c: continue
        if c == b'\n': return buf
        buf += c
    return buf
def readn(n, timeout=60):
    data = b''; t0 = time.time()
    while len(data) < n and time.time() - t0 < timeout:
        r, _, _ = select.select([fd], [], [], 0.5)
        if not r: continue
        try: data += os.read(fd, min(65536, n - len(data)))
        except BlockingIOError: pass
    return data
def drain():
    while True:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r: break
        try: os.read(fd, 65536)
        except BlockingIOError: break
def snap(name):
    drain(); os.write(fd, b'S')
    hdr = b''; t0 = time.time()
    while time.time() - t0 < 10:
        l = readline()
        if l.startswith(b'SNAP'): hdr = l; break
    if not hdr: print('no header'); return
    _, w, h, n = hdr.split(); w, h, n = int(w), int(h), int(n)
    data = readn(n)
    if len(data) < n: data += b'\0' * (n - len(data))
    rows = []
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            v = data[(y*w+x)*2] | (data[(y*w+x)*2+1] << 8)
            row += bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
        rows.append(bytes(row))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(name, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b''))
    print('saved', name)
time.sleep(float(sys.argv[1]))
for cmd, name in zip(sys.argv[2::2], sys.argv[3::2]):
    os.write(fd, cmd.encode()); time.sleep(1.5); snap(name)
