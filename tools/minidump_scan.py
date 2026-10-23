# SPDX-License-Identifier: MIT
"""Crude crash triage for Windows minidumps without a debugger.

Prints the exception, the faulting module+offset, the faulting thread's RIP/RSP, and a scan of
that thread's stack for values that point into loaded modules (likely return addresses, so
read them as "probably on the call stack", not as an exact backtrace).

Usage: python -I tools/minidump_scan.py <file.dmp> [--depth N]
"""

import argparse
import struct
import sys

STREAM_THREAD_LIST = 3
STREAM_MODULE_LIST = 4
STREAM_MEMORY_LIST = 5
STREAM_EXCEPTION = 6
STREAM_MEMORY64_LIST = 9

CTX_RSP = 0x98
CTX_RIP = 0xF8


class Dump:
    def __init__(self, data):
        self.data = data
        sig, _ver, count, dir_rva = struct.unpack_from("<4sIII", data, 0)
        if sig != b"MDMP":
            raise ValueError("not a minidump")
        self.streams = {}
        for i in range(count):
            stype, size, rva = struct.unpack_from("<III", data, dir_rva + i * 12)
            self.streams[stype] = (rva, size)
        self.modules = self._modules()
        self.memory = self._memory()

    def _string(self, rva):
        (length,) = struct.unpack_from("<I", self.data, rva)
        return self.data[rva + 4 : rva + 4 + length].decode("utf-16-le", "replace")

    def _modules(self):
        mods = []
        if STREAM_MODULE_LIST not in self.streams:
            return mods
        rva, _ = self.streams[STREAM_MODULE_LIST]
        (count,) = struct.unpack_from("<I", self.data, rva)
        for i in range(count):
            off = rva + 4 + i * 108
            base, size = struct.unpack_from("<QI", self.data, off)
            (name_rva,) = struct.unpack_from("<I", self.data, off + 20)
            name = self._string(name_rva).split("\\")[-1]
            mods.append((base, base + size, name))
        return sorted(mods)

    def _memory(self):
        ranges = []
        if STREAM_MEMORY64_LIST in self.streams:
            rva, _ = self.streams[STREAM_MEMORY64_LIST]
            count, base_rva = struct.unpack_from("<QQ", self.data, rva)
            pos = base_rva
            for i in range(count):
                start, size = struct.unpack_from("<QQ", self.data, rva + 16 + i * 16)
                ranges.append((start, size, pos))
                pos += size
        if STREAM_MEMORY_LIST in self.streams:
            rva, _ = self.streams[STREAM_MEMORY_LIST]
            (count,) = struct.unpack_from("<I", self.data, rva)
            for i in range(count):
                start, size, data_rva = struct.unpack_from("<QII", self.data, rva + 4 + i * 16)
                ranges.append((start, size, data_rva))
        return ranges

    def read(self, address, size):
        for start, length, rva in self.memory:
            if start <= address and address + size <= start + length:
                off = rva + (address - start)
                return self.data[off : off + size]
        return None

    def module_of(self, address):
        for base, end, name in self.modules:
            if base <= address < end:
                return f"{name}+0x{address - base:x}"
        return None

    def threads(self):
        out = {}
        if STREAM_THREAD_LIST not in self.streams:
            return out
        rva, _ = self.streams[STREAM_THREAD_LIST]
        (count,) = struct.unpack_from("<I", self.data, rva)
        for i in range(count):
            off = rva + 4 + i * 48
            tid = struct.unpack_from("<I", self.data, off)[0]
            stack_start, stack_size, stack_rva = struct.unpack_from("<QII", self.data, off + 24)
            ctx_size, ctx_rva = struct.unpack_from("<II", self.data, off + 40)
            out[tid] = (stack_start, stack_size, stack_rva, ctx_rva)
        return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dump")
    parser.add_argument("--depth", type=int, default=40, help="module hits to print from the stack scan")
    args = parser.parse_args()

    with open(args.dump, "rb") as f:
        dump = Dump(f.read())

    if STREAM_EXCEPTION not in dump.streams:
        print("no exception stream")
        return 1
    rva, _ = dump.streams[STREAM_EXCEPTION]
    tid = struct.unpack_from("<I", dump.data, rva)[0]
    code, flags, _rec, address, nparams = struct.unpack_from("<IIQQI", dump.data, rva + 8)
    params = struct.unpack_from(f"<{min(nparams, 15)}Q", dump.data, rva + 8 + 32) if nparams else ()
    ctx_size, ctx_rva = struct.unpack_from("<II", dump.data, rva + 8 + 32 + 15 * 8)

    print(f"exception 0x{code:08x} (flags 0x{flags:x}) on thread {tid}")
    print(f"  at 0x{address:x}  {dump.module_of(address) or '(no module)'}")
    if code == 0xC0000005 and len(params) >= 2:
        kind = {0: "read", 1: "write", 8: "execute"}.get(params[0], str(params[0]))
        print(f"  access violation: {kind} of 0x{params[1]:x}")
    elif params:
        print("  parameters: " + ", ".join(f"0x{p:x}" for p in params))

    rsp, = struct.unpack_from("<Q", dump.data, ctx_rva + CTX_RSP)
    rip, = struct.unpack_from("<Q", dump.data, ctx_rva + CTX_RIP)
    print(f"  RIP 0x{rip:x} {dump.module_of(rip) or ''}  RSP 0x{rsp:x}")

    thread = dump.threads().get(tid)
    stack = None
    if thread:
        start, size, data_rva, _ = thread
        if start <= rsp < start + size:
            stack = dump.data[data_rva + (rsp - start) : data_rva + size]
    if stack is None:
        stack = dump.read(rsp, 0x4000) or b""
    print(f"\nstack scan from RSP ({len(stack)} bytes), values pointing into modules:")
    hits = 0
    for i in range(0, len(stack) - 7, 8):
        (value,) = struct.unpack_from("<Q", stack, i)
        where = dump.module_of(value)
        if where:
            print(f"  [rsp+0x{i:04x}] {where}")
            hits += 1
            if hits >= args.depth:
                break

    print("\nmodules of interest:")
    for base, end, name in dump.modules:
        lowered = name.lower()
        if any(k in lowered for k in ("mevr", "masseffect", "bink", "oculus", "openxr", "runtimeipc", "libovr")):
            print(f"  0x{base:x}-0x{end:x}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
