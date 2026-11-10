# SPDX-License-Identifier: MIT
"""Mass Effect Legendary Edition, ME1 saves (.pcsav): read, edit, write.

Reads an LE1 save into plain Python values (dicts, lists, numbers, strings), lets a few things change (the level, the
player's place and facing, plot booleans/integers/floats, the shown name) and writes it back. An unchanged save writes back
byte for byte. Used by tools/conversation-launch.ps1 -Save to reach a conversation from a career's own plot state by
loading a save instead of opening a level from the console (research/59 "The launcher").

Format knowledge: Trilogy Save Editor by KarlitosVII (https://github.com/KarlitosVII/trilogy-save-editor, CeCILL-2.1),
src/save_data/mass_effect_1_le and src/unreal; no code copied (this is our own Python; NOTICE.md lists the source). Fetch
it into reference/ with tools/fetch-references.ps1 to read the structs.

The container: magic C1 83 2A 9E, block size (0x40000), a (compressed, uncompressed) pair for the whole payload, one pair
per zlib chunk of at most a block, the chunks, then CRC-32/BZIP2 of everything before it, the compression flag (1 =
zlib) and the payload's size again. The payload is Unreal's flat serialization: little-endian; bool = u32 0/1; enum = u8;
string = i32 length with its terminating 0 (negative: that many UTF-16 units), 0 = empty; array/map = u32 count; option =
bool then the value. The game's deflate isn't zlib's, so a changed payload compresses differently but loads the same.

  python tools/le1-save.py info <save>                       name, map, place, facing, time, plot sizes, maps kept
  python tools/le1-save.py roundtrip <save>...               parse and write each back: payload and file byte for byte
  python tools/le1-save.py plot <save> --bool N [--int N]    read plot values
  python tools/le1-save.py write <in> <out> --level BIOA_STA00 --location X Y Z --yaw DEG [--bool N=1] [--int N=V]
  python tools/le1-save.py diff <before> <after> --rewind-out beat.json   what a beat set (with the plot's names)
  python tools/le1-save.py write <later> <out> --rewind beat.json [--with-maps]   ... put back: the beat plays again
                                                             a new save (the edits listed in `write -h`)
"""
from __future__ import annotations

import argparse
import base64
import datetime
import json
import math
import os
import re
import struct
import sys
import zlib

MAGIC = 0x9E2A83C1
VERSION = 50
BLOCK = 0x40000


# ---- CRC-32/BZIP2 (poly 0x04C11DB7, not reflected, init and xorout 0xFFFFFFFF) ----

def _crc_table() -> list[int]:
    table = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) if c & 0x80000000 else (c << 1)
        table.append(c & 0xFFFFFFFF)
    return table


_CRC = _crc_table()


def crc32_bzip2(data: bytes) -> int:
    crc = 0xFFFFFFFF
    table = _CRC
    for b in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ table[((crc >> 24) ^ b) & 0xFF]
    return crc ^ 0xFFFFFFFF


# ---- Strings keep how they were stored ----

class UStr(str):
    """A string read from a save; `wide` says it was stored as UTF-16, so writing it back keeps the bytes."""
    wide: bool = False


def ustr(value: str, wide: bool | None = None) -> UStr:
    s = UStr(value)
    s.wide = any(ord(c) > 0xFF for c in value) if wide is None else wide
    return s


# ---- The schema: types are names, tuples or struct names ----
# 'i32' 'u32' 'u8' 'f32' 'bool' 'str'; ('vec', T); ('opt', T); ('map', K, V); ('bytes', n); a key of STRUCTS;
# 'object' is a legacy BaseObject (its class name picks its body).

