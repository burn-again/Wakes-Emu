#!/usr/bin/env python3
"""Instrument a WebAssembly module so it counts estimated Cortex-M4F cycles.

usage: wasm_cycles.py IN.wasm OUT.wasm --counter SYMBOL:OFFSET [--model cpu.json | --scale S]
       wasm_cycles.py IN.wasm OUT.wasm --profile SYMBOL

Every straight-line run of code (a basic block) starts by adding its estimated cost, in
nRF52840 CPU cycles, to the 32-bit word at COUNTER_SYMBOL + OFFSET in linear memory. The
emulator points that at DWT->CYCCNT, so Wakes' own cycle accounting (its AUD, CPU and
THREADS lines) reads an estimate of what the code would cost on the SP-1.

The cost of a block is the sum of per-instruction weights (CYCLES below), multiplied by
SCALE, which emulator/tools/calibrate_cpu.mjs fits against Wakes' hardware measurements
(docs/PLAITS-ENGINES.md). The weights follow the Cortex-M4F: single-precision FPU ops
take a cycle, divide and square root about 14; there is no double-precision FPU, so every
f64 operation is a software routine; locals live in registers; a taken branch refills
the pipeline. It is a model, and the calibration says how well it holds.

Pure Python, no dependencies. Reads MVP WebAssembly plus sign extension, the 0xFC
saturating truncations and bulk memory, which is what clang 18 emits for wasm32-wasi.
"""
import argparse
import json
import sys

# ---------------------------------------------------------------------- the cycle model
CYCLES = {}


def w(ops, c):
    for op in ops:
        CYCLES[op] = c


w([0x00, 0x01, 0x02, 0x03, 0x0B, 0x1A], 0)        # unreachable nop block loop end drop
w([0x04], 1)                                       # if: compare-and-branch
w([0x05], 2)                                       # else: the then-arm jumps over it
w([0x0C, 0x0D], 2)                                 # br, br_if: pipeline refill
w([0x0E], 5)                                       # br_table: bounds check + table jump
w([0x0F], 3)                                       # return: pop {..., pc}
w([0x10], 5)                                       # call: BL, push/pop
w([0x11], 7)                                       # call_indirect: + table load
w([0x1B, 0x1C], 2)                                 # select: IT block
w([0x20, 0x21, 0x22], 0)                           # locals: registers
w([0x23, 0x24], 1)                                 # globals (the shadow stack pointer)
w(range(0x28, 0x36), 2)                            # loads
w(range(0x36, 0x3F), 1)                            # stores
w([0x3F], 1)
w([0x40], 50)                                      # memory.grow
CONST = {0x41: 0.3, 0x42: 0.6, 0x43: 0.5, 0x44: 1.0}
CYCLES.update(CONST)
w(range(0x45, 0x50), 1)                            # i32 compares
w(range(0x50, 0x5B), 3)                            # i64 compares
w(range(0x5B, 0x61), 2)                            # f32 compares: vcmp + vmrs
w(range(0x61, 0x67), 25)                           # f64 compares: software
w([0x67], 1); w([0x68], 2); w([0x69], 8)           # i32 clz ctz popcnt
w([0x6A, 0x6B, 0x6C], 1)                           # i32 add sub mul
w([0x6D, 0x6E], 7); w([0x6F, 0x70], 8)             # i32 div rem (SDIV 2-12)
w(range(0x71, 0x79), 1)                            # i32 bitwise, shifts
w([0x79], 3); w([0x7A], 4); w([0x7B], 16)          # i64 clz ctz popcnt
w([0x7C, 0x7D], 2); w([0x7E], 4)                   # i64 add sub mul
w(range(0x7F, 0x83), 70)                           # i64 div rem: library
w([0x83, 0x84, 0x85], 2); w(range(0x86, 0x8B), 4)  # i64 bitwise, shifts
w([0x8B, 0x8C], 1)                                 # f32 abs neg
w([0x8D, 0x8E, 0x90], 8); w([0x8F], 4)             # f32 ceil floor nearest; trunc (no VRINT on M4)
w([0x91], 14)                                      # f32 sqrt
w([0x92, 0x93, 0x94], 1)                           # f32 add sub mul
w([0x95], 14)                                      # f32 div
w([0x96, 0x97], 3); w([0x98], 2)                   # f32 min max copysign
w([0x99, 0x9A], 2); w(range(0x9B, 0x9F), 40)       # f64 abs neg; rounding
w([0x9F], 300)                                     # f64 sqrt
w([0xA0, 0xA1, 0xA2], 70); w([0xA3], 200)          # f64 add sub mul div
w([0xA4, 0xA5], 30); w([0xA6], 3)                  # f64 min max copysign
w([0xA7], 0)                                       # i32.wrap_i64
w([0xA8, 0xA9], 1); w([0xAA, 0xAB], 30)            # i32.trunc_f32 / _f64
w([0xAC, 0xAD], 1)                                 # i64.extend_i32
w(range(0xAE, 0xB2), 30)                           # i64.trunc_*
w([0xB2, 0xB3], 1); w([0xB4, 0xB5], 30)            # f32.convert_i32 / _i64
w([0xB6], 25)                                      # f32.demote_f64
w([0xB7, 0xB8], 25); w([0xB9, 0xBA], 40)           # f64.convert_*
w([0xBB], 25)                                      # f64.promote_f32
w(range(0xBC, 0xC0), 1)                            # reinterpret
w(range(0xC0, 0xC5), 1)                            # sign extension
FC_CYCLES = {0: 2, 1: 2, 2: 30, 3: 30, 4: 30, 5: 30, 6: 30, 7: 30, 10: 40, 11: 40}

