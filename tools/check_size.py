#!/usr/bin/env python3
"""Code-size ratchet for the linked invariant probe.

Why size and not speed. `bench-smoke` builds the benchmarks, runs them and
prints the numbers -- and at the time this was written it asserted nothing, so
it was a job that could only pass (it now also gates instruction counts, via
tools/check_instructions.py; the wall-clock half still asserts nothing).
The obvious fix was rejected early ("a threshold on a shared runner
either fires spuriously or never fires"), and that rejection is right: wall-clock
on a shared GitHub runner is noise.

Code size is the measurement that argument does *not* apply to. Cross-compiled
with fixed flags, `.text` is a deterministic function of the source and the
toolchain -- the same tree produces the same byte count every time. And on the
target METL is actually for, an unnoticed kilobyte matters more than a
percentage of throughput on somebody's laptop.

What is measured: the LINKED `invariant_probe.elf`, not the archive. An archive
carries every object whether or not a linker would keep it; the ELF is what a
user pays for. The probe exercises a hand-picked set of containers (see
docs/SCOPE.md section 8), so this ratchet tracks *that set* -- adding a type to
the library moves nothing here until the probe uses it, which is the same
coverage caveat the invariant audit carries and for the same reason.

THREE GATED METRICS, and the second one is why this file changed. They are
named here for the sections that dominate them; what is summed is a CLASS (see
below), and `.text` / `.rodata` / `.bss`+`.data` below mean code / const / ram.

  .text    the code. Gated since #56.
  .rodata  constants -- lookup tables, string literals. Also flash, also
           deterministic, and it was being MEASURED AND PRINTED on every run of
           every invariants job since the beginning while being compared against
           nothing. `rodata = sizes.get(".rodata", 0)` fed a printf and stopped
           there. That is the "step that can only pass" this file's own header
           argues against, sitting inside the file that argues it.

           It is not idle either: #28 turned CRC's bit-at-a-time loop into a
           nibble table by default, which is a deliberate trade of .rodata for
           speed. A repository that gates .text and not .rodata says that trade
           is free.

  ram      `.bss` + `.data` -- the static storage a caller declares. This is the
           other half of the RAM question that #77 and #79 opened: #77 pins
           `sizeof` per container with static_assert, #79 measures the stack.
           Neither can see a hidden static inside a header, a function-local
           static and its guard variable, or a table that moved from .rodata
           into .data. Measured on the stack probe's OBJECT file, because that
           probe already declares realistic-capacity containers in static
           storage -- which is exactly what a .bss ratchet needs, and it existed
           already. The ratchet covers ram + tls: thread-local storage is RAM
           too, one copy per thread.

SECTIONS ARE CLASSIFIED BY ELF TYPE AND FLAGS, NEVER BY NAME (#405). This file
used to sum sections by name prefix -- `.bss`, `.bss.*`, `.data`, `.data.*` --
which survived -fdata-sections but silently left out anything holding RAM under
another name. Checked with an ARM object built from a probe of each case:

  thread_local        `.tbss.*` / `.tdata.*`   flags WAT       was not counted
  section(".noinit")  `.noinit`, PROGBITS      flags WA        was not counted
  dynamic init        `.bss.*` + `.init_array`                 `.bss` only

So the tool reads the ELF itself (no `size`, no `readelf`) and sorts every
SHF_ALLOC section into one class:

  code         ALLOC + EXECINSTR
  const        ALLOC, not WRITE, not EXECINSTR -- `.rodata`, `.ARM.exidx`
  ram          ALLOC + WRITE, not TLS, not an init/fini array. Split into
               ram_progbits and ram_nobits for information only: `.noinit` is
               PROGBITS in an object file although it holds no initial value, and
               only the linker script's NOLOAD makes it NOBITS. An object file can
               tell RAM from flash; it cannot reliably tell initialized RAM from
               zeroed RAM.
  tls          the TLS flag
  init_arrays  INIT_ARRAY / FINI_ARRAY / PREINIT_ARRAY. Writable, but on bare
               metal the linker script puts them in flash. An entry means some
               namespace-scope object is constructed at run time, in link order,
               so the entry count is reported as well as the bytes.

A non-ALLOC section is ignored. An ALLOC section of a type this file does not
know FAILS the run: dropping it would be the name-prefix hole again.

A LINKED image is also measured by address, because there the address decides:
`.data` occupies RAM at its run address and flash at its load address. When the
image defines the region symbols that tests/embedded/mps2-an385.ld gives
picolibc.ld (`__flash`, `__flash_size`, `__ram`, `__ram_size`), every PT_LOAD
segment is placed: RAM is memsz inside the RAM region, flash is filesz inside the
flash region, and a segment in neither fails. The invariant probe links with the
toolchain's default script, which declares no regions, so its region totals are
recorded as null -- not measured, which is not the same as 0.

THE MEASUREMENT RECORD. `--record FILE` writes what was measured as JSON: cpu,
commit, per artifact the toolchain (`.comment`), flags, per-class totals and
every section, plus the stack figures in keys of their own -- the static deepest
frame from `.su` (`--su-dir`) and the runtime high-water mark without and with
LTO, which tools/check_qemu_probes.py adds to the same record (#404), with the
QEMU instruction counts. The `invariants` job uploads one record per CPU, and the release attaches all four, merged by
`--merge-records`, as metl-X.Y.Z-measurements.json, so a later release has
something to be compared against.

`--compare OLD NEW` prints that comparison as Markdown (#409): per CPU, every
figure old and new with its delta, the sections behind each class that moved,
"not measured" for a null (never 0, and no delta), and a CPU or figure on one
side only listed as added or removed. The budgets only catch growth past their
tolerance; this shows the change below it. The `invariants` job compares its
record with the latest release's, and the release compares the new record with
the previous release's and attaches it as metl-X.Y.Z-measurements.md.

Usage:
    tools/check_size.py --elf invariant_probe.elf --cpu cortex-m3
    tools/check_size.py --elf invariant_probe.elf --cpu cortex-m3 --report
    tools/check_size.py --ram-object stack_probe.o --cpu cortex-m3
    tools/check_size.py --ram-object stack_probe.o --cpu cortex-m3 \
        --record m.json --flags "$FLAGS" --su-dir .
    tools/check_size.py --summary m.json
    tools/check_size.py --merge-records out.json --expect-commit SHA a.json b.json ...
    tools/check_size.py --compare metl-1.4.0-measurements.json measurements-cortex-m3.json
    tools/check_size.py --self-test
"""

import argparse
import json
import os
import pathlib
import struct
import subprocess
import sys
import tempfile

# Budgets in bytes for `.text` of the linked probe, per CPU, measured by the
# `invariants` CI job itself. A local number is not a substitute and never has
# been in this repo: there is no ARM toolchain on the dev machine, and even where
# a local toolchain exists it has disagreed with CI three times running (a
# clang-tidy figure that was 3.7x slack, a delta with the wrong sign, and a
# header that reported clean locally and two findings on CI).
#
# So these started as None. The job prints the measured sizes with --report, and
# the numbers were set from that output in a follow-up commit -- the same
# two-step the clang-tidy ratchet used, for the same reason.
#
# A budget goes DOWN freely. It goes UP only when the probe was deliberately
# given more to link, and the commit that raises it has to say what and why --
# never to make a red job green. "Do not pad" is the whole point: a budget set
# above what was measured is a ratchet that has stopped ratcheting.
#
# Raised once so far, and it earned its keep. Adding metl/parse.hpp to the probe
# cost +1504 bytes on Cortex-M0 against +812 on M3/M4/M7 -- the outlier said the
# fold was using 64-bit arithmetic and a runtime divide on the one target with
# neither. Fixing that brought M0 to +660 and the spread to 660..704. Had the
# budget simply been raised to fit the first number, that bug would have shipped.
#
# Measured by the `invariants` job on run 32483066357 (PR #66, after the fix).
BUDGETS = {
    "cortex-m0": 3440,
    "cortex-m3": 4236,
    "cortex-m4": 4244,
    "cortex-m7": 4260,
}

