#!/usr/bin/env python3
"""
send-firmware.py - upload a slot image (from prepare-firmware.py) over USART1.

    scripts/send-firmware.py /dev/ttyUSB0 app_b_v1.2.0.img
    scripts/send-firmware.py /dev/ttyUSB0 --info

Protocol: see app/inc/update_proto.h. Stop-and-wait with retransmission; telemetry JSON
lines that are interleaved on the same UART are ignored by the frame decoder.
The device must be awake (USART RX cannot wake the MCU from STOP): press the user button or
wait for the next 5 s RTC wake-up - the tool keeps sending INFO until the device answers.
"""
import argparse
import struct
import sys
import time
import zlib

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    sys.exit("pip install pyserial")

SOF = b"\xA5\x5A"
START, DATA, END, ABORT, INFO, ACK, NAK = 0x01, 0x02, 0x03, 0x04, 0x05, 0x80, 0x81
STATUS = ["ok", "bad state", "bad header", "wrong slot", "bad size", "bad offset",
          "flash error", "verify failed", "frame crc"]
HEADER_LEN, HEADER_AREA = 128, 0x200
CHUNK = 1024 - 4


def encode(ftype, seq, payload=b""):
    body = struct.pack("<BBH", ftype, seq & 0xFF, len(payload)) + payload
    return SOF + body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)


def read_frame(port, timeout):
    """Return (type, seq, payload) or None. Skips non-frame bytes (telemetry text)."""
    deadline = time.monotonic() + timeout
    state = 0
    while time.monotonic() < deadline:
        b = port.read(1)
        if not b:
            continue
        if state == 0:
            state = 1 if b == b"\xA5" else 0
            continue
        if state == 1:
            if b != b"\x5A":
                state = 1 if b == b"\xA5" else 0
                continue
            hdr = port.read(4)
            if len(hdr) < 4:
                return None
            ftype, seq, length = struct.unpack("<BBH", hdr)
            rest = port.read(length + 4)
            if len(rest) < length + 4:
                return None
            payload, crc = rest[:length], struct.unpack("<I", rest[length:])[0]
            if zlib.crc32(hdr + payload) & 0xFFFFFFFF != crc:
                state = 0
                continue
            return ftype, seq, payload
    return None


class Link:
    def __init__(self, port):
        self.port = port
        self.seq = 0

    def transact(self, ftype, payload=b"", timeout=2.0, retries=5):
        self.seq = (self.seq + 1) & 0xFF
        for attempt in range(retries):
            self.port.write(encode(ftype, self.seq, payload))
            while True:
                fr = read_frame(self.port, timeout)
                if fr is None:
                    break                       # timeout -> retransmit
                rtype, rseq, rpayload = fr
                if rseq != self.seq:
                    continue                    # stale reply to an earlier attempt
                if rtype == NAK and rpayload and rpayload[0] == 8:
                    break                       # frame CRC error on device -> retransmit
                return rtype, rpayload
        raise RuntimeError(f"no response to frame type 0x{ftype:02x} after {retries} attempts")


def status_of(rtype, payload):
    st, arg = payload[0], struct.unpack_from("<I", payload, 1)[0]
    return rtype == ACK and st == 0, STATUS[st] if st < len(STATUS) else str(st), arg


def show_info(link):
    rtype, p = link.transact(INFO, timeout=1.0, retries=12)   # ~12 s: covers a STOP period
    slot, maj, mnr, pat, trial, active, pending, event = p[:8]
    boots, = struct.unpack_from("<I", p, 8)
    rollbacks, = struct.unpack_from("<H", p, 12)
    pend = "-" if pending == 0xFF else "AB"[pending]
    print(f"running slot {'AB'[slot]} v{maj}.{mnr}.{pat} {'(trial)' if trial else ''}| "
          f"journal: active={'AB'[active]} pending={pend} last_event={event} "
          f"boots={boots} rollbacks={rollbacks}")
    return slot


def upload(link, image):
    if len(image) <= HEADER_AREA:
        raise SystemExit("image too small")
    header, body = image[:HEADER_LEN], image[HEADER_AREA:]
    target = header[6]
    print(f"image for slot {'AB'[target]}, {len(body)} bytes")

    ok, why, arg = status_of(*link.transact(START, header, timeout=15.0))   # erase takes seconds
    if not ok:
        raise SystemExit(f"START rejected: {why} ({arg:#x})")

    body += b"\xFF" * (-len(body) % 4)
    t0 = time.monotonic()
    for off in range(0, len(body), CHUNK):
        chunk = body[off:off + CHUNK]
        ok, why, arg = status_of(*link.transact(DATA, struct.pack("<I", off) + chunk))
        if not ok:
            link.transact(ABORT)
            raise SystemExit(f"DATA at {off:#x} rejected: {why} ({arg:#x})")
        pct = 100 * (off + len(chunk)) // len(body)
        print(f"\r  {pct:3d}%  {off + len(chunk):7d} B", end="", flush=True)
    print(f"  ({time.monotonic() - t0:.1f} s)")

    ok, why, arg = status_of(*link.transact(END, timeout=5.0))
    if not ok:
        raise SystemExit(f"END rejected: {why} ({arg:#x})")
    print(f"staged v{arg >> 24}.{(arg >> 16) & 0xFF}.{arg & 0xFFFF}; device resets into a trial boot.")
    print("It must run 10 s healthy to be confirmed, otherwise the bootloader rolls back.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("image", nargs="?")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--info", action="store_true", help="only query running slot / boot state")
    args = ap.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.05) as port:
        link = Link(port)
        running = show_info(link)
        if args.info or not args.image:
            return
        with open(args.image, "rb") as f:
            image = f.read()
        if image[6] == running:
            raise SystemExit("image is linked for the running slot; build it for the other slot")
        upload(link, image)
        time.sleep(12)
        show_info(link)


if __name__ == "__main__":
    main()