# The C maths library differs: the wasm build links musl, whose expf/powf/log2f compute in
# double precision (ARM's optimized-routines), while the SP-1's picolibc has true
# single-precision versions. Counting musl's bodies would bill a software double routine
# per call, so these are charged a fixed cost per call instead (single-precision fdlibm on
# a Cortex-M4F, approximately) and their insides are not counted.
LIBM_CALL = {
    'expf': 120, 'exp2f': 100, 'log2f': 110, 'logf': 120, 'log10f': 130, 'powf': 300,
    'sinf': 110, 'cosf': 110, 'tanf': 160, 'atanf': 120, 'atan2f': 160, 'fmodf': 60,
    'roundf': 10, 'floorf': 8, 'ceilf': 8, 'truncf': 6, 'sqrtf': 14, 'frexp': 20,
    'frexpf': 12, 'ldexpf': 12, 'scalbnf': 12,
}
LIBM_QUIET = ('__math_', '__expo2f', '__sindf', '__cosdf', '__tandf', '__rem_pio2f')


# Kinds of work, each with its own weight in the calibrated model (cpu.json "classes"):
# float maths, memory, integer, control flow, the maths library.
FP, MEM, INT, CTL, LIB = range(5)
CLASS_NAMES = ['fp', 'mem', 'int', 'ctl', 'lib']


def class_of(op):
    if isinstance(op, tuple):                      # 0xFC prefix
        return FP if op[1] <= 7 else MEM
    if 0x28 <= op <= 0x40:
        return MEM
    if op in (0x04, 0x05, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11):
        return CTL
    if op in (0x43, 0x44) or 0x5B <= op <= 0x66 or 0x8B <= op <= 0xA6 or \
            0xA8 <= op <= 0xAB or 0xAE <= op <= 0xBF:
        return FP
    return INT


# ---------------------------------------------------------------------- LEB128
def uleb(b, i):
    r = s = 0
    while True:
        x = b[i]
        i += 1
        r |= (x & 0x7F) << s
        s += 7
        if not x & 0x80:
            return r, i


