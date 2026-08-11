#!/usr/bin/env python3
"""Generate an initial N64Recomp symbol file for the 40 Winks ROM."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


ROM_START = 0x00001000
VRAM_START = 0x80000400
CODE_END = 0x80080390
STATIC_DATA_ROM_END = 0x000AE400
ENTRYPOINT = 0x80000400
BOOT_TARGET = 0x800019B0
CALLBACK_SCHEDULER = 0x8005BD34


def read_word(rom: bytes, vram: int) -> int:
    offset = ROM_START + vram - VRAM_START
    if offset < 0 or offset + 4 > len(rom):
        raise ValueError(f"address 0x{vram:08X} is outside the ROM")
    return int.from_bytes(rom[offset : offset + 4], "big")


def likely_stack_prologue(word: int) -> bool:
    opcode = (word >> 26) & 0x3F
    source = (word >> 21) & 0x1F
    destination = (word >> 16) & 0x1F
    immediate = word & 0xFFFF
    return (
        bool(immediate & 0x8000)
        and source == 29
        and destination == 29
        and opcode in (0x09, 0x19)
    )


def scan_prologue_candidates(rom: bytes):
    """Yield stack-frame candidates directly from the user-supplied ROM."""
    for vram in range(VRAM_START, CODE_END, 4):
        if likely_stack_prologue(read_word(rom, vram)):
            yield vram


def is_jr_ra(word: int) -> bool:
    return (word >> 26) == 0 and (word & 0x3F) == 0x08 and ((word >> 21) & 0x1F) == 31


def is_direct_jump(word: int) -> bool:
    return (word >> 26) == 0x02


def has_function_boundary_before(rom: bytes, vram: int) -> bool:
    if vram <= VRAM_START + 4:
        return True

    # IDO commonly leaves one or more padding NOPs after a return's delay slot.
    # Walk over that padding, then accept either a zero delay slot or a populated
    # delay slot immediately preceded by a return/tail jump.
    cursor = vram - 4
    for _ in range(8):
        word = read_word(rom, cursor)
        if word != 0:
            if is_jr_ra(word) or is_direct_jump(word):
                return True
            if cursor >= VRAM_START + 4:
                before = read_word(rom, cursor - 4)
                return is_jr_ra(before) or is_direct_jump(before)
            return False
        cursor -= 4
        if cursor < VRAM_START:
            break
    return True


def has_return_boundary_before(rom: bytes, vram: int) -> bool:
    """Return whether vram follows a jr $ra and its delay-slot/padding."""
    if vram <= VRAM_START + 4:
        return True

    cursor = vram - 4
    for _ in range(8):
        word = read_word(rom, cursor)
        if word != 0:
            if is_jr_ra(word):
                return True
            if cursor >= VRAM_START + 4:
                return is_jr_ra(read_word(rom, cursor - 4))
            return False
        cursor -= 4
        if cursor < VRAM_START:
            break
    return False


def branch_target(pc: int, word: int) -> int:
    immediate = word & 0xFFFF
    if immediate & 0x8000:
        immediate -= 0x10000
    return pc + 4 + immediate * 4


def direct_target(pc: int, word: int) -> int:
    return ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)


def scheduled_callback_target(lui_word: int, low_word: int) -> int | None:
    if lui_word >> 26 != 0x0F or ((lui_word >> 16) & 0x1F) != 4:
        return None

    opcode = low_word >> 26
    source = (low_word >> 21) & 0x1F
    destination = (low_word >> 16) & 0x1F
    if source != 4 or destination != 4 or opcode not in (0x09, 0x0D):
        return None

    upper = (lui_word & 0xFFFF) << 16
    lower = low_word & 0xFFFF
    if opcode == 0x09 and lower & 0x8000:
        lower -= 0x10000
    return (upper + lower) & 0xFFFFFFFF


def scan_direct_call_targets(rom: bytes) -> set[int]:
    """Return JAL destinations found in this ROM's verified static-code range."""
    targets: set[int] = set()
    for pc in range(VRAM_START, CODE_END, 4):
        word = read_word(rom, pc)
        if word >> 26 != 0x03:
            continue
        target = direct_target(pc, word)
        if VRAM_START <= target < CODE_END:
            targets.add(target)
    return targets


