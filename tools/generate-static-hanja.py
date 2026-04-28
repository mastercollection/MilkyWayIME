#!/usr/bin/env python3
"""Generate committed C++ Hanja/symbol lookup tables.

The runtime does not depend on this script. It is a developer tool for updating
src/adapters/dictionary/generated_hanja_data.cpp from libhangul text data.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import argparse


ROOT = Path(__file__).resolve().parents[1]
HANJA_TXT = ROOT / "external" / "libhangul" / "data" / "hanja" / "hanja.txt"
SYMBOL_TXT = ROOT / "external" / "libhangul" / "data" / "hanja" / "mssymbol.txt"
OUTPUT_CPP = ROOT / "src" / "adapters" / "dictionary" / "generated_hanja_data.cpp"


@dataclass(frozen=True)
class SourceEntry:
    key: str
    value: str
    comment: str
    source_index: int


@dataclass(frozen=True)
class OffsetEntry:
    key: int
    value: int
    comment: int


@dataclass(frozen=True)
class Range:
    text: int
    first: int
    count: int


class StringPool:
    def __init__(self) -> None:
        self._offsets: dict[str, int] = {}
        self._chunks: list[str] = []
        self._size = 0
        self.add("")

    def add(self, text: str) -> int:
        existing = self._offsets.get(text)
        if existing is not None:
            return existing

        offset = self._size
        self._offsets[text] = offset
        self._chunks.append(text)
        self._size += len(text.encode("utf-8")) + 1
        return offset

    @property
    def chunks(self) -> list[str]:
        return self._chunks


def parse_libhangul_hanja_text(path: Path) -> list[SourceEntry]:
    entries: list[SourceEntry] = []
    with path.open("r", encoding="utf-8", newline="") as stream:
        for line in stream:
            if not line or line[0] in "#\r\n\0":
                continue

            line = line.rstrip("\r\n")
            first_colon = line.find(":")
            if first_colon < 0:
                continue

            key = line[:first_colon]
            if not key:
                continue

            rest = line[first_colon + 1 :]
            second_colon = rest.find(":")
            if second_colon < 0:
                value = rest
                comment = ""
            else:
                value = rest[:second_colon]
                comment = rest[second_colon + 1 :]

            if not value:
                continue

            entries.append(SourceEntry(key, value, comment, len(entries)))
    return entries


def add_entry_strings(entries: list[SourceEntry], pool: StringPool) -> list[OffsetEntry]:
    return [
        OffsetEntry(pool.add(entry.key), pool.add(entry.value), pool.add(entry.comment))
        for entry in entries
    ]


def utf8_sort_key(text: str) -> bytes:
    return text.encode("utf-8")


def build_order(entries: list[SourceEntry], field: str) -> list[int]:
    return sorted(
        range(len(entries)),
        key=lambda index: (utf8_sort_key(getattr(entries[index], field)),
                           entries[index].source_index),
    )


def build_ranges(
    entries: list[SourceEntry],
    offsets: list[OffsetEntry],
    order: list[int],
    field: str,
) -> list[Range]:
    ranges: list[Range] = []
    first = 0
    while first < len(order):
        entry_index = order[first]
        text = getattr(entries[entry_index], field)
        offset = getattr(offsets[entry_index], field)
        count = 1
        while first + count < len(order):
            next_index = order[first + count]
            if getattr(entries[next_index], field) != text:
                break
            count += 1
        ranges.append(Range(offset, first, count))
        first += count
    return ranges


def escape_cpp_string(text: str) -> str:
    escaped: list[str] = []
    for char in text:
        if char == "\\":
            escaped.append("\\\\")
        elif char == "\"":
            escaped.append("\\\"")
        elif char == "\t":
            escaped.append("\\t")
        elif char == "\r":
            escaped.append("\\r")
        elif char == "\n":
            escaped.append("\\n")
        elif ord(char) < 0x20:
            escaped.append(f"\\x{ord(char):02X}")
        else:
            escaped.append(char)
    return "".join(escaped)


def write_string_pool(stream, pool: StringPool) -> None:
    stream.write("static const char kStringPool[] =\n")
    chunk = ""
    chunk_size = 0
    for text in pool.chunks:
        piece = escape_cpp_string(text) + "\\000"
        piece_size = len(piece.encode("utf-8"))
        if chunk and chunk_size + piece_size > 8000:
            stream.write(f'    "{chunk}"\n')
            chunk = ""
            chunk_size = 0
        chunk += piece
        chunk_size += piece_size
    if chunk:
        stream.write(f'    "{chunk}"\n')
    stream.write("    ;\n\n")


def write_entries(stream, name: str, entries: list[OffsetEntry]) -> None:
    if not name.endswith("Entries"):
        raise ValueError(f"unexpected entry array name: {name}")
    count_name = name[:-7] + "EntryCount"
    stream.write(f"const HanjaEntry {name}[] = {{\n")
    for entry in entries:
        stream.write(f"    {{{entry.key}u, {entry.value}u, {entry.comment}u}},\n")
    stream.write("};\n")
    stream.write(f"const std::uint32_t {count_name} = {len(entries)}u;\n\n")


def write_ranges(stream, name: str, ranges: list[Range]) -> None:
    stream.write(f"const RangeEntry {name}[] = {{\n")
    for item in ranges:
        stream.write(f"    {{{item.text}u, {item.first}u, {item.count}u}},\n")
    stream.write("};\n")
    stream.write(f"const std::uint32_t {name[:-1]}Count = {len(ranges)}u;\n\n")


def write_order(stream, name: str, order: list[int]) -> None:
    stream.write(f"const std::uint32_t {name}[] = {{\n")
    for index in range(0, len(order), 12):
        line = ", ".join(f"{value}u" for value in order[index : index + 12])
        stream.write(f"    {line},\n")
    stream.write("};\n\n")


def generate(hanja_txt: Path, symbol_txt: Path, output_cpp: Path) -> None:
    hanja_entries = parse_libhangul_hanja_text(hanja_txt)
    symbol_entries = parse_libhangul_hanja_text(symbol_txt)

    pool = StringPool()
    hanja_offsets = add_entry_strings(hanja_entries, pool)
    symbol_offsets = add_entry_strings(symbol_entries, pool)

    hanja_key_order = build_order(hanja_entries, "key")
    hanja_value_order = build_order(hanja_entries, "value")
    symbol_key_order = build_order(symbol_entries, "key")

    hanja_key_ranges = build_ranges(hanja_entries, hanja_offsets,
                                    hanja_key_order, "key")
    hanja_value_ranges = build_ranges(hanja_entries, hanja_offsets,
                                      hanja_value_order, "value")
    symbol_key_ranges = build_ranges(symbol_entries, symbol_offsets,
                                     symbol_key_order, "key")

    output_cpp.parent.mkdir(parents=True, exist_ok=True)
    with output_cpp.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("// Generated by tools/generate-static-hanja.py. Do not edit.\n\n")
        stream.write("#include \"adapters/dictionary/generated_hanja_data.h\"\n\n")
        stream.write("namespace milkyway::adapters::dictionary::generated {\n")
        stream.write("namespace {\n\n")
        write_string_pool(stream, pool)
        stream.write("}  // namespace\n\n")
        stream.write(
            "const char* StringAt(std::uint32_t offset) {\n"
            "  return kStringPool + offset;\n"
            "}\n\n"
        )
        write_entries(stream, "kHanjaEntries", hanja_offsets)
        write_ranges(stream, "kHanjaKeyRanges", hanja_key_ranges)
        write_order(stream, "kHanjaKeyEntryOrder", hanja_key_order)
        write_ranges(stream, "kHanjaValueRanges", hanja_value_ranges)
        write_order(stream, "kHanjaValueEntryOrder", hanja_value_order)
        write_entries(stream, "kSymbolEntries", symbol_offsets)
        write_ranges(stream, "kSymbolKeyRanges", symbol_key_ranges)
        write_order(stream, "kSymbolKeyEntryOrder", symbol_key_order)
        stream.write("}  // namespace milkyway::adapters::dictionary::generated\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--hanja", type=Path, default=HANJA_TXT)
    parser.add_argument("--symbols", type=Path, default=SYMBOL_TXT)
    parser.add_argument("--output", type=Path, default=OUTPUT_CPP)
    args = parser.parse_args()

    generate(args.hanja, args.symbols, args.output)
    print(f"Generated {args.output}")


if __name__ == "__main__":
    main()