# Room for a toolchain that shifts codegen slightly without anything regressing.
# Ubuntu's arm-none-eabi-gcc moves with the runner image, and a minor version can
# change inlining decisions by a few hundred bytes on an image this small.
TOLERANCE_BYTES = 512

# Budgets for `.rodata` of the same linked probe. Measured by the `invariants`
# job, run 38493698132 (#374): all four targets reported 472, which is what a
# section of pure constants should do -- it does not depend on instruction
# selection the way `.text` does. The first budget, 408 (run 32586215841), had
# drifted to 456 on main within the tolerance; #374's destructor check then
# stores its file name and expression text, which took it to 472.
#
# The tolerance is deliberately much tighter than .text's 512. A toolchain bump
# moves code generation; it does not move the number of constant bytes the
# source declares. Slack here would only hide the thing this is for -- a table
# quietly arriving in the image.
RODATA_BUDGETS = {
    "cortex-m0": 472,
    "cortex-m3": 472,
    "cortex-m4": 472,
    "cortex-m7": 472,
}
RODATA_TOLERANCE_BYTES = 64

# Budgets for `.bss` + `.data` of the stack probe's object file. Measured by the
# `invariants` job on PR #83, run 32613760217. All four targets report the same
# number, and they should: static storage is a sum of `sizeof`, which is an ABI
# property, and these are four ARM32 targets with the same one. A DIFFERENCE
# between them would itself be the finding.
#
# The figure is large -- more than a Cortex-M0+ with 8 KB of SRAM has -- because
# the probe deliberately holds realistic-capacity containers. That is the point:
# at capacity 4-8 the measurement would be noise. It is a probe, not a product.
RAM_BUDGETS = {
    "cortex-m0": 24_304,
    "cortex-m3": 24_304,
    "cortex-m4": 24_304,
    "cortex-m7": 24_304,
}
RAM_TOLERANCE_BYTES = 64


# ELF constants this file needs, from the System V gABI and the ARM ELF ABI.
SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHF_TLS = 0x400

SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_NOTE = 7
SHT_NOBITS = 8
SHT_INIT_ARRAY = 14
SHT_FINI_ARRAY = 15
SHT_PREINIT_ARRAY = 16
SHT_ARM_EXIDX = 0x70000001
EM_ARM = 40
PT_LOAD = 1

ARRAY_TYPES = {SHT_INIT_ARRAY: "INIT_ARRAY", SHT_FINI_ARRAY: "FINI_ARRAY",
               SHT_PREINIT_ARRAY: "PREINIT_ARRAY"}

# The section types an ALLOC section may have. Anything else fails the run
# rather than being dropped. Processor-specific types are only known on their
# own machine: 0x70000001 is ARM_EXIDX on ARM and something else elsewhere.
ALLOC_TYPES = {SHT_PROGBITS: "PROGBITS", SHT_NOBITS: "NOBITS", SHT_NOTE: "NOTE",
               **ARRAY_TYPES}
MACHINE_ALLOC_TYPES = {EM_ARM: {SHT_ARM_EXIDX: "ARM_EXIDX"}}

CLASSES = ("code", "const", "ram", "ram_progbits", "ram_nobits", "tls",
           "init_arrays", "init_array_entries")

# The symbols tests/embedded/mps2-an385.ld defines for picolibc.ld.
REGION_SYMBOLS = ("__flash", "__flash_size", "__ram", "__ram_size")


class ElfError(Exception):
    """A file this tool cannot measure honestly."""