def scan_static_function_pointer_targets(rom: bytes) -> set[int]:
    """Return callback addresses stored in the ROM's static-data tables."""
    code_rom_end = ROM_START + CODE_END - VRAM_START
    if len(rom) < STATIC_DATA_ROM_END:
        raise ValueError("ROM ends before the static-data region")

    targets: set[int] = set()
    for offset in range(code_rom_end, STATIC_DATA_ROM_END, 4):
        target = int.from_bytes(rom[offset : offset + 4], "big")
        if target & 3 or not VRAM_START <= target < CODE_END:
            continue
        if has_return_boundary_before(rom, target):
            targets.add(target)
    return targets


def scan_scheduled_callback_targets(rom: bytes) -> set[int]:
    """Return immediate callbacks passed to the game's deferred scheduler."""
    targets: set[int] = set()
    for pc in range(VRAM_START, CODE_END - 4, 4):
        word = read_word(rom, pc)
        if word >> 26 != 0x03 or direct_target(pc, word) != CALLBACK_SCHEDULER:
            continue

        # The callback is built either as an adjacent LUI/ADDIU pair shortly
        # before the call, or by putting the ADDIU in the JAL delay slot.
        window_start = max(VRAM_START, pc - 0x40)
        for pair_pc in range(window_start, pc - 4, 4):
            target = scheduled_callback_target(
                read_word(rom, pair_pc), read_word(rom, pair_pc + 4)
            )
            if target is not None and VRAM_START <= target < CODE_END:
                if has_return_boundary_before(rom, target):
                    targets.add(target)

        target = scheduled_callback_target(read_word(rom, pc - 4), read_word(rom, pc + 4))
        if target is not None and VRAM_START <= target < CODE_END:
            if has_return_boundary_before(rom, target):
                targets.add(target)

    return targets


def is_conditional_branch(word: int) -> bool:
    opcode = word >> 26
    if opcode == 0x01 or 0x04 <= opcode <= 0x07 or 0x14 <= opcode <= 0x17:
        return True
    return opcode == 0x11 and ((word >> 21) & 0x1F) == 0x08


def reachable_end(rom: bytes, start: int, stop: int) -> int:
    pending = [start]
    visited: set[int] = set()

    while pending:
        pc = pending.pop()
        if pc < start or pc >= stop or pc in visited:
            continue
        visited.add(pc)
        word = read_word(rom, pc)
        opcode = word >> 26

        if is_conditional_branch(word):
            if pc + 4 < stop:
                visited.add(pc + 4)
            target = branch_target(pc, word)
            if start <= target < stop:
                pending.append(target)
            pending.append(pc + 8)
            continue

        if opcode == 0x02:
            if pc + 4 < stop:
                visited.add(pc + 4)
            target = direct_target(pc, word)
            if start <= target < stop:
                pending.append(target)
            continue

        if opcode == 0x03:
            if pc + 4 < stop:
                visited.add(pc + 4)
            pending.append(pc + 8)
            continue

        if opcode == 0 and (word & 0x3F) == 0x08:
            if pc + 4 < stop:
                visited.add(pc + 4)
            continue

        pending.append(pc + 4)

    return max(visited, default=start) + 4


def read_known_names(path: Path) -> dict[int, str]:
    names: dict[int, str] = {}
    with path.open(newline="") as handle:
        for row in csv.DictReader(handle):
            name = re.sub(r"[^A-Za-z0-9_]", "_", row["name"])
            names[int(row["vram"], 16)] = name
    return names


def read_manual_functions(path: Path) -> dict[int, str]:
    functions: dict[int, str] = {}
    with path.open(newline="") as handle:
        for row in csv.DictReader(handle):
            vram = int(row["vram"], 16)
            if not VRAM_START <= vram < CODE_END:
                raise ValueError(f"manual function 0x{vram:08X} is outside static code")
            functions[vram] = re.sub(r"[^A-Za-z0-9_]", "_", row["name"])
    return functions