def sleb(b, i):
    r = s = 0
    while True:
        x = b[i]
        i += 1
        r |= (x & 0x7F) << s
        s += 7
        if not x & 0x80:
            if x & 0x40:
                r -= 1 << s
            return r, i


def enc_uleb(v):
    out = bytearray()
    while True:
        x = v & 0x7F
        v >>= 7
        if v:
            out.append(x | 0x80)
        else:
            out.append(x)
            return bytes(out)


def enc_sleb(v):
    out = bytearray()
    while True:
        x = v & 0x7F
        v >>= 7
        done = (v == 0 and not x & 0x40) or (v == -1 and x & 0x40)
        out.append(x if done else x | 0x80)
        if done:
            return bytes(out)


# ---------------------------------------------------------------------- decoding
VALTYPES = {0x7F, 0x7E, 0x7D, 0x7C, 0x7B, 0x70, 0x6F}


def skip_blocktype(b, i):
    if b[i] == 0x40 or b[i] in VALTYPES:
        return i + 1
    _, i = sleb(b, i)
    return i


def next_instr(b, i):
    """(opcode or (0xFC, sub), cost, index after the instruction)."""
    op = b[i]
    i += 1
    if op in (0x02, 0x03, 0x04):
        i = skip_blocktype(b, i)
    elif op in (0x0C, 0x0D, 0x10, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0xD2):
        _, i = uleb(b, i)
    elif op == 0x0E:
        n, i = uleb(b, i)
        for _ in range(n + 1):
            _, i = uleb(b, i)
    elif op == 0x11:
        _, i = uleb(b, i)
        _, i = uleb(b, i)
    elif op == 0x1C:
        n, i = uleb(b, i)
        i += n
    elif 0x28 <= op <= 0x3E:
        _, i = uleb(b, i)
        _, i = uleb(b, i)
    elif op in (0x3F, 0x40):
        i += 1
    elif op == 0x41:
        _, i = sleb(b, i)
    elif op == 0x42:
        _, i = sleb(b, i)
    elif op == 0x43:
        i += 4
    elif op == 0x44:
        i += 8
    elif op == 0xD0:
        i += 1
    elif op == 0xFC:
        sub, i = uleb(b, i)
        if sub == 8:
            _, i = uleb(b, i)
            i += 1
        elif sub in (9, 13, 15, 16, 17):
            _, i = uleb(b, i)
        elif sub == 10:
            i += 2
        elif sub == 11:
            i += 1
        elif sub in (12, 14):
            _, i = uleb(b, i)
            _, i = uleb(b, i)
        return (0xFC, sub), FC_CYCLES.get(sub, 5), i
    elif op in (0xFD, 0xFE):
        raise SystemExit('wasm_cycles: SIMD / atomics not supported (op 0x%02X)' % op)
    elif op not in CYCLES and op not in (0xD1,):
        raise SystemExit('wasm_cycles: unknown opcode 0x%02X' % op)
    return op, CYCLES.get(op, 1), i


# A new basic block starts after these: the start of a block's body, an else, an end, the
# fall-through of a conditional branch, and (dead until the next end/else) after a jump.
LEADER_AFTER = {0x02, 0x03, 0x04, 0x05, 0x0B, 0x0D, 0x0C, 0x0E, 0x0F, 0x00}


def add_to(addr, c):
    """i32.const addr; i32.const addr; i32.load; i32.const c; i32.add; i32.store"""
    a = enc_sleb(addr)
    return (b'\x41' + a + b'\x41' + a + b'\x28\x02\x00' + b'\x41' + enc_sleb(c) +
            b'\x6A' + b'\x36\x02\x00')


PROFILE_UNIT = 8          # a profiling build counts eighths of a cycle