V3 = 'Vector'
STRUCTS: dict[str, list[tuple[str, object]]] = {
    'Vector': [('x', 'f32'), ('y', 'f32'), ('z', 'f32')],
    'Rotator': [('pitch', 'i32'), ('yaw', 'i32'), ('roll', 'i32')],
    'LinearColor': [('r', 'f32'), ('g', 'f32'), ('b', 'f32'), ('a', 'f32')],
    'TimeStamp': [('seconds_since_midnight', 'i32'), ('day', 'i32'), ('month', 'i32'), ('year', 'i32')],
    'Plot': [('booleans', ('vec', 'u32')), ('integers', ('vec', 'i32')), ('floats', ('vec', 'f32'))],
    'PlotQuest': [('quest_counter', 'i32'), ('quest_updated', 'bool'), ('history', ('vec', 'i32'))],
    'Journal': [('quest_progress_counter', 'i32'), ('quest_progress', ('vec', 'PlotQuest')), ('quest_ids', ('vec', 'i32'))],
    'CodexPage': [('page', 'i32'), ('is_new', 'bool')],
    'PlotCodex': [('pages', ('vec', 'CodexPage'))],
    'Codex': [('codex_entries', ('vec', 'PlotCodex')), ('codex_ids', ('vec', 'i32'))],
    'SimpleTalent': [('talent_id', 'i32'), ('current_rank', 'i32')],
    'ComplexTalent': [('talent_id', 'i32'), ('current_rank', 'i32'), ('max_rank', 'i32'), ('level_offset', 'i32'),
                      ('levels_per_rank', 'i32'), ('visual_order', 'i32'), ('prereq_talent_ids', ('vec', 'i32')),
                      ('prereq_talent_ranks', ('vec', 'i32'))],
    'ItemMod': [('item_id', 'i32'), ('item_level', 'u8'), ('manufacturer_id', 'i32'), ('plot_conditional_id', 'i32')],
    'Item': [('item_id', 'i32'), ('item_level', 'u8'), ('manufacturer_id', 'i32'), ('plot_conditional_id', 'i32'),
             ('new_item', 'bool'), ('junk', 'bool'), ('attached_mods', ('vec', 'ItemMod'))],
    'PlayerInventory': [('equipment', ('vec', 'Item')), ('quick_slots', ('vec', 'Item')), ('inventory', ('vec', 'Item')),
                        ('buy_pack', ('vec', 'Item'))],
    'HeadMorph': [('hair_mesh', 'str'), ('accessory_mesh', ('vec', 'str')), ('morph_features', ('map', 'str', 'f32')),
                  ('offset_bones', ('map', 'str', V3)), ('lod0_vertices', ('vec', V3)), ('lod1_vertices', ('vec', V3)),
                  ('lod2_vertices', ('vec', V3)), ('lod3_vertices', ('vec', V3)),
                  ('scalar_parameters', ('map', 'str', 'f32')), ('vector_parameters', ('map', 'str', 'LinearColor')),
                  ('texture_parameters', ('map', 'str', 'str'))],
    'Hotkey': [('pawn', 'i32'), ('event', 'i32')],
    'Player': [('is_female', 'bool'), ('localized_class_name', 'i32'), ('player_class', 'u8'), ('level', 'i32'),
               ('current_xp', 'f32'), ('first_name', 'str'), ('localized_last_name', 'i32'), ('origin', 'u8'),
               ('notoriety', 'u8'), ('specialization_bonus_id', 'i32'), ('spectre_rank', 'u8'), ('talent_points', 'i32'),
               ('talent_pool_points', 'i32'), ('mapped_talent', 'str'), ('head_morph', ('opt', 'HeadMorph')),
               ('simple_talents', ('vec', 'SimpleTalent')), ('complex_talents', ('vec', 'ComplexTalent')),
               ('inventory', 'PlayerInventory'), ('credits', 'i32'), ('medigel', 'i32'), ('grenades', 'f32'),
               ('omnigel', 'f32'), ('face_code', 'str'), ('armor_overridden', 'bool'), ('auto_levelup_template_id', 'i32'),
               ('health_per_level', 'f32'), ('stability', 'f32'), ('race', 'u8'), ('toxic', 'f32'), ('stamina', 'i32'),
               ('focus', 'i32'), ('precision', 'i32'), ('coordination', 'i32'), ('attribute_primary', 'u8'),
               ('attribute_secondary', 'u8'), ('skill_charm', 'f32'), ('skill_intimidate', 'f32'), ('skill_haggle', 'f32'),
               ('health', 'f32'), ('shield', 'f32'), ('xp_level', 'i32'), ('is_driving', 'bool'),
               ('game_options', ('vec', 'i32')), ('helmet_shown', 'bool'), ('unknown', ('bytes', 5)), ('last_power', 'str'),
               ('health_max', 'f32'), ('hotkeys', ('vec', 'Hotkey')), ('primary_weapon', 'str'), ('secondary_weapon', 'str')],
    'Henchman': [('tag', 'str'), ('simple_talents', ('vec', 'SimpleTalent')), ('complex_talents', ('vec', 'ComplexTalent')),
                 ('equipment', ('vec', 'Item')), ('quick_slots', ('vec', 'Item')), ('talent_points', 'i32'),
                 ('talent_pool_points', 'i32'), ('auto_levelup_template_id', 'i32'), ('localized_last_name', 'i32'),
                 ('localized_class_name', 'i32'), ('class_base', 'u8'), ('health_per_level', 'f32'), ('stability', 'f32'),
                 ('gender', 'u8'), ('race', 'u8'), ('toxic', 'f32'), ('stamina', 'i32'), ('focus', 'i32'),
                 ('precision', 'i32'), ('coordination', 'i32'), ('attribute_primary', 'u8'), ('attribute_secondary', 'u8'),
                 ('health', 'f32'), ('shield', 'f32'), ('level', 'i32'), ('helmet_shown', 'bool'),
                 ('current_quick_slot', 'u8'), ('health_max', 'f32')],
    # The world state the game keeps per map it has visited (NoExport, absent from a character export).
    'Proxy': [('proxy', ('opt', 'object'))],
    'Level': [('objects', ('vec', 'object')), ('actors', ('vec', 'str'))],
    'Map': [('levels', ('map', 'str', 'Level')), ('world', ('opt', 'object'))],
    'Mako': [('first_name', 'str'), ('localized_last_name', 'i32'), ('health', 'f32'), ('shield', 'f32')],
    'NoExport': [('legacy_maps', ('map', 'str', 'Map')), ('mako', 'Mako')],
    # The legacy objects' bodies, by their class name.
    'PawnBehavior': [('is_dead', 'bool'), ('generated_treasure', 'bool'), ('challenge_scaled', 'bool'),
                     ('owner', ('opt', 'object')), ('health', 'f32'), ('shield', 'f32'), ('first_name', 'str'),
                     ('localized_last_name', 'i32'), ('health_max', 'f32'), ('health_regen_rate', 'f32'),
                     ('radar_range', 'f32'), ('level', 'i32'), ('health_per_level', 'f32'), ('stability', 'f32'),
                     ('gender', 'u8'), ('race', 'u8'), ('toxic', 'f32'), ('stamina', 'i32'), ('focus', 'i32'),
                     ('precision', 'i32'), ('coordination', 'i32'), ('quick_slot', 'u8'), ('squad', ('opt', 'object')),
                     ('inventory', ('opt', 'object')), ('unknown', ('bytes', 3)), ('experience', 'i32'),
                     ('talent_points', 'i32'), ('talent_pool_points', 'i32'), ('attribute_primary', 'u8'),
                     ('attribute_secondary', 'u8'), ('class_base', 'u8'), ('localized_class_name', 'i32'),
                     ('auto_level_up_template_id', 'i32'), ('spectre_rank', 'u8'), ('background_origin', 'u8'),
                     ('background_notoriety', 'u8'), ('specialization_bonus_id', 'u8'), ('skill_charm', 'f32'),
                     ('skill_intimidate', 'f32'), ('skill_haggle', 'f32'), ('audibility', 'f32'), ('blindness', 'f32'),
                     ('damage_duration_mult', 'f32'), ('deafness', 'f32'), ('unlootable_grenade_count', 'i32'),
                     ('head_gear_visible_preference', 'bool'), ('simple_talents', ('vec', 'SimpleTalent')),
                     ('complex_talents', ('vec', 'ComplexTalent')), ('quick_slots', ('vec', 'Proxy')),
                     ('equipment', ('vec', 'Proxy'))],
    'Pawn': [('location', V3), ('rotation', 'Rotator'), ('velocity', V3), ('acceleration', V3),
             ('script_initialized', 'bool'), ('hidden', 'bool'), ('stasis', 'bool'), ('grime_level', 'f32'),
             ('grime_dirt_level', 'f32'), ('talked_to_count', 'i32'), ('head_gear_visible_preference', 'bool')],
    'BaseSquad': [('inventory', ('opt', 'object'))],
    'Shop': [('last_player_level', 'i32'), ('is_initialized', 'bool'), ('inventory', ('vec', 'Proxy'))],
    'PlotItem': [('localized_name', 'i32'), ('localized_desc', 'i32'), ('export_id', 'i32'), ('base_price', 'i32'),
                 ('shop_gui_image_id', 'i32'), ('plot_conditional_id', 'i32')],
    'Inventory': [('items', ('vec', 'object')), ('plot_items', ('vec', 'PlotItem')), ('credits', 'i32'),
                  ('grenades', 'i32'), ('medigel', 'f32'), ('omnigel', 'f32')],
    'SlotSpec': [('type_id', 'i32'), ('mods', ('vec', 'Proxy'))],
    'LegacyItem': [('item_id', 'i32'), ('item_level', 'u8'), ('manufacturer_id', 'i32'), ('plot_conditional_id', 'i32'),
                   ('slot_specs', ('vec', 'SlotSpec'))],
    'LegacyItemMod': [('item_id', 'i32'), ('item_level', 'u8'), ('manufacturer_id', 'i32'), ('plot_conditional_id', 'i32'),
                      ('type_id', 'i32')],
    'ArtPlaceableBehavior': [('is_dead', 'bool'), ('generated_treasure', 'bool'), ('challenge_scaled', 'bool'),
                             ('owner', ('opt', 'object')), ('health', 'f32'), ('current_health', 'f32'),
                             ('enabled', 'bool'), ('current_fsm_state_name', 'str'), ('is_destroyed', 'bool'),
                             ('state_0', 'str'), ('state_1', 'str'), ('use_case', 'u8'), ('use_case_override', 'bool'),
                             ('player_only', 'bool'), ('skill_difficulty', 'u8'), ('inventory', ('opt', 'object')),
                             ('skill_game_failed', 'bool'), ('skill_game_xp_awarded', 'bool')],
    'ArtPlaceable': [('unknown', ('bytes', 60))],
    'VehicleBehavior': [('actor_type', 'str'), ('powertrain_enabled', 'bool'), ('vehicle_function_enabled', 'bool'),
                        ('owner', ('opt', 'object'))],
    'Vehicle': [('location', V3), ('rotation', 'Rotator'), ('velocity', V3), ('acceleration', V3),
                ('script_initialized', 'bool'), ('hidden', 'bool'), ('stasis', 'bool'), ('health', 'f32'),
                ('shield', 'f32'), ('first_name', 'str'), ('localized_last_name', 'i32'), ('unknown', ('bytes', 16))],
    'StreamingState': [('name', 'str'), ('enabled', 'u8')],
    'World': [('streaming_states', ('vec', 'StreamingState')), ('destination_area_map', 'str'), ('destination', V3),
              ('cinematics_seen', ('vec', 'str')), ('scanned_clusters', ('vec', 'i32')), ('scanned_systems', ('vec', 'i32')),
              ('scanned_planets', ('vec', 'i32')), ('journal_sort_method', 'u8'), ('journal_showing_missions', 'bool'),
              ('journal_last_selected_mission', 'i32'), ('journal_last_selected_assignment', 'i32'),
              ('codex_showing_primary', 'bool'), ('codex_last_selected_primary', 'i32'),
              ('codex_last_selected_secondary', 'i32'), ('current_tip_id', 'i32'), ('override_tip', 'i32'),
              ('browser_alerts', ('bytes', 8)), ('pending_loot', ('opt', 'object'))],
    # The save itself (no_export is read only when bytes remain: a character export ends before it).
    'SaveData': [('version', 'i32'), ('character_id', 'str'), ('created_date', 'TimeStamp'), ('plot', 'Plot'),
                 ('journal', 'Journal'), ('codex', 'Codex'), ('timestamp', 'TimeStamp'), ('seconds_played', 'i32'),
                 ('player', 'Player'), ('base_level_name', 'str'), ('map_name', 'str'), ('parent_map_name', 'str'),
                 ('location', V3), ('rotation', 'Rotator'), ('squad', ('vec', 'Henchman')), ('display_name', 'str'),
                 ('file_name', 'str')],
}