def generate(args: argparse.Namespace) -> None:
    rom = args.rom.read_bytes()
    if rom[:4] != bytes.fromhex("80371240"):
        raise SystemExit("expected a big-endian .z64 ROM")

    starts: dict[int, set[str]] = {
        ENTRYPOINT: {"entrypoint"},
        BOOT_TARGET: {"boot-register-jump"},
    }
    manual_functions = read_manual_functions(args.manual_functions)
    for start in manual_functions:
        starts.setdefault(start, set()).add("manual-boundary")

    for target in scan_direct_call_targets(rom):
        starts.setdefault(target, set()).add("direct-jal")

    for target in scan_static_function_pointer_targets(rom):
        starts.setdefault(target, set()).add("static-function-pointer")

    for target in scan_scheduled_callback_targets(rom):
        starts.setdefault(target, set()).add("scheduled-callback")

    direct_starts = sorted(starts)
    direct_ranges: list[tuple[int, int]] = []
    for index, start in enumerate(direct_starts):
        stop = direct_starts[index + 1] if index + 1 < len(direct_starts) else CODE_END
        direct_ranges.append((start, reachable_end(rom, start, stop)))

    if args.prologues is None:
        prologue_candidates = scan_prologue_candidates(rom)
    else:
        with args.prologues.open(newline="") as handle:
            prologue_candidates = [
                int(row["vram"], 16) for row in csv.DictReader(handle)
            ]

    for candidate in prologue_candidates:
        if not VRAM_START <= candidate < CODE_END:
            continue
        if any(start < candidate < end for start, end in direct_ranges):
            continue
        if not has_function_boundary_before(rom, candidate):
            continue
        starts.setdefault(candidate, set()).add("bounded-prologue")

    names = read_known_names(args.known_names)
    names.update(manual_functions)
    ordered = sorted(starts)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)

    with args.output.open("w", encoding="ascii") as handle:
        handle.write("# Generated by tools/generate_recomp_symbols.py.\n")
        handle.write("# This is an initial static-code map; addresses still require validation.\n\n")
        handle.write("[[section]]\n")
        handle.write('name = ".main"\n')
        handle.write(f"rom = 0x{ROM_START:08X}\n")
        handle.write(f"vram = 0x{VRAM_START:08X}\n")
        handle.write(f"size = 0x{CODE_END - VRAM_START:X}\n")
        handle.write("functions = [\n")
        for index, start in enumerate(ordered):
            stop = ordered[index + 1] if index + 1 < len(ordered) else CODE_END
            name = names.get(start, f"func_{start:08X}")
            handle.write(
                f'    {{ name = "{name}", vram = 0x{start:08X}, size = 0x{stop - start:X} }},\n'
            )
        handle.write("]\n")

    with args.report.open("w", newline="", encoding="ascii") as handle:
        writer = csv.writer(handle)
        writer.writerow(["vram", "rom_offset", "size", "name", "sources"])
        for index, start in enumerate(ordered):
            stop = ordered[index + 1] if index + 1 < len(ordered) else CODE_END
            writer.writerow(
                [
                    f"0x{start:08X}",
                    f"0x{ROM_START + start - VRAM_START:08X}",
                    f"0x{stop - start:X}",
                    names.get(start, f"func_{start:08X}"),
                    "+".join(sorted(starts[start])),
                ]
            )

    print(
        f"Generated {len(ordered)} function symbols for "
        f"0x{VRAM_START:08X}..0x{CODE_END:08X}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument(
        "--prologues",
        type=Path,
        help="optional precomputed prologue CSV; defaults to scanning the ROM",
    )
    parser.add_argument(
        "--known-names", type=Path, default=Path("recomp/known_symbols.csv")
    )
    parser.add_argument(
        "--manual-functions",
        type=Path,
        default=Path("recomp/manual_functions.csv"),
    )
    parser.add_argument(
        "--output", type=Path, default=Path("recomp/generated/40winks.syms.toml")
    )
    parser.add_argument(
        "--report", type=Path, default=Path("recomp/generated/40winks.functions.csv")
    )
    generate(parser.parse_args())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