def instrument_body(body, cfg, fixed=None):
    """The body with a counter update at the head of every basic block, or, with `fixed`,
    one update of that many cycles at its entry and nothing inside. cfg: {'addr', 'mult'
    (per-class multipliers, scale included)} or {'profile': base address}."""
    i = 0
    nlocal, i = uleb(body, i)
    for _ in range(nlocal):
        _, i = uleb(body, i)
        i += 1
    out = bytearray(body[:i])
    updates = 0

    def emit(costs):
        nonlocal updates
        if 'profile' in cfg:
            for k, c in enumerate(costs):
                n = int(round(c * PROFILE_UNIT))
                if n > 0:
                    out.extend(add_to(cfg['profile'] + 4 * k, n))
                    updates += 1
        else:
            n = int(round(sum(c * m for c, m in zip(costs, cfg['mult']))))
            if n > 0:
                out.extend(add_to(cfg['addr'], n))
                updates += 1

    if fixed is not None:
        costs = [0.0] * 5
        costs[LIB] = fixed
        emit(costs)
        out.extend(body[i:])
        return bytes(out), 1, updates

    seg = bytearray()
    costs = [0.0] * 5
    blocks = 0
    end = len(body)
    while i < end:
        op, cost, j = next_instr(body, i)
        seg.extend(body[i:j])
        costs[class_of(op)] += cost
        i = j
        if op in LEADER_AFTER and i < end:
            blocks += 1
            emit(costs)
            out.extend(seg)
            seg = bytearray()
            costs = [0.0] * 5
    blocks += 1
    emit(costs)
    out.extend(seg)
    return bytes(out), blocks, updates


# ---------------------------------------------------------------------- the module
def sections(b):
    assert b[:4] == b'\0asm', 'not a wasm module'
    i = 8
    while i < len(b):
        sid = b[i]
        size, j = uleb(b, i + 1)
        yield sid, b[j:j + size], b[i:j + size]
        i = j + size


def function_names(mod):
    """{function index: name} from the 'name' custom section, and the number of imported
    functions (code section entry k is function imported + k)."""
    names, imported = {}, 0
    for sid, payload, _ in sections(mod):
        if sid == 2:
            n, i = uleb(payload, 0)
            for _ in range(n):
                ml, i = uleb(payload, i); i += ml
                nl, i = uleb(payload, i); i += nl
                kind = payload[i]; i += 1
                if kind == 0:
                    _, i = uleb(payload, i)
                    imported += 1
                elif kind == 1:
                    i += 1
                    flags = payload[i]; i += 1
                    _, i = uleb(payload, i)
                    if flags & 1:
                        _, i = uleb(payload, i)
                elif kind == 2:
                    flags = payload[i]; i += 1
                    _, i = uleb(payload, i)
                    if flags & 1:
                        _, i = uleb(payload, i)
                elif kind == 3:
                    i += 2
        elif sid == 0:
            nl, i = uleb(payload, 0)
            if payload[i:i + nl] != b'name':
                continue
            i += nl
            while i < len(payload):
                sub = payload[i]
                size, j = uleb(payload, i + 1)
                if sub == 1:
                    n, k = uleb(payload, j)
                    for _ in range(n):
                        idx, k = uleb(payload, k)
                        ln, k = uleb(payload, k)
                        names[idx] = payload[k:k + ln].decode('utf-8', 'replace')
                        k += ln
                i = j + size
    return names, imported