OBJECT_BODIES = {
    'BioPawnBehaviorSaveObject': 'PawnBehavior',
    'BioPawnSaveObject': 'Pawn',
    'BioBaseSquadSaveObject': 'BaseSquad',
    'BioShopSaveObject': 'Shop',
    'BioInventorySaveObject': 'Inventory',
    'BioItemXModdableSaveObject': 'LegacyItem',
    'BioItemXModSaveObject': 'LegacyItemMod',
    'BioArtPlaceableBehaviorSaveObject': 'ArtPlaceableBehavior',
    'BioArtPlaceableSaveObject': 'ArtPlaceable',
    'BioVehicleBehaviorSaveObject': 'VehicleBehavior',
    'BioVehicleSaveObject': 'Vehicle',
    'BioWorldInfoSaveObject': 'World',
}


class SaveError(Exception):
    pass


# ---- Reading ----

class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def take(self, n: int) -> bytes:
        if n < 0 or self.pos + n > len(self.data):
            raise SaveError(f'unexpected end of data at {self.pos} (wanted {n} bytes)')
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def unpack(self, fmt: str):
        size = struct.calcsize(fmt)
        v = struct.unpack_from(fmt, self.data, self.pos)
        if self.pos + size > len(self.data):
            raise SaveError(f'unexpected end of data at {self.pos}')
        self.pos += size
        return v[0]

    def read(self, t, path: str = ''):
        if t == 'i32':
            return self.unpack('<i')
        if t == 'u32':
            return self.unpack('<I')
        if t == 'u8':
            return self.unpack('<B')
        if t == 'f32':
            return self.unpack('<f')
        if t == 'bool':
            at = self.pos
            v = self.unpack('<I')
            if v not in (0, 1):
                raise SaveError(f'{path}: a bool of {v} at {at}')
            return v == 1
        if t == 'str':
            n = self.unpack('<i')
            if n == 0:
                return ustr('', False)
            if n < 0:
                raw = self.take(-n * 2)
                return ustr(raw.decode('utf-16-le')[:-1], True)
            raw = self.take(n)
            return ustr(raw.decode('cp1252', errors='strict')[:-1], False)
        if t == 'object':
            return self.read_object(path)
        if isinstance(t, tuple):
            kind = t[0]
            if kind == 'vec':
                n = self.unpack('<I')
                return [self.read(t[1], f'{path}[{i}]') for i in range(n)]
            if kind == 'opt':
                return self.read(t[1], path) if self.read('bool', path) else None
            if kind == 'map':
                n = self.unpack('<I')
                out = {}
                for _ in range(n):
                    k = self.read(t[1], path)
                    out[k] = self.read(t[2], f'{path}[{k}]')
                return out
            if kind == 'bytes':
                return self.take(t[1])
        if t in STRUCTS:
            return {name: self.read(ft, f'{path}.{name}') for name, ft in STRUCTS[t]}
        raise SaveError(f'unknown type {t!r}')

    def read_object(self, path: str) -> dict:
        cls = self.read('str', path)
        body = OBJECT_BODIES.get(cls)
        if body is None:
            raise SaveError(f'{path}: unknown legacy object class {cls!r} at {self.pos}')
        return {'class_name': cls, 'owner_name': self.read('str', path),
                'owner_class': self.read(('opt', 'str'), path), 'body': self.read(body, f'{path}<{cls}>')}