class Elf:
    """The parts of an ELF file this tool reads: sections, segments, symbols."""

    def __init__(self, data):
        if data[:4] != b"\x7fELF":
            raise ElfError("not an ELF file")
        elf_class, encoding = data[4], data[5]
        if elf_class not in (1, 2) or encoding not in (1, 2):
            raise ElfError(f"unsupported ELF class {elf_class} / data encoding {encoding}")
        self.is64 = elf_class == 2
        self.endian = "<" if encoding == 1 else ">"
        self.data = data
        header = "16sHHIQQQIHHHHHH" if self.is64 else "16sHHIIIIIHHHHHH"
        (_, self.type, self.machine, _, _, phoff, shoff, _, _,
         phentsize, phnum, shentsize, shnum, shstrndx) = self._unpack(header, 0)

        raw_sections = []
        if shoff:
            layout = "IIQQQQIIQQ" if self.is64 else "IIIIIIIIII"
            first = self._unpack(layout, shoff)
            # Extended numbering: past 0xff00 sections the real count and
            # string-table index live in section 0.
            count = shnum or first[5]
            if shstrndx == 0xFFFF:
                shstrndx = first[6]
            for i in range(count):
                (name, kind, flags, addr, offset, size, link, _, _,
                 entsize) = self._unpack(layout, shoff + i * shentsize)
                raw_sections.append({"name_offset": name, "type": kind, "flags": flags,
                                     "addr": addr, "offset": offset, "size": size,
                                     "link": link, "entsize": entsize})
        if raw_sections and not 0 < shstrndx < len(raw_sections):
            # SHN_UNDEF, or past the table: nothing to name the sections by,
            # and `.comment` is found by name.
            raise ElfError(f"no section-name string table: e_shstrndx is {shstrndx} "
                           f"with {len(raw_sections)} sections")
        self.sections = []
        for section in raw_sections:
            section["name"] = self._string(raw_sections[shstrndx], section["name_offset"])
            self.sections.append(section)

        self.segments = []
        if phoff:
            for i in range(phnum):
                if self.is64:
                    kind, _, _, vaddr, paddr, filesz, memsz, _ = self._unpack(
                        "IIQQQQQQ", phoff + i * phentsize)
                else:
                    kind, _, vaddr, paddr, filesz, memsz, _, _ = self._unpack(
                        "IIIIIIII", phoff + i * phentsize)
                self.segments.append({"type": kind, "vaddr": vaddr, "paddr": paddr,
                                      "filesz": filesz, "memsz": memsz})

    def _unpack(self, layout, offset):
        fmt = self.endian + layout
        end = offset + struct.calcsize(fmt)
        if end > len(self.data):
            raise ElfError(f"truncated: a header at offset {offset} runs past the file")
        return struct.unpack_from(fmt, self.data, offset)

    def _contents(self, section):
        """A section's bytes; an error if they run past the end of the file."""
        if section["type"] == SHT_NOBITS:
            return b""
        end = section["offset"] + section["size"]
        if end > len(self.data):
            raise ElfError(f"truncated: section {section.get('name') or '?'} at offset "
                           f"{section['offset']} ({section['size']} bytes) runs past the "
                           f"end of the {len(self.data)}-byte file")
        return self.data[section["offset"]:end]

    def _string(self, table, offset):
        # Bounds from _contents, but searched in place: slicing the table per
        # name is quadratic on an object with tens of thousands of sections.
        size = 0 if table["type"] == SHT_NOBITS else table["size"]
        table_end = table["offset"] + size
        if table_end > len(self.data):
            self._contents(table)  # raises, naming the table
        start = table["offset"] + offset
        end = self.data.find(b"\0", start, table_end)
        if offset >= size or end < 0:
            raise ElfError(f"string table offset {offset} is outside its "
                           f"{size}-byte table or not terminated")
        return self.data[start:end].decode("utf-8", "replace")

    def symbols(self):
        """{name: value} from the symbol table, empty if there is none."""
        found = {}
        layout = "IBBHQQ" if self.is64 else "IIIBBH"
        size = struct.calcsize(self.endian + layout)
        for section in self.sections:
            if section["type"] != SHT_SYMTAB:
                continue
            strings = self.sections[section["link"]]
            for i in range(section["size"] // size):
                fields = self._unpack(layout, section["offset"] + i * size)
                name, value = (fields[0], fields[4]) if self.is64 else (fields[0], fields[1])
                if name:
                    found[self._string(strings, name)] = value
        return found

    def comment(self):
        """The toolchain identification strings in `.comment`, or None."""
        for section in self.sections:
            if section["name"] == ".comment" and section["type"] == SHT_PROGBITS:
                blob = self._contents(section)
                strings = [s.decode("utf-8", "replace") for s in blob.split(b"\0") if s]
                return sorted(set(strings)) or None
        return None


def flag_letters(flags):
    return "".join(letter for bit, letter in ((SHF_WRITE, "W"), (SHF_ALLOC, "A"),
                                              (SHF_EXECINSTR, "X"), (SHF_TLS, "T"))
                   if flags & bit)


def classify(elf):
    """(totals, sections) by type and flags; raises ElfError on an unknown type.

    `totals` has every key in CLASSES. `sections` lists each ALLOC section with
    its class, so a total can always be traced back to what made it up.
    """
    known = {**ALLOC_TYPES, **MACHINE_ALLOC_TYPES.get(elf.machine, {})}
    word = 8 if elf.is64 else 4
    totals = dict.fromkeys(CLASSES, 0)
    listed = []
    for section in elf.sections:
        flags, kind, size = section["flags"], section["type"], section["size"]
        if not flags & SHF_ALLOC:
            continue
        if kind not in known:
            raise ElfError(f"section {section['name'] or '?'} is ALLOC with type "
                           f"{kind:#x}, which this tool does not classify. Teach "
                           f"classify() what it is rather than leave its bytes out.")
        if flags & SHF_TLS:
            cls = "tls"
        elif kind in ARRAY_TYPES:
            cls = "init_arrays"
            totals["init_array_entries"] += size // (section["entsize"] or word)
        elif flags & SHF_EXECINSTR:
            cls = "code"
        elif flags & SHF_WRITE:
            cls = "ram"
            totals["ram_nobits" if kind == SHT_NOBITS else "ram_progbits"] += size
        else:
            cls = "const"
        totals[cls] += size
        listed.append({"name": section["name"], "type": known[kind],
                       "flags": flag_letters(flags), "class": cls, "bytes": size})
    return totals, listed


def regions(elf):
    """{"flash": bytes, "ram": bytes} placed by PT_LOAD address, or None.

    None means the image declares no memory regions (the toolchain's default
    linker script), so there is nothing to place a segment against.
    """
    symbols = elf.symbols()
    if not all(name in symbols for name in REGION_SYMBOLS):
        return None
    flash = (symbols["__flash"], symbols["__flash"] + symbols["__flash_size"])
    ram = (symbols["__ram"], symbols["__ram"] + symbols["__ram_size"])

    def inside(region, start, size):
        return region[0] <= start and start + size <= region[1]

    totals = {"flash": 0, "ram": 0}
    for segment in elf.segments:
        if segment["type"] != PT_LOAD or segment["memsz"] == 0:
            continue
        if inside(ram, segment["vaddr"], segment["memsz"]):
            totals["ram"] += segment["memsz"]
            if segment["filesz"]:
                # Initialized RAM: its initial bytes are stored in flash.
                if not inside(flash, segment["paddr"], segment["filesz"]):
                    raise ElfError(f"a RAM segment at {segment['vaddr']:#x} loads its "
                                   f"{segment['filesz']} initial bytes from "
                                   f"{segment['paddr']:#x}, outside the flash region")
                totals["flash"] += segment["filesz"]
        elif inside(flash, segment["vaddr"], segment["memsz"]):
            totals["flash"] += segment["filesz"]
        else:
            raise ElfError(f"a PT_LOAD segment at {segment['vaddr']:#x} "
                           f"({segment['memsz']} bytes) is in neither the flash "
                           f"region [{flash[0]:#x}, {flash[1]:#x}) nor the RAM "
                           f"region [{ram[0]:#x}, {ram[1]:#x})")
    return totals


def measure(path, linked):
    """One artifact's entry for the measurement record."""
    try:
        data = pathlib.Path(path).read_bytes()
    except OSError as error:
        raise ElfError(f"cannot read {path}: {error.strerror}") from error
    elf = Elf(data)
    totals, listed = classify(elf)
    entry = {"role": "linked-image" if linked else "object",
             "toolchain": elf.comment(), "flags": None,
             "classes": totals, "regions": None, "sections": listed}
    if linked:
        entry["regions"] = regions(elf) or {"flash": None, "ram": None}
    return entry


def static_max_frame(su_dir):
    """The deepest frame in the `.su` files under `su_dir`, or None."""
    import check_stack  # same directory; only needed when recording the stack
    entries = check_stack.collect(su_dir)
    if not entries:
        return None
    name, size, _ = max(entries, key=lambda e: e[1])
    return {"bytes": size, "function": name}


RECORD_SCHEMA = 1


def empty_record(cpu, commit=None):
    return {"schema": RECORD_SCHEMA, "cpu": cpu, "commit": commit,
            "artifacts": {},
            # Separate keys, never one "stack" number: the static figure is the
            # deepest single frame, the runtime ones are a high-water mark.
            # tools/check_qemu_probes.py fills the runtime ones (#404); null
            # until it has.
            "stack": {"static_max_frame": None, "runtime_hwm": None,
                      "runtime_hwm_lto": None}}


def current_commit():
    sha = os.environ.get("GITHUB_SHA")
    if sha:
        return sha
    result = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def update_record(path, cpu, artifact, entry, su_dir, commit):
    """Merge one measurement into the record at `path`, creating it if needed."""
    record_path = pathlib.Path(path)
    record = json.loads(record_path.read_text()) if record_path.exists() else empty_record(cpu, commit)
    if record.get("cpu") != cpu:
        raise ElfError(f"{path} records {record.get('cpu')}, not {cpu}")
    if record.get("commit") != commit or record.get("schema") != RECORD_SCHEMA:
        # Left by a run of another commit (or schema): its artifacts were
        # measured there, so start over rather than mix the two.
        record = empty_record(cpu, commit)
    record["artifacts"][pathlib.Path(artifact).name] = entry
    if su_dir is not None:
        frame = static_max_frame(su_dir)
        if frame is None:
            raise ElfError(f"no .su frames under {su_dir}; the record would claim "
                           f"a stack measurement that was never made")
        record["stack"]["static_max_frame"] = frame
    record_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")


def summary(record):
    """Markdown for the job summary: one row per artifact, nulls shown as such."""
    def cell(value):
        return "not measured" if value is None else str(value)

    lines = [f"### Measurements: {record['cpu']} at {(record.get('commit') or '?')[:12]}", "",
             "| artifact | code | const | ram | tls | init arrays (entries) | flash | RAM |",
             "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for name, entry in sorted(record["artifacts"].items()):
        c = entry["classes"]
        r = entry["regions"]
        # An object file has no addresses yet: placing it is not applicable,
        # which is a different answer from "not measured".
        flash, ram = ("n/a", "n/a") if r is None else (cell(r["flash"]), cell(r["ram"]))
        lines.append(f"| `{name}` | {c['code']} | {c['const']} | {c['ram']} | {c['tls']} | "
                     f"{c['init_arrays']} ({c['init_array_entries']}) | {flash} | {ram} |")
    stack = record["stack"]
    frame = stack["static_max_frame"]
    lines += ["", "| stack | bytes |", "|---|---:|",
              f"| static deepest frame | "
              f"{cell(frame and frame['bytes'])}{' (`' + frame['function'] + '`)' if frame else ''} |",
              f"| runtime high-water mark | {cell(stack['runtime_hwm'])} |",
              f"| runtime high-water mark, LTO | {cell(stack['runtime_hwm_lto'])} |"]
    return "\n".join(lines) + "\n"


REQUIRED_ARTIFACTS = ("invariant_probe.elf", "stack_probe.o")


def merge_records(paths, expect_commit):
    """{"commit", "records": {cpu: record}} for a release; raises on any gap."""
    records = {}
    for path in paths:
        record = json.loads(pathlib.Path(path).read_text())
        cpu = record.get("cpu")
        if record.get("schema") != RECORD_SCHEMA:
            raise ElfError(f"{path}: schema {record.get('schema')}, expected {RECORD_SCHEMA}")
        if cpu in records:
            raise ElfError(f"{path}: a second record for {cpu}")
        if expect_commit and record.get("commit") != expect_commit:
            raise ElfError(f"{path}: measured {record.get('commit')}, not {expect_commit}")
        missing = [a for a in REQUIRED_ARTIFACTS if a not in record.get("artifacts", {})]
        if missing:
            raise ElfError(f"{path}: no measurement of {', '.join(missing)}")
        if record["stack"]["static_max_frame"] is None:
            raise ElfError(f"{path}: no static stack measurement")
        records[cpu] = record
    wanted = set(RAM_BUDGETS)
    if set(records) != wanted:
        raise ElfError(f"records for {sorted(records)}; a release needs exactly "
                       f"{sorted(wanted)}")
    commits = {record.get("commit") for record in records.values()}
    if None in commits or len(commits) != 1:
        raise ElfError(f"the records do not agree on one known commit: {sorted(map(str, commits))}")
    return {"schema": RECORD_SCHEMA, "commit": commits.pop(), "records": records}


def records_by_cpu(document, path):
    """{cpu: record} from a merged release file or from one job's record."""
    if document.get("schema") != RECORD_SCHEMA:
        raise ElfError(f"{path}: schema {document.get('schema')}, expected {RECORD_SCHEMA}; "
                       f"records of different schemas are not compared")
    if "records" in document:
        return document["records"]
    return {document.get("cpu"): document}


def figures(record):
    """{(group, name): value} of everything a record measured, None where it
    measured nothing. Absent keys are absent here too: a figure one side has
    and the other does not is listed as added or removed, not as a change."""
    out = {}
    for artifact, entry in record.get("artifacts", {}).items():
        for name, value in entry.get("classes", {}).items():
            out[(f"`{artifact}`", name)] = value
        for name, value in (entry.get("regions") or {}).items():
            out[(f"`{artifact}`", f"region {name}")] = value
    stack = record.get("stack", {})
    frame = stack.get("static_max_frame")
    out[("stack", "static deepest frame")] = frame and frame["bytes"]
    for key, label in (("runtime_hwm", "runtime high-water mark"),
                       ("runtime_hwm_lto", "runtime high-water mark, LTO")):
        if key in stack:
            out[("stack", label)] = stack[key]
    for name, value in ((record.get("instructions") or {}).get("regions") or {}).items():
        out[("instructions", name)] = value
    return out


def delta_cells(old, new):
    """(delta, percent) cells. A side that measured nothing has no delta."""
    if old is None or new is None:
        return "", ""
    change = new - old
    percent = "" if old == 0 else f"{100.0 * change / old:+.1f}%"
    return f"{change:+d}", percent


def section_changes(old_entry, new_entry, classes):
    """Rows naming the sections behind a change in `classes`, largest change
    first."""
    def sizes(entry):
        # Summed by name: an object file can hold two sections of one name.
        total = {}
        for section in entry.get("sections", []):
            if section["class"] in classes:
                total[section["name"]] = total.get(section["name"], 0) + section["bytes"]
        return total
    before, after = sizes(old_entry), sizes(new_entry)
    changed = [(name, before.get(name), after.get(name)) for name in set(before) | set(after)
               if before.get(name) != after.get(name)]
    changed.sort(key=lambda row: (-abs((row[2] or 0) - (row[1] or 0)), row[0]))
    return [f"| `{name}` | {'absent' if old is None else old} | {'absent' if new is None else new} | "
            f"{(new or 0) - (old or 0):+d} |" for name, old, new in changed]


def compare_records(old_document, new_document, old_path="old", new_path="new"):
    """Markdown comparing two records (or merged release files) per CPU. A
    single job's record is compared with the same CPU of the other side only,
    so a pull request's job does not list the other CPUs as removed."""
    def cell(value):
        return "not measured" if value is None else str(value)

    def label(document):
        commit = document.get("commit")
        return commit[:12] if commit else "?"

    old_cpus = records_by_cpu(old_document, old_path)
    new_cpus = records_by_cpu(new_document, new_path)
    for single, other in ((old_document, "new"), (new_document, "old")):
        if "records" not in single:
            keep = set(records_by_cpu(single, ""))
            if other == "new":
                new_cpus = {cpu: r for cpu, r in new_cpus.items() if cpu in keep}
            else:
                old_cpus = {cpu: r for cpu, r in old_cpus.items() if cpu in keep}
    lines = [f"### Measurements compared: {label(old_document)} -> {label(new_document)}", ""]
    for cpu in sorted(set(old_cpus) - set(new_cpus)):
        lines += [f"#### {cpu}", "", "Removed: only the old side measured this CPU.", ""]
    for cpu in sorted(set(new_cpus) - set(old_cpus)):
        lines += [f"#### {cpu}", "", "Added: only the new side measured this CPU.", ""]
    for cpu in sorted(set(old_cpus) & set(new_cpus)):
        old, new = figures(old_cpus[cpu]), figures(new_cpus[cpu])
        lines += [f"#### {cpu}", "", "| figure | old | new | delta | % |", "|---|---:|---:|---:|---:|"]
        for key in sorted(set(old) | set(new)):
            name = f"{key[0]} {key[1]}"
            if key not in new:
                lines.append(f"| {name} | {cell(old[key])} | removed | | |")
            elif key not in old:
                lines.append(f"| {name} | added | {cell(new[key])} | | |")
            else:
                change, percent = delta_cells(old[key], new[key])
                lines.append(f"| {name} | {cell(old[key])} | {cell(new[key])} | {change} | {percent} |")
        old_artifacts = old_cpus[cpu].get("artifacts", {})
        new_artifacts = new_cpus[cpu].get("artifacts", {})
        for artifact in sorted(set(old_artifacts) & set(new_artifacts)):
            old_classes = old_artifacts[artifact].get("classes", {})
            new_classes = new_artifacts[artifact].get("classes", {})
            # The section class, and the totals it feeds.
            for cls, totals in (("code", ("code",)), ("const", ("const",)),
                                ("ram", ("ram", "ram_progbits", "ram_nobits")), ("tls", ("tls",)),
                                ("init_arrays", ("init_arrays", "init_array_entries"))):
                if all(old_classes.get(t) == new_classes.get(t) for t in totals):
                    continue
                rows = section_changes(old_artifacts[artifact], new_artifacts[artifact], {cls})
                if rows:
                    lines += ["", f"Sections behind the {cls} change in `{artifact}`:", "",
                              "| section | old | new | delta |", "|---|---:|---:|---:|", *rows]
        lines.append("")
    return "\n".join(lines)


def compare(label, measured, budget, tolerance, cpu, report, key):
    """Return (exit_code, lines). Shared by all three metrics."""
    out = []

    if budget is None:
        out.append(f"  {label}={measured} bytes  (no budget recorded)")
        if report:
            return 0, out
        out.append(f"::error::no {label} budget recorded for {cpu}. Set "
                   f"{key}['{cpu}'] = {measured} in tools/check_size.py from THIS "
                   f"number, measured by this job, not a local one.")
        return 1, out

    ceiling = budget + tolerance
    if measured > ceiling:
        out.append(f"::error::{cpu} {label} grew to {measured} bytes, over the "
                   f"{budget}-byte budget (+{tolerance} tolerance = {ceiling}).")
        out.append(f"::error::That is {measured - budget} bytes more than the "
                   f"recorded size. If a toolchain bump moved it, re-measure with "
                   f"--report and say so in the commit; do not pad.")
        return 1, out

    out.append(f"  {label}={measured} bytes, within budget {budget} "
               f"(+{tolerance} tolerance), {ceiling - measured} bytes of room")
    if measured < budget - tolerance:
        out.append(f"      NOTE: {budget - measured} bytes SMALLER than the "
                   f"budget. Lower {key}['{cpu}'] to {measured} so the ratchet "
                   f"keeps its grip.")
    return 0, out


def breakdown(listed, *classes):
    """The sections making up `classes`, largest first, for a failure message."""
    rows = sorted((s for s in listed if s["class"] in classes), key=lambda s: -s["bytes"])
    return [f"      {s['bytes']:7}  {s['class']:11} {s['type']:10} {s['flags']:4} {s['name']}"
            for s in rows]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--elf", help="linked probe: gates code and const")
    parser.add_argument("--ram-object", help="stack probe object: gates ram + tls")
    parser.add_argument("--cpu")
    parser.add_argument(
        "--report",
        action="store_true",
        help="print the measured size and exit 0 even with no budget set",
    )
    parser.add_argument("--record", help="JSON measurement record to create or add to")
    parser.add_argument("--flags", help="the compiler flags, for the record")
    parser.add_argument("--su-dir", help="with --record: record the deepest .su frame")
    parser.add_argument("--summary", metavar="RECORD",
                        help="print RECORD as a Markdown table and exit")
    parser.add_argument("--merge-records", metavar="OUT",
                        help="merge the record files given as arguments into OUT")
    parser.add_argument("--expect-commit", help="with --merge-records: every record's commit")
    parser.add_argument("--compare", nargs=2, metavar=("OLD", "NEW"),
                        help="print a Markdown comparison of two records or merged release files")
    parser.add_argument("records", nargs="*", help=argparse.SUPPRESS)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    try:
        if args.summary:
            print(summary(json.loads(pathlib.Path(args.summary).read_text())), end="")
            return 0
        if args.compare:
            old_path, new_path = args.compare
            print(compare_records(json.loads(pathlib.Path(old_path).read_text()),
                                  json.loads(pathlib.Path(new_path).read_text()),
                                  old_path, new_path), end="")
            return 0
        if args.merge_records:
            if not args.records:
                raise ElfError("--merge-records needs the record files to merge")
            merged = merge_records(args.records, args.expect_commit)
            pathlib.Path(args.merge_records).write_text(
                json.dumps(merged, indent=2, sort_keys=True) + "\n")
            print(f"merged {len(merged['records'])} records into {args.merge_records}")
            return 0
        if not args.cpu or bool(args.elf) == bool(args.ram_object) or args.records:
            print("error: --cpu plus exactly one of --elf / --ram-object", file=sys.stderr)
            return 2
        if args.su_dir and not args.record:
            print("error: --su-dir only feeds --record", file=sys.stderr)
            return 2

        artifact = args.elf or args.ram_object
        entry = measure(artifact, linked=bool(args.elf))
        entry["flags"] = args.flags
        if args.record:
            update_record(args.record, args.cpu, artifact, entry, args.su_dir,
                          current_commit())
    except (ElfError, OSError, ValueError, KeyError, TypeError) as error:
        # A traceback here would be read as a broken script rather than as a
        # broken measurement, and the job would need someone to go and look.
        print(f"::error::{error}", file=sys.stderr)
        return 1

    classes, listed = entry["classes"], entry["sections"]
    if args.elf:
        code, const = classes["code"], classes["const"]
        where = entry["regions"]
        placed = ("no memory regions declared" if where["flash"] is None
                  else f"flash={where['flash']} RAM={where['ram']}")
        print(f"{args.cpu}: code={code} const={const} bytes, "
              f"{classes['init_array_entries']} init_array entries, {placed}  ({artifact})")

        # A linked probe with no code is not a small probe, it is a broken
        # measurement -- a wrong path, a section that lost its flags. Zero must
        # never look like a pass.
        if code == 0:
            print(f"::error::{artifact} has no code. A ratchet over an empty "
                  f"measurement passes forever.", file=sys.stderr)
            return 1

        code_text, lines_text = compare(
            "code", code, BUDGETS.get(args.cpu), TOLERANCE_BYTES,
            args.cpu, args.report, "BUDGETS")
        code_rodata, lines_rodata = compare(
            "const", const, RODATA_BUDGETS.get(args.cpu), RODATA_TOLERANCE_BYTES,
            args.cpu, args.report, "RODATA_BUDGETS")
        for line in lines_text + lines_rodata:
            print(line)
        if code_text or code_rodata or args.report:
            print("  what makes up code and const:")
            for line in breakdown(listed, "code", "const"):
                print(line)
        if args.report:
            print("  --report: printing only, not enforcing")
        return max(code_text, code_rodata)

    ram = classes["ram"] + classes["tls"]
    print(f"{args.cpu}: ram={classes['ram']} (progbits {classes['ram_progbits']}, "
          f"nobits {classes['ram_nobits']}) tls={classes['tls']} bytes, "
          f"{classes['init_array_entries']} init_array entries  ({artifact})")

    # The stack probe declares realistic-capacity containers in static storage.
    # If that sums to zero the probe was not the artifact measured.
    if ram == 0:
        print(f"::error::{artifact} has no writable ALLOC section at all. The RAM "
              f"probe declares static containers, so zero means the wrong file was "
              f"measured.", file=sys.stderr)
        return 1

    code, lines = compare("ram+tls", ram, RAM_BUDGETS.get(args.cpu),
                          RAM_TOLERANCE_BYTES, args.cpu, args.report, "RAM_BUDGETS")
    for line in lines:
        print(line)
    if code or args.report:
        print("  what makes up ram and tls:")
        for line in breakdown(listed, "ram", "tls"):
            print(line)
    if args.report:
        print("  --report: printing only, not enforcing")
    return code


# ---------------------------------------------------------------------------
# Synthetic ELF files for the self-test, so it needs no toolchain.
# ---------------------------------------------------------------------------
def build_elf(sections, segments=(), symbols=None, comment=None,
              is64=False, big=False, machine=EM_ARM, elf_type=1, extended=False):
    """Bytes of an ELF file with these sections (name, type, flags, size[, entsize]).

    Section contents are not written: the tool never reads them, except
    `.comment` and the symbol and string tables, which this builds for real.
    """
    end = ">" if big else "<"
    names = bytearray(b"\0")
    blobs = bytearray()
    table = [(0, 0, 0, 0, 0, 0, 0, 0)]  # name, type, flags, offset, size, link, entsize, addr
    ehsize = 64 if is64 else 52
    phentsize = 56 if is64 else 32
    data_start = ehsize + phentsize * len(segments)

    def name_of(text):
        offset = len(names)
        names.extend(text.encode() + b"\0")
        return offset

    def blob(payload):
        offset = data_start + len(blobs)
        blobs.extend(payload)
        return offset

    for name, kind, flags, size, *entsize in sections:
        table.append((name_of(name), kind, flags, 0, size, 0, entsize[0] if entsize else 0, 0))
    if comment is not None:
        payload = b"\0".join(c.encode() for c in comment) + b"\0"
        table.append((name_of(".comment"), SHT_PROGBITS, 0, blob(payload), len(payload), 0, 0, 0))
    if symbols:
        strtab = bytearray(b"\0")
        syms = bytearray(struct.pack(end + ("IBBHQQ" if is64 else "IIIBBH"), *([0] * 6)))
        for symbol, value in symbols.items():
            offset = len(strtab)
            strtab.extend(symbol.encode() + b"\0")
            syms.extend(struct.pack(end + "IBBHQQ", offset, 0, 0, 0xFFF1, value, 0) if is64
                        else struct.pack(end + "IIIBBH", offset, value, 0, 0, 0, 0xFFF1))
        strtab_index = len(table) + 1
        table.append((name_of(".symtab"), SHT_SYMTAB, 0, blob(syms), len(syms),
                      strtab_index, 24 if is64 else 16, 0))
        table.append((name_of(".strtab"), 3, 0, blob(strtab), len(strtab), 0, 0, 0))
    shstrndx = len(table)
    table.append((name_of(".shstrtab"), 3, 0, 0, 0, 0, 0, 0))
    shstr_offset = data_start + len(blobs)
    table[-1] = table[-1][:3] + (shstr_offset, len(names)) + table[-1][5:]
    shoff = shstr_offset + len(names)
    shentsize = 64 if is64 else 40

    out = bytearray(b"\x7fELF" + bytes([2 if is64 else 1, 2 if big else 1, 1]) + bytes(9))
    header = "HHIQQQIHHHHHH" if is64 else "HHIIIIIHHHHHH"
    if extended:
        # gABI extended numbering: e_shnum = 0 and e_shstrndx = SHN_XINDEX,
        # the real values in section 0's sh_size and sh_link.
        table[0] = (0, 0, 0, 0, len(table), shstrndx, 0, 0)
    out += struct.pack(end + header, elf_type, machine, 1, 0,
                       ehsize if segments else 0, shoff, 0, ehsize, phentsize,
                       len(segments), shentsize, 0 if extended else len(table),
                       0xFFFF if extended else shstrndx)
    for vaddr, paddr, filesz, memsz, *kind in segments:
        kind = kind[0] if kind else PT_LOAD
        out += (struct.pack(end + "IIQQQQQQ", kind, 0, 0, vaddr, paddr, filesz, memsz, 0)
                if is64 else
                struct.pack(end + "IIIIIIII", kind, 0, vaddr, paddr, filesz, memsz, 0, 0))
    out += blobs + names
    for name, kind, flags, offset, size, link, entsize, addr in table:
        out += (struct.pack(end + "IIQQQQIIQQ", name, kind, flags, addr, offset, size,
                            link, 0, 1, entsize) if is64 else
                struct.pack(end + "IIIIIIIIII", name, kind, flags, addr, offset, size,
                            link, 0, 1, entsize))
    return bytes(out)


WA, AX, A, WAT = SHF_WRITE | SHF_ALLOC, SHF_ALLOC | SHF_EXECINSTR, SHF_ALLOC, \
    SHF_WRITE | SHF_ALLOC | SHF_TLS

# Every row of #405's table, as the ARM object probe produced them.
PROBE_SECTIONS = [
    (".text.f", SHT_PROGBITS, AX, 100),
    (".rodata.c", SHT_PROGBITS, A, 32),
    (".bss.zero_g", SHT_NOBITS, WA, 4),
    (".data.nonzero_g", SHT_PROGBITS, WA, 8),
    (".tbss.tls_zero", SHT_NOBITS, WAT, 16),
    (".tdata.tls_init", SHT_PROGBITS, WAT, 32),
    (".noinit", SHT_PROGBITS, WA, 64),
    (".bss.dyn_g", SHT_NOBITS, WA, 128),
    (".init_array", SHT_INIT_ARRAY, WA, 8),
    (".ARM.exidx.text.f", SHT_ARM_EXIDX, A | 0x80, 8),  # AL: SHF_LINK_ORDER
    (".ARM.attributes", 0x70000003, 0, 50),  # not ALLOC: ignored
    (".rel.text.f", 9, 0x40, 16),  # SHT_REL, not ALLOC: ignored
]


def self_test():
    failures = []

    cases = (
        ("BUDGETS", "code", TOLERANCE_BYTES),
        ("RODATA_BUDGETS", "const", RODATA_TOLERANCE_BYTES),
        ("RAM_BUDGETS", "ram+tls", RAM_TOLERANCE_BYTES),
    )
    for key, label, tolerance in cases:
        budget = 10_000
        code, _ = compare(label, budget + tolerance + 1, budget, tolerance,
                          "fixture", report=False, key=key)
        if code == 0:
            failures.append(f"{label}: an over-budget size was accepted")
        code, _ = compare(label, budget, budget, tolerance,
                          "fixture", report=False, key=key)
        if code != 0:
            failures.append(f"{label}: an at-budget size was rejected")
        code, _ = compare(label, budget + tolerance, budget, tolerance,
                          "fixture", report=False, key=key)
        if code != 0:
            failures.append(f"{label}: a size exactly at the tolerance edge was rejected")

        # No budget recorded fails when enforcing and is fine under --report,
        # which is the whole mechanism of the two-step.
        code, _ = compare(label, 1234, None, tolerance, "fixture",
                          report=False, key=key)
        if code == 0:
            failures.append(f"{label}: a missing budget was accepted while enforcing")
        code, _ = compare(label, 1234, None, tolerance, "fixture",
                          report=True, key=key)
        if code != 0:
            failures.append(f"{label}: --report refused a missing budget")

    def expect(name, got, want):
        if got != want:
            failures.append(f"{name}: got {got}, want {want}")

    # Every row of the table, in each byte order and word size the readers have
    # to handle. ARM_EXIDX is an ARM type, so the non-ARM variants drop it.
    variants = [(False, False, EM_ARM), (True, False, EM_ARM),
                (False, True, EM_ARM), (True, True, 21)]  # 21: EM_PPC64, big-endian job
    for is64, big, machine in variants:
        tag = f"ELF{64 if is64 else 32}{'BE' if big else 'LE'}"
        rows = [s for s in PROBE_SECTIONS if machine == EM_ARM or s[1] != SHT_ARM_EXIDX]
        totals, listed = classify(Elf(build_elf(rows, is64=is64, big=big, machine=machine)))
        expect(f"{tag} code", totals["code"], 100)
        expect(f"{tag} const (.rodata + .ARM.exidx)", totals["const"],
               32 + (8 if machine == EM_ARM else 0))
        expect(f"{tag} ram (.bss + .data + .noinit + dyn .bss)", totals["ram"], 4 + 8 + 64 + 128)
        expect(f"{tag} ram_nobits", totals["ram_nobits"], 4 + 128)
        expect(f"{tag} ram_progbits (.noinit is PROGBITS)", totals["ram_progbits"], 8 + 64)
        expect(f"{tag} tls", totals["tls"], 16 + 32)
        expect(f"{tag} init_arrays bytes", totals["init_arrays"], 8)
        expect(f"{tag} init_array entries", totals["init_array_entries"], 8 // (8 if is64 else 4))
        if any(s["name"] in (".ARM.attributes", ".rel.text.f") for s in listed):
            failures.append(f"{tag}: a non-ALLOC section was classified")

    # Extended numbering (gABI): e_shnum = 0 and e_shstrndx = SHN_XINDEX put
    # the counts in section 0. Read wrongly, the file has no sections and
    # every class is 0.
    for is64 in (False, True):
        totals, listed = classify(Elf(build_elf(PROBE_SECTIONS, is64=is64, extended=True)))
        expect(f"extended numbering ELF{64 if is64 else 32}: ram", totals["ram"], 4 + 8 + 64 + 128)
        expect(f"extended numbering ELF{64 if is64 else 32}: names",
               sorted(s["name"] for s in listed)[0], ".ARM.exidx.text.f")

    # sh_entsize, when set, sizes an init_array entry; 0 falls back to a word.
    totals, _ = classify(Elf(build_elf([(".init_array", SHT_INIT_ARRAY, WA, 16, 8)])))
    expect("init_array entries by sh_entsize", totals["init_array_entries"], 2)

    # What the name-prefix sum this replaced would have missed on the same rows.
    by_name = sum(s[3] for s in PROBE_SECTIONS
                  if s[0].startswith((".bss.", ".data.")) or s[0] in (".bss", ".data"))
    totals, _ = classify(Elf(build_elf(PROBE_SECTIONS)))
    expect("ram + tls over the prefix sum (.tbss, .tdata, .noinit)",
           totals["ram"] + totals["tls"] - by_name, 16 + 32 + 64)

    # An object with no .bss at all is fine: only a total of zero is not.
    totals, _ = classify(Elf(build_elf([(".data.x", SHT_PROGBITS, WA, 12)])))
    expect("no .bss: ram", totals["ram"], 12)
    expect("no .bss: ram_nobits", totals["ram_nobits"], 0)

    # An ALLOC section of a type nobody taught the classifier must stop the run,
    # and so must a processor-specific type on another processor.
    for label, data in (
            ("unknown ALLOC type", build_elf([(".odd", 0x6FFF4700, A, 4)])),
            ("ARM_EXIDX on a non-ARM machine",
             build_elf([(".ARM.exidx", SHT_ARM_EXIDX, A, 8)], machine=21))):
        try:
            classify(Elf(data))
            failures.append(f"{label} was accepted")
        except ElfError:
            pass
    # ...but an unknown type that is not ALLOC is not this tool's business.
    totals, _ = classify(Elf(build_elf([(".x", 0x6FFF4700, 0, 4), (".t", SHT_PROGBITS, AX, 2)])))
    expect("non-ALLOC unknown type ignored, code", totals["code"], 2)

    # A linked image: .data counted once in RAM (run address) and once in flash
    # (load address). .bss is RAM only. Region bounds come from the image.
    region_symbols = {"__flash": 0x0, "__flash_size": 0x1000,
                      "__ram": 0x20000000, "__ram_size": 0x1000}
    text, data, bss = 0x200, 0x10, 0x40
    for is64, big in ((False, False), (True, True)):
        image = build_elf([(".text", SHT_PROGBITS, AX, text)],
                          segments=[(0x0, 0x0, text, text),
                                    (0x20000000, text, data, data),
                                    (0x20000000 + data, 0x20000000 + data, 0, bss)],
                          symbols=region_symbols, is64=is64, big=big, elf_type=2)
        expect(f"linked ELF{64 if is64 else 32} regions", regions(Elf(image)),
               {"flash": text + data, "ram": data + bss})

    # Only PT_LOAD is placed. A linked ARM image also has PT_ARM_EXIDX inside
    # flash and PT_TLS: counting them would count those bytes twice.
    image = build_elf([(".text", SHT_PROGBITS, AX, text)],
                      segments=[(0x0, 0x0, text, text), (0x100, 0x100, 8, 8, 0x70000001),
                                (0x20000000, 0x20000000, 0, 4, 7)],
                      symbols=region_symbols, elf_type=2)
    expect("non-PT_LOAD segments are not placed", regions(Elf(image)), {"flash": text, "ram": 0})

    for label, segment in (("a segment outside both regions", (0x10000000, 0x10000000, 4, 4)),
                           ("a segment straddling the end of RAM", (0x20000FF0, 0x0, 0, 0x20)),
                           ("RAM initialized from outside flash", (0x20000000, 0x30000000, 4, 4))):
        try:
            regions(Elf(build_elf([], segments=[segment], symbols=region_symbols, elf_type=2)))
            failures.append(f"{label} was accepted")
        except ElfError:
            pass

    # No region symbols (the toolchain's default script): not measured, not 0.
    expect("no region symbols", regions(Elf(build_elf([], segments=[(0, 0, 4, 4)], elf_type=2))),
           None)

    # .comment is where the toolchain names itself.
    expect("toolchain from .comment",
           Elf(build_elf([], comment=["GCC: (fixture) 14.2.0", "GCC: (fixture) 14.2.0"])).comment(),
           ["GCC: (fixture) 14.2.0"])

    # A file whose section names cannot be read is an error, not a file of
    # unnamed sections: e_shstrndx SHN_UNDEF, or an index past the table.
    for label, index in (("SHN_UNDEF", 0), ("out of range", 0xFEFF)):
        data = bytearray(build_elf(PROBE_SECTIONS))
        struct.pack_into("<H", data, 50, index)  # e_shstrndx in an ELF32 header
        try:
            Elf(bytes(data))
            failures.append(f"e_shstrndx {label} was read as unnamed sections")
        except ElfError as error:
            if "section-name string table" not in str(error):
                failures.append(f"e_shstrndx {label} failed without saying why: {error}")

    # A section whose contents run past the end of the file is an error, not a
    # shorter section: here `.comment`, whose bytes name the toolchain.
    data = bytearray(build_elf([], comment=["GCC: (fixture) 14.2.0"]))
    shoff = struct.unpack_from("<I", data, 32)[0]  # e_shoff in an ELF32 header
    struct.pack_into("<I", data, shoff + 40 + 20, 0x10000)  # section 1 (.comment) sh_size
    try:
        Elf(bytes(data)).comment()
        failures.append("a .comment past the end of the file was read")
    except ElfError:
        pass

    # Truncated and foreign files are errors, not zeros.
    for label, data in (("a truncated file", build_elf(PROBE_SECTIONS)[:80]),
                        ("a file that is not ELF", b"\xcf\xfa\xed\xfe" + bytes(60))):
        try:
            classify(Elf(data))
            failures.append(f"{label} was measured")
        except ElfError:
            pass

    # The record: unmeasured stays null, and a release refuses any gap.
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch)
        obj = root / "stack_probe.o"
        obj.write_bytes(build_elf(PROBE_SECTIONS))
        elf = root / "invariant_probe.elf"
        elf.write_bytes(build_elf([(".text", SHT_PROGBITS, AX, 64)], segments=[(0, 0, 64, 64)],
                                  elf_type=2))
        (root / "stack_probe.su").write_text("probe.cpp:1:1:void f()\t40\tstatic\n")
        # A fixed commit, never current_commit(): the self-test must not depend
        # on the directory it runs in or on GITHUB_SHA.
        commit = "c" * 40
        paths = []
        for cpu in sorted(RAM_BUDGETS):
            record = root / f"{cpu}.json"
            update_record(record, cpu, elf, measure(elf, linked=True), None, commit=commit)
            update_record(record, cpu, obj, measure(obj, linked=False), root, commit=commit)
            paths.append(record)
        one = json.loads(paths[0].read_text())
        expect("record: runtime stack is null", (one["stack"]["runtime_hwm"],
                                                 one["stack"]["runtime_hwm_lto"]), (None, None))
        expect("record: static frame", one["stack"]["static_max_frame"]["bytes"], 40)
        expect("record: default-script regions are null",
               one["artifacts"]["invariant_probe.elf"]["regions"], {"flash": None, "ram": None})
        expect("record: object ram", one["artifacts"]["stack_probe.o"]["classes"]["ram"], 204)
        if "not measured" not in summary(one):
            failures.append("the summary shows an unmeasured value as something else")

        try:
            merged = merge_records(paths, commit)
            expect("merged cpus", sorted(merged["records"]), sorted(RAM_BUDGETS))
        except ElfError as error:
            failures.append(f"a complete set of records was refused: {error}")
        stale = root / "stale.json"
        stale.write_text(json.dumps(dict(one, commit="0" * 40)))
        no_commit = root / "no_commit.json"
        no_commit.write_text(json.dumps(dict(one, commit=None)))
        no_stack = root / "no_stack.json"
        no_stack.write_text(json.dumps(dict(one, stack=empty_record("x")["stack"])))
        for label, inputs, want in (("a missing CPU", paths[1:], commit),
                                    ("a duplicate CPU", paths + paths[:1], commit),
                                    ("a record of another commit", [stale] + paths[1:], commit),
                                    ("a record without a stack figure", [no_stack] + paths[1:],
                                     commit)):
            try:
                merge_records(inputs, want)
                failures.append(f"{label} was released")
            except ElfError:
                pass
        try:
            update_record(paths[0], "cortex-m3" if one["cpu"] != "cortex-m3" else "cortex-m0",
                          obj, measure(obj, linked=False), None, commit=commit)
            failures.append("a record accepted a measurement for another CPU")
        except ElfError:
            pass

        # Without --expect-commit the records must still agree on one commit,
        # and a record that does not know its commit is never released.
        for label, inputs in (("records of two commits", [stale] + paths[1:]),
                              ("a record with no commit",
                               [no_commit] + paths[1:])):
            try:
                merge_records(inputs, None)
                failures.append(f"{label}: merged without --expect-commit")
            except ElfError:
                pass

        # A record left by an earlier run of another commit is replaced, not
        # added to: its commit and its artifacts both belong to that run.
        rerun = root / "rerun.json"
        update_record(rerun, "cortex-m3", obj, measure(obj, linked=False), None,
                      commit="a" * 40)
        update_record(rerun, "cortex-m3", elf, measure(elf, linked=True), None,
                      commit="b" * 40)
        again = json.loads(rerun.read_text())
        expect("re-run record: commit", again["commit"], "b" * 40)
        expect("re-run record: artifacts", sorted(again["artifacts"]), ["invariant_probe.elf"])

    # --compare: a grown and a shrunk class with the sections behind them, a
    # null that must not read as 0, a CPU on one side only, a single job's
    # record against a release, and a schema mismatch.
    def fixture(cpu, code, ram, text_bytes, hwm, regions):
        classes = dict.fromkeys(CLASSES, 0)
        classes.update(code=code, ram=ram, ram_nobits=ram)
        sections = [{"name": ".text.f", "type": "PROGBITS", "flags": "AX", "class": "code",
                     "bytes": text_bytes},
                    {"name": ".text.g", "type": "PROGBITS", "flags": "AX", "class": "code",
                     "bytes": code - text_bytes},
                    {"name": ".bss.pool", "type": "NOBITS", "flags": "WA", "class": "ram",
                     "bytes": ram}]
        record = empty_record(cpu, "c" * 40)
        record["artifacts"]["invariant_probe.elf"] = {
            "role": "linked-image", "toolchain": None, "flags": None, "classes": classes,
            "regions": {"flash": None, "ram": None}, "sections": sections}
        record["stack"].update(static_max_frame={"bytes": 96, "function": "f"}, runtime_hwm=hwm)
        record["instructions"] = {"source": "fixture", "regions": regions}
        return record
    old = {"schema": RECORD_SCHEMA, "commit": "a" * 40, "records": {
        "cortex-m3": fixture("cortex-m3", 1000, 256, 600, None, {"fill": 50}),
        "cortex-m0plus": fixture("cortex-m0plus", 900, 256, 500, 300, {})}}
    new = {"schema": RECORD_SCHEMA, "commit": "b" * 40, "records": {
        "cortex-m3": fixture("cortex-m3", 1040, 200, 640, 412, {"fill": 50, "drain": 7}),
        "cortex-m7": fixture("cortex-m7", 800, 256, 400, 300, {})}}
    table = compare_records(old, new)
    for want in ("| `invariant_probe.elf` code | 1000 | 1040 | +40 | +4.0% |",
                 "| `invariant_probe.elf` ram | 256 | 200 | -56 | -21.9% |",
                 "| `.text.f` | 600 | 640 | +40 |",
                 "| `.bss.pool` | 256 | 200 | -56 |",
                 "| stack runtime high-water mark | not measured | 412 |  |  |",
                 "| instructions drain | added | 7 | | |",
                 "| instructions fill | 50 | 50 | +0 | +0.0% |",
                 "Removed: only the old side measured this CPU.",
                 "Added: only the new side measured this CPU."):
        if want not in table:
            failures.append(f"--compare: no line {want!r} in\n{table}")
    if "`.text.g`" in table:
        failures.append("--compare: listed a section that did not change")
    if "| stack runtime high-water mark | not measured | 412 | +" in table:
        failures.append("--compare: read a null as 0 and printed a delta")
    for cpu, side in (("cortex-m0plus", "Removed"), ("cortex-m7", "Added")):
        if table.index(f"#### {cpu}") > table.index(f"{side}: only"):
            failures.append(f"--compare: {cpu} is not the CPU marked {side.lower()}")
    single = compare_records(old, new["records"]["cortex-m3"])
    if "cortex-m0plus" in single or "#### cortex-m3" not in single:
        failures.append(f"--compare: a single record was not compared with its own CPU only:\n{single}")
    try:
        compare_records(old, dict(new, schema=RECORD_SCHEMA + 1))
        failures.append("--compare: records of different schemas were compared")
    except ElfError:
        pass

    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("self-test passed: over-budget and missing-budget are rejected while "
          "at-budget and edge are accepted; every row of #405's table lands in its "
          "class in ELF32/64 LE/BE, an unknown ALLOC type fails, linked .data counts "
          "once in RAM and once in flash, a segment outside both regions fails, and "
          "a release refuses an incomplete set of records; --compare shows growth, "
          "shrinkage and the sections behind them, never reads null as 0, lists a CPU on "
          "one side only, and refuses a schema mismatch")
    return 0


if __name__ == "__main__":
    sys.exit(main())