def counter_address(mod, symbol):
    imported_globals = 0
    for sid, payload, _ in sections(mod):
        if sid == 2:
            n, i = uleb(payload, 0)
            for _ in range(n):
                ml, i = uleb(payload, i); i += ml
                nl, i = uleb(payload, i); i += nl
                kind = payload[i]; i += 1
                if kind == 0:
                    _, i = uleb(payload, i)
                elif kind == 1:
                    i += 1
                    flags = payload[i]; i += 1
                    _, i = uleb(payload, i)
                    if flags & 1:
                        _, i = uleb(payload, i)
                elif kind == 2:
                    flags = payload[i]; i += 1
                    _, i = uleb(payload, i)
                    if flags & 1:
                        _, i = uleb(payload, i)
                elif kind == 3:
                    i += 2
                    imported_globals += 1
    gidx = None
    for sid, payload, _ in sections(mod):
        if sid == 7:
            n, i = uleb(payload, 0)
            for _ in range(n):
                nl, i = uleb(payload, i)
                name = payload[i:i + nl].decode()
                i += nl
                kind = payload[i]; i += 1
                idx, i = uleb(payload, i)
                if name == symbol and kind == 3:
                    gidx = idx
    if gidx is None:
        raise SystemExit('wasm_cycles: %s is not exported as a global (link with --export=%s)'
                         % (symbol, symbol))
    for sid, payload, _ in sections(mod):
        if sid == 6:
            n, i = uleb(payload, 0)
            for g in range(n):
                i += 2                                  # valtype, mutability
                if g + imported_globals == gidx:
                    assert payload[i] == 0x41, 'expected i32.const init'
                    v, _ = sleb(payload, i + 1)
                    return v
                while payload[i] != 0x0B:               # skip the init expression
                    _, _, i = next_instr(payload, i)
                i += 1
    raise SystemExit('wasm_cycles: global %d not found' % gidx)


def main():
    ap = argparse.ArgumentParser(description='count estimated Cortex-M4F cycles in a wasm module')
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--counter', help='SYMBOL:OFFSET of the 32-bit counter (DWT->CYCCNT)')
    ap.add_argument('--scale', type=float, help='one factor for every kind of work')
    ap.add_argument('--model', help='cpu.json: "scale" and per-kind "classes"')
    ap.add_argument('--profile', help='SYMBOL of five counters, one per kind of work')
    a = ap.parse_args()
    mod = open(a.src, 'rb').read()
    if a.profile:
        cfg = {'profile': counter_address(mod, a.profile)}
        desc = 'profile: one counter per kind of work at 0x%x, in 1/%d cycles' % (cfg['profile'], PROFILE_UNIT)
    else:
        sym, off = a.counter.split(':')
        scale, mult = 1.0, [1.0] * 5
        if a.scale is not None:
            scale = a.scale
        elif a.model:
            try:
                model = json.load(open(a.model))
                scale = float(model.get('scale', 1.0))
                cls = model.get('classes', {})
                mult = [float(cls.get(n, 1.0)) for n in CLASS_NAMES]
            except FileNotFoundError:
                print('wasm_cycles: no %s; counting uncalibrated' % a.model, file=sys.stderr)
        cfg = {'addr': counter_address(mod, sym) + int(off), 'mult': [m * scale for m in mult]}
        desc = 'counter at 0x%x, weights %s' % (cfg['addr'], ' '.join(
            '%s=%.3f' % (n, m) for n, m in zip(CLASS_NAMES, cfg['mult'])))
    names, imported = function_names(mod)
    out = bytearray(mod[:8])
    nblocks = nupd = nfun = nfixed = 0
    for sid, payload, raw in sections(mod):
        if sid != 10:
            out.extend(raw)
            continue
        n, i = uleb(payload, 0)
        code = bytearray(enc_uleb(n))
        for k in range(n):
            size, i = uleb(payload, i)
            name = names.get(imported + k, '')
            fixed = LIBM_CALL.get(name)
            if fixed is None and name.startswith(LIBM_QUIET):
                fixed = 0
            nfixed += fixed is not None
            body, blocks, upd = instrument_body(payload[i:i + size], cfg, fixed)
            i += size
            code.extend(enc_uleb(len(body)) + body)
            nblocks += blocks
            nupd += upd
            nfun += 1
        out.append(10)
        out.extend(enc_uleb(len(code)) + code)
    open(a.dst, 'wb').write(out)
    if not names:
        print('wasm_cycles: no name section; libm calls are counted as compiled', file=sys.stderr)
    print('cycles: %d functions (%d maths-library at a fixed cost), %d basic blocks, '
          '%d counter updates; %s' % (nfun, nfixed, nblocks, nupd, desc))


if __name__ == '__main__':
    main()