# ---- Writing ----

class Writer:
    def __init__(self):
        self.out = bytearray()

    def write(self, t, v, path: str = ''):
        o = self.out
        if t == 'i32':
            o += struct.pack('<i', v)
        elif t == 'u32':
            o += struct.pack('<I', v)
        elif t == 'u8':
            o += struct.pack('<B', v)
        elif t == 'f32':
            o += struct.pack('<f', v)
        elif t == 'bool':
            o += struct.pack('<I', 1 if v else 0)
        elif t == 'str':
            if v == '':
                o += struct.pack('<i', 0)
            else:
                wide = v.wide if isinstance(v, UStr) else any(ord(c) > 0xFF for c in v)
                if wide:
                    raw = (v + '\0').encode('utf-16-le')
                    o += struct.pack('<i', -(len(raw) // 2)) + raw
                else:
                    raw = (v + '\0').encode('cp1252')
                    o += struct.pack('<i', len(raw)) + raw
        elif t == 'object':
            self.write('str', v['class_name'])
            self.write('str', v['owner_name'])
            self.write(('opt', 'str'), v['owner_class'])
            self.write(OBJECT_BODIES[v['class_name']], v['body'], path)
        elif isinstance(t, tuple):
            kind = t[0]
            if kind == 'vec':
                o += struct.pack('<I', len(v))
                for item in v:
                    self.write(t[1], item, path)
            elif kind == 'opt':
                self.write('bool', v is not None)
                if v is not None:
                    self.write(t[1], v, path)
            elif kind == 'map':
                o += struct.pack('<I', len(v))
                for k, item in v.items():
                    self.write(t[1], k, path)
                    self.write(t[2], item, path)
            elif kind == 'bytes':
                if len(v) != t[1]:
                    raise SaveError(f'{path}: {len(v)} bytes where {t[1]} belong')
                o += v
            else:
                raise SaveError(f'unknown type {t!r}')
        elif t in STRUCTS:
            for name, ft in STRUCTS[t]:
                self.write(ft, v[name], f'{path}.{name}')
        else:
            raise SaveError(f'unknown type {t!r}')


def parse_payload(payload: bytes) -> dict:
    r = Reader(payload)
    data = r.read('SaveData', 'save')
    if data['version'] != VERSION:
        raise SaveError(f"save version {data['version']}, not {VERSION} (only the current Legendary Edition's)")
    data['no_export'] = r.read('NoExport', 'save.no_export') if r.pos < len(payload) else None
    if r.pos != len(payload):
        raise SaveError(f'{len(payload) - r.pos} bytes left after the save data')
    return data


def build_payload(data: dict) -> bytes:
    w = Writer()
    w.write('SaveData', data, 'save')
    if data.get('no_export') is not None:
        w.write('NoExport', data['no_export'], 'save.no_export')
    return bytes(w.out)


# ---- The container ----

class Save:
    """A save file: its data, plus what it was read from so an unchanged save writes back byte for byte."""

    def __init__(self, data: dict, block: int = BLOCK, compression_flag: int = 1,
                 original: tuple[bytes, list[bytes]] | None = None):
        self.data = data
        self.block = block
        self.compression_flag = compression_flag
        self._original = original  # (payload, compressed chunks as read)

    @classmethod
    def from_bytes(cls, b: bytes) -> 'Save':
        if len(b) < 32:
            raise SaveError('too short for a save')
        magic, block = struct.unpack_from('<II', b, 0)
        if magic != MAGIC:
            raise SaveError(f'not an LE1 save (magic {magic:08X})')
        total_c, total_u = struct.unpack_from('<II', b, 8)
        off = 16
        headers = []
        seen_u = 0
        while seen_u < total_u:
            c, u = struct.unpack_from('<II', b, off)
            off += 8
            if u == 0 or u > block:
                raise SaveError(f'chunk header ({c}, {u}) out of range')
            headers.append((c, u))
            seen_u += u
        if seen_u != total_u or sum(c for c, _ in headers) != total_c:
            raise SaveError('chunk headers disagree with the totals')
        chunks, payload = [], bytearray()
        for c, u in headers:
            comp = b[off:off + c]
            off += c
            raw = zlib.decompress(comp)
            if len(raw) != u:
                raise SaveError(f'a chunk inflated to {len(raw)} bytes, its header says {u}')
            chunks.append(comp)
            payload += raw
        if len(b) - off != 12:
            raise SaveError(f'{len(b) - off} bytes after the chunks, 12 belong')
        checksum, flag, size = struct.unpack_from('<III', b, off)
        if checksum != crc32_bzip2(b[:off]):
            raise SaveError(f'checksum {checksum:08X} does not match the file ({crc32_bzip2(b[:off]):08X})')
        if size != total_u:
            raise SaveError('the trailing size disagrees with the header')
        payload = bytes(payload)
        return cls(parse_payload(payload), block, flag, (payload, chunks))

    @classmethod
    def load(cls, path: str) -> 'Save':
        with open(path, 'rb') as f:
            return cls.from_bytes(f.read())

    def to_bytes(self) -> bytes:
        payload = build_payload(self.data)
        if self._original is not None and payload == self._original[0]:
            chunks = self._original[1]  # unchanged: the game's own compressed bytes
        else:
            chunks = [zlib.compress(payload[i:i + self.block], 6) for i in range(0, len(payload), self.block)]
        sizes = [min(self.block, len(payload) - i) for i in range(0, len(payload), self.block)]
        out = bytearray(struct.pack('<II', MAGIC, self.block))
        out += struct.pack('<II', sum(len(c) for c in chunks), len(payload))
        for c, u in zip(chunks, sizes):
            out += struct.pack('<II', len(c), u)
        for c in chunks:
            out += c
        out += struct.pack('<III', crc32_bzip2(bytes(out)), self.compression_flag, len(payload))
        return bytes(out)

    def save(self, path: str) -> None:
        tmp = path + '.tmp'
        with open(tmp, 'wb') as f:
            f.write(self.to_bytes())
        os.replace(tmp, path)

    # ---- Edits ----

    def plot_bool(self, index: int) -> bool:
        words = self.data['plot']['booleans']
        w, bit = divmod(index, 32)
        return w < len(words) and bool(words[w] >> bit & 1)

    def set_plot_bool(self, index: int, value: bool) -> None:
        words = self.data['plot']['booleans']
        w, bit = divmod(index, 32)
        while len(words) <= w:
            words.append(0)
        words[w] = (words[w] | (1 << bit)) if value else (words[w] & ~(1 << bit) & 0xFFFFFFFF)

    def set_plot_int(self, index: int, value: int) -> None:
        ints = self.data['plot']['integers']
        while len(ints) <= index:
            ints.append(0)
        ints[index] = value

    def set_plot_float(self, index: int, value: float) -> None:
        floats = self.data['plot']['floats']
        while len(floats) <= index:
            floats.append(0.0)
        floats[index] = value

    def set_place(self, location=None, yaw_deg: float | None = None, pitch_deg: float = 0.0) -> None:
        if location is not None:
            self.data['location'] = {'x': float(location[0]), 'y': float(location[1]), 'z': float(location[2])}
        if yaw_deg is not None:
            self.data['rotation'] = {'pitch': unreal_angle(pitch_deg), 'yaw': unreal_angle(yaw_deg), 'roll': 0}

    def set_stream_state(self, map_name: str, state: str, enabled: bool) -> str:
        """A visited map's kept streaming state (its BioWorldInfoSaveObject), added when the map has none of that name."""
        ne = self.data.get('no_export')
        maps = ne['legacy_maps'] if ne is not None else {}
        key = next((k for k in maps if k.lower() == map_name.lower()), None)
        if key is None:
            raise SystemExit(f"{map_name} is not among the maps the save keeps ({', '.join(maps) or 'none'})")
        w = maps[key]['world']
        if w is None or w['class_name'] != 'BioWorldInfoSaveObject':
            raise SystemExit(f'{key} keeps no world info to hold a streaming state')
        states = w['body']['streaming_states']
        for st in states:
            if st['name'].lower() == state.lower():
                was = bool(st['enabled'])
                st['enabled'] = 1 if enabled else 0
                return f"{key}: streaming state {st['name']} {int(was)} -> {int(enabled)}"
        states.append({'name': ustr(state), 'enabled': 1 if enabled else 0})
        return f'{key}: streaming state {state} added as {int(enabled)}'

    def set_level(self, level: str | None = None, area: str | None = None, parent: str | None = None) -> None:
        """Where the save loads. base_level_name is the persistent level the game opens (BIOA_STA00); map_name is the
        area inside it the menus name (MapSTA30_02 at C-Sec, MapNOR01 on the Normandy docked there); parent_map_name
        is empty in every save we have."""
        if level is not None:
            self.data['base_level_name'] = ustr(level)
        if area is not None:
            self.data['map_name'] = ustr(area)
        if parent is not None:
            self.data['parent_map_name'] = ustr(parent)

    def stamp_now(self) -> None:
        """The save's time to now (the newest save of its career), and the date in the Load menu's text with it."""
        now = datetime.datetime.now()
        self.data['timestamp'] = {'seconds_since_midnight': now.hour * 3600 + now.minute * 60 + now.second,
                                  'day': now.day, 'month': now.month, 'year': now.year}
        shown = now.strftime('%Y/%m/%d %H:%M:%S')
        name = self.data['display_name']
        self.data['display_name'] = ustr(DATE_IN_NAME.sub(shown, name, count=1), getattr(name, 'wide', None))

    def set_file_name(self, stem: str) -> None:
        """The name the save knows itself by (its file's name without .pcsav, as the game writes it). For a numbered save
        (<career>_<n>) the display text's first field, the slot number right-aligned in 3 characters, follows: the game's save
        record (BioSaveGame.SaveGameRecord: m_SaveGameNumber, the date, the area, level and time played) reads that text."""
        self.data['file_name'] = ustr(stem)
        m = SLOT_IN_FILE.match(stem)
        name = self.data['display_name']
        if m and len(name) >= 3:
            self.data['display_name'] = ustr(f'{int(m.group(1)):>3}' + name[3:], getattr(name, 'wide', None))


DATE_IN_NAME = re.compile(r'\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}')
SLOT_IN_FILE = re.compile(r'^.+_(\d+)$')


def unreal_angle(deg: float) -> int:
    """Degrees to Unreal's 65536 units a turn, in 0..65535."""
    return int(round((deg % 360.0) * 65536 / 360.0)) % 65536


def angle_deg(units: int) -> float:
    return (units % 65536) * 360.0 / 65536


# ---- Rewinding a story beat ----
# A beat (a conversation, a scene) sets a few plot values when it plays: 28 minutes of the Citadel set 21 booleans and 8
# integers in the owner's career. A save from just before it and one from just after give the list (`diff`); putting those
# values back on any later save (`write --rewind`) makes the beat's conditions read "not yet played". The maps' kept world
# state (who is dead, moved, looted) is recorded too, and put back only on request (`--with-maps`): it also undoes everything
# else done in that map since.

REWIND_FORMAT = 'mevr-le1-rewind 1'


def serialize(t, value) -> bytes:
    w = Writer()
    w.write(t, value)
    return bytes(w.out)


def plot_bits(save: Save) -> set[int]:
    return {i * 32 + b for i, word in enumerate(save.data['plot']['booleans']) for b in range(32) if word >> b & 1}


def _changed(a: list, b: list, empty):
    n = max(len(a), len(b))
    return {i: (a[i] if i < len(a) else empty) for i in range(n)
            if (a[i] if i < len(a) else empty) != (b[i] if i < len(b) else empty)}


def _maps(save: Save) -> dict:
    ne = save.data.get('no_export')
    return ne['legacy_maps'] if ne is not None else {}


def diff_saves(before: Save, after: Save) -> dict:
    """What changed from `before` to `after`, as a rewind: each changed value with its value in `before`."""
    bb, ab = plot_bits(before), plot_bits(after)
    spec = {
        'format': REWIND_FORMAT,
        'character': str(before.data['character_id']),
        'booleans': {str(i): (i in bb) for i in sorted(bb ^ ab)},
        'integers': {str(i): v for i, v in _changed(before.data['plot']['integers'], after.data['plot']['integers'], 0).items()},
        'floats': {str(i): v for i, v in _changed(before.data['plot']['floats'], after.data['plot']['floats'], 0.0).items()},
        'maps': {},
        'mapLevels': {},
    }
    # The values after the beat, for reading only (a rewind puts back the ones above).
    for key in ('integers', 'floats'):
        values = after.data['plot'][key]
        spec['after' + key.capitalize()] = {i: (values[int(i)] if int(i) < len(values) else 0) for i in spec[key]}
    bm, am = _maps(before), _maps(after)
    for name in list(bm) + [m for m in am if m not in bm]:
        b_bytes = serialize('Map', bm[name]) if name in bm else None
        a_bytes = serialize('Map', am[name]) if name in am else None
        if b_bytes == a_bytes:
            continue
        spec['maps'][str(name)] = base64.b64encode(b_bytes).decode('ascii') if b_bytes is not None else None
        bl = bm[name]['levels'] if name in bm else {}
        al = am[name]['levels'] if name in am else {}
        spec['mapLevels'][str(name)] = sorted(str(lv) for lv in set(bl) | set(al)
                                              if lv not in bl or lv not in al or serialize('Level', bl[lv]) != serialize('Level', al[lv]))
    return spec


def apply_rewind(save: Save, spec: dict, with_maps: bool = False) -> list[str]:
    """Puts a rewind's values back on `save`; the lines say what changed."""
    if spec.get('format') != REWIND_FORMAT:
        raise SaveError(f"not a rewind file ({spec.get('format')!r})")
    done = []
    if spec.get('character') and spec['character'] != save.data['character_id']:
        done.append(f"warning: the rewind is from career {spec['character']}, this save is {save.data['character_id']}")
    for i, v in spec['booleans'].items():
        if save.plot_bool(int(i)) != v:
            save.set_plot_bool(int(i), v)
            done.append(f'bool {i} = {int(v)}')
    for key, setter, values in (('integers', save.set_plot_int, save.data['plot']['integers']),
                                ('floats', save.set_plot_float, save.data['plot']['floats'])):
        for i, v in spec[key].items():
            now = values[int(i)] if int(i) < len(values) else 0
            if now != v:
                setter(int(i), v)
                done.append(f'{key[:-1]} {i} = {v} (was {now})')
    if with_maps:
        ne = save.data.get('no_export')
        if ne is None:
            raise SaveError('this save keeps no world state (a character export)')
        for name, b64 in spec['maps'].items():
            if b64 is None:
                if name in ne['legacy_maps']:
                    del ne['legacy_maps'][name]
                    done.append(f'map {name}: forgotten (not visited before the beat)')
            else:
                r = Reader(base64.b64decode(b64))
                ne['legacy_maps'][ustr(name)] = r.read('Map', f'rewind.maps[{name}]')
                done.append(f'map {name}: world state as before the beat')
    return done


def load_plot_names(path: str | None = None) -> dict[str, dict[int, str]]:
    """The plot's names from Trilogy Save Editor's databases/me1_raw_plot_db.ron (reference/; tools/fetch-references.ps1),
    {'booleans': {id: name}, 'integers': ..., 'floats': ...}; empty when it isn't there."""
    if path is None:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'reference', 'trilogy-save-editor',
                            'databases', 'me1_raw_plot_db.ron')
    names: dict[str, dict[int, str]] = {'booleans': {}, 'integers': {}, 'floats': {}}
    try:
        text = open(path, encoding='utf-8').read()
    except OSError:
        return names
    section = None
    for line in text.splitlines():
        head = re.match(r'^\s*(booleans|integers|floats): \{', line)
        if head:
            section = head.group(1)
            continue
        entry = re.match(r'^\s*(\d+): "(.*)",?\s*$', line)
        if entry and section:
            names[section][int(entry.group(1))] = entry.group(2)
    return names


def describe_rewind(spec: dict, names: dict) -> str:
    lines = []
    for key, label in (('booleans', 'bool'), ('integers', 'int'), ('floats', 'float')):
        for i, v in spec[key].items():
            name = names[key].get(int(i), '')
            after = spec.get('after' + key.capitalize(), {}).get(i, '?')
            shown = f'{int(v)} -> {1 - int(v)}' if key == 'booleans' else f'{v} -> {after}'
            lines.append(f'  {label} {i:>5}  {shown:<16} {name}')
    for name, levels in spec['mapLevels'].items():
        state = ('not visited before' if spec['maps'][name] is None else
                 f'{len(levels)} level(s) changed' if levels else 'only its world info changed (streaming states, destination)')
        lines.append(f"  map {name}: {state}{': ' + ', '.join(levels[:8]) if levels else ''}{' ...' if len(levels) > 8 else ''}")
    return '\n'.join(lines) or '  nothing changed'


# ---- Command line ----

def describe(save: Save) -> str:
    d = save.data
    p = d['player']
    loc, rot = d['location'], d['rotation']
    ts = d['timestamp']
    lines = [
        f"character  {d['character_id']}  {p['first_name']} (level {p['level']}, class {p['player_class']})",
        f"saved      {ts['year']:04}-{ts['month']:02}-{ts['day']:02} "
        f"{ts['seconds_since_midnight'] // 3600:02}:{ts['seconds_since_midnight'] // 60 % 60:02}, "
        f"played {d['seconds_played'] // 3600} h {d['seconds_played'] // 60 % 60} min",
        f"map        {d['map_name']}  (base {d['base_level_name']!s}, parent {d['parent_map_name']!s})",
        f"place      {loc['x']:.1f} {loc['y']:.1f} {loc['z']:.1f}  yaw {angle_deg(rot['yaw']):.1f} deg "
        f"(pitch {rot['pitch']}, roll {rot['roll']})",
        f"names      display {d['display_name']!s}, file {d['file_name']!s}",
        f"plot       {len(d['plot']['booleans']) * 32} booleans, {len(d['plot']['integers'])} integers, "
        f"{len(d['plot']['floats'])} floats",
        f"squad      {', '.join(h['tag'] for h in d['squad']) or '(none)'}",
    ]
    ne = d.get('no_export')
    if ne is None:
        lines.append('world      none (a character export)')
    else:
        lines.append(f"world      {len(ne['legacy_maps'])} maps kept: {', '.join(ne['legacy_maps'])}")
        for name, m in ne['legacy_maps'].items():
            w = m['world']
            if w is not None and w['class_name'] == 'BioWorldInfoSaveObject':
                b = w['body']
                on = [s['name'] for s in b['streaming_states'] if s['enabled']]
                lines.append(f"  {name}: {len(m['levels'])} levels, streaming states on: {', '.join(on) or '-'}; "
                             f"destination {b['destination_area_map'] or '-'}")
    return '\n'.join(lines)


def cmd_info(args) -> int:
    for path in args.saves:
        print(f'== {path}')
        print(describe(Save.load(path)))
    return 0


def cmd_roundtrip(args) -> int:
    bad = 0
    for path in args.saves:
        with open(path, 'rb') as f:
            b = f.read()
        try:
            s = Save.from_bytes(b)
            payload_same = build_payload(s.data) == s._original[0]
            file_same = s.to_bytes() == b
            # And through our own compression: the written file reads back to the same payload.
            fresh = Save(s.data, s.block, s.compression_flag).to_bytes()
            reread_same = build_payload(Save.from_bytes(fresh).data) == s._original[0]
        except SaveError as e:
            print(f'FAIL {path}: {e}')
            bad += 1
            continue
        ok = payload_same and file_same and reread_same
        bad += 0 if ok else 1
        print(f"{'ok  ' if ok else 'FAIL'} {path}: payload {'same' if payload_same else 'DIFFERS'}, "
              f"file {'same' if file_same else 'DIFFERS'}, recompressed {'reads back' if reread_same else 'DIFFERS'}")
    return 1 if bad else 0


def cmd_plot(args) -> int:
    s = Save.load(args.save)
    for i in args.bool or []:
        print(f'bool {i} = {int(s.plot_bool(i))}')
    for i in args.int or []:
        ints = s.data['plot']['integers']
        print(f"int {i} = {ints[i] if i < len(ints) else '(past the end: 0)'}")
    for i in args.float or []:
        fl = s.data['plot']['floats']
        print(f"float {i} = {fl[i] if i < len(fl) else '(past the end: 0)'}")
    return 0


def _pairs(items, conv):
    out = []
    for it in items or []:
        k, _, v = it.partition('=')
        if not _:
            raise SystemExit(f'expected N=VALUE, got {it!r}')
        out.append((int(k), conv(v)))
    return out


def cmd_write(args) -> int:
    s = Save.load(args.input)
    s.set_level(args.level, args.area, args.parent_map)
    if args.location or args.yaw is not None:
        s.set_place(args.location, args.yaw)
    for i, v in _pairs(args.bool, lambda v: v.strip().lower() in ('1', 'true', 'yes', 'on')):
        s.set_plot_bool(i, v)
    for i, v in _pairs(args.int, int):
        s.set_plot_int(i, v)
    for i, v in _pairs(args.float, float):
        s.set_plot_float(i, v)
    if args.rewind:
        with open(args.rewind, encoding='utf-8') as f:
            spec = json.load(f)
        for line in apply_rewind(s, spec, args.with_maps):
            print(f'rewind: {line}')
    for item in args.stream_state or []:
        where, _, value = item.rpartition('=')
        map_name, colon, state = where.partition(':')
        if not _ or not colon or not map_name or not state:
            raise SystemExit(f'expected MAP:STATE=0|1, got {item!r}')
        print(s.set_stream_state(map_name, state, value.strip().lower() in ('1', 'true', 'yes', 'on')))
    if args.display_name is not None:
        s.data['display_name'] = ustr(args.display_name)
    if args.drop_map:
        ne = s.data.get('no_export')
        for name in args.drop_map:
            if ne is None or name not in ne['legacy_maps']:
                raise SystemExit(f'{name} is not among the maps the save keeps')
            del ne['legacy_maps'][name]
    if not args.keep_time:
        s.stamp_now()
    s.set_file_name(args.file_name or os.path.splitext(os.path.basename(args.output))[0])
    if os.path.exists(args.output) and not args.force:
        raise SystemExit(f'{args.output} exists (--force to replace it)')
    s.save(args.output)
    print(describe(Save.load(args.output)))
    return 0


def cmd_diff(args) -> int:
    before, after = Save.load(args.before), Save.load(args.after)
    spec = diff_saves(before, after)
    spec['before'] = os.path.basename(args.before)
    spec['after'] = os.path.basename(args.after)
    print(f"{args.before} -> {args.after}: {len(spec['booleans'])} booleans, {len(spec['integers'])} integers, "
          f"{len(spec['floats'])} floats, {len(spec['maps'])} maps changed")
    print(describe_rewind(spec, load_plot_names(args.names)))
    if args.rewind_out:
        with open(args.rewind_out, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(spec, f, indent=1)
        print(f'rewind written to {args.rewind_out} (write <later save> <out> --rewind {os.path.basename(args.rewind_out)})')
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description='Read and write Mass Effect Legendary Edition ME1 saves (.pcsav).')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('diff', help='what changed between two saves of a career; --rewind-out keeps it as a rewind')
    p.add_argument('before')
    p.add_argument('after')
    p.add_argument('--rewind-out', metavar='FILE', help='write the changes as a rewind file (the values before)')
    p.add_argument('--names', metavar='RON', help="Trilogy Save Editor's me1_raw_plot_db.ron (default: reference/)")
    p.set_defaults(fn=cmd_diff)
    p = sub.add_parser('info', help='what a save holds')
    p.add_argument('saves', nargs='+')
    p.set_defaults(fn=cmd_info)
    p = sub.add_parser('roundtrip', help='parse and write back: the same bytes?')
    p.add_argument('saves', nargs='+')
    p.set_defaults(fn=cmd_roundtrip)
    p = sub.add_parser('plot', help='read plot values')
    p.add_argument('save')
    p.add_argument('--bool', type=int, action='append')
    p.add_argument('--int', type=int, action='append')
    p.add_argument('--float', type=int, action='append')
    p.set_defaults(fn=cmd_plot)
    p = sub.add_parser('write', help='a new save from one, with edits')
    p.add_argument('input')
    p.add_argument('output')
    p.add_argument('--level', help='the persistent level to load (base_level_name), e.g. BIOA_STA00')
    p.add_argument('--area', help='the area the menus name (map_name), e.g. MapSTA30_02; default: kept')
    p.add_argument('--parent-map', help='parent_map_name (default: kept)')
    p.add_argument('--file-name', help='the name the save knows itself by (default: the output file name without .pcsav)')
    p.add_argument('--location', type=float, nargs=3, metavar=('X', 'Y', 'Z'), help='the player, in Unreal units')
    p.add_argument('--yaw', type=float, help='the player\'s facing in degrees (0 = +X, 90 = +Y)')
    p.add_argument('--bool', action='append', metavar='N=0|1', help='a plot boolean (repeatable)')
    p.add_argument('--int', action='append', metavar='N=V', help='a plot integer (repeatable)')
    p.add_argument('--float', action='append', metavar='N=V', help='a plot float (repeatable)')
    p.add_argument('--display-name', help='the name the Load menu shows')
    p.add_argument('--rewind', metavar='FILE', help='put back the plot values a diff --rewind-out recorded (replays a beat)')
    p.add_argument('--with-maps', action='store_true', help='with --rewind: also the world state of the maps the beat changed, as before it')
    p.add_argument('--stream-state', action='append', metavar='MAP:STATE=0|1',
                   help="a visited map's kept streaming state, e.g. BIOA_NOR00:stream_wakeup=1 (repeatable; info lists them)")
    p.add_argument('--drop-map', action='append', metavar='MAP', help='forget a visited map\'s kept state (repeatable)')
    p.add_argument('--keep-time', action='store_true', help='keep the save time (default: now, so it is the newest)')
    p.add_argument('--force', action='store_true', help='replace an existing output')
    p.set_defaults(fn=cmd_write)
    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except SaveError as e:
        print(f'error: {e}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
