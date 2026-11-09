# SPDX-License-Identifier: MIT
"""Private fixture and Resume-footer tests; synthetic bytes only, no game access."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
import save_fixture as fixture
import recorded_route
import turn
import json


def text(value):
    data=value.encode('ascii')+b'\0'
    return struct.pack('<i',len(data))+data


def profile():
    body=struct.pack('<I',50)+bytes(range(120))+text('Other01')+text('Other01_AutoSave')+struct.pack('<I',3)
    return body+struct.pack('<I',fixture.crc32(body))


def save():
    body=(struct.pack('<I',50)+text('Tester00')+bytes(range(80))+text('BIOA_STA00')+
          text('MapSTA30_01')+struct.pack('<i',0)+struct.pack('<fffiii',1,2,3,0,0,0))
    compressed=zlib.compress(body)
    data=struct.pack('<IIIIII',0x9e2a83c1,262144,len(compressed),len(body),len(compressed),len(body))+compressed
    return data+struct.pack('<III',fixture.crc32(data),1,len(body))


def make_fixture(root):
    root=Path(root);root.mkdir(parents=True,exist_ok=True)
    (root/'Tester00_96.pcsav').write_bytes(save())
    (root/'profile.pcsav').write_bytes(profile())
    (root/'package').mkdir()
    turn.write(root/'package/walk.mevrrec')
    (root/'package/route.json').write_text(json.dumps(recorded_route.inspect(root/'package/walk.mevrrec')))
    fixture.freeze(root/'package',root/'Tester00_96.pcsav',root/'profile.pcsav')


class FixtureTests(unittest.TestCase):
    def test_checksum_and_footer_preservation(self):
        self.assertEqual(fixture.crc32(b'123456789'),0xfc891918)
        before=profile();at,_,_=fixture.profile_selection(before)
        after=fixture.resume_profile(before,'Tester00','Tester00_96')
        self.assertEqual(after[:at],before[:at])
        self.assertEqual(after[-8:-4],before[-8:-4])
        self.assertEqual(fixture.profile_selection(after)[1:],('Tester00','Tester00_96'))
        corrupt=bytearray(before);corrupt[12]^=1
        with self.assertRaises(ValueError):fixture.resume_profile(corrupt,'Tester00','Tester00_96')
        with self.assertRaises(ValueError):fixture.resume_profile(before,'../outside','../outside_96')

    def test_archive_survives_source_change_and_deletion(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);make_fixture(root)
            initial=fixture.verify(root/'package')
            (root/'Tester00_96.pcsav').write_bytes(b'overwritten save')
            (root/'profile.pcsav').unlink()
            self.assertEqual(fixture.verify(root/'package'),initial)
            (root/'Tester00_96.pcsav').unlink()
            self.assertEqual(fixture.verify(root/'package'),initial)
            (root/'Tester00_96.pcsav').write_bytes(save())
            with self.assertRaises(FileExistsError):
                fixture.freeze(root/'package',root/'Tester00_96.pcsav',root/'package/fixture/profile-template.pcsav')

    def test_fixture_integrity_and_map_admission(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);make_fixture(root)
            info=fixture.save_info(root/'Tester00_96.pcsav')
            self.assertEqual(info['locationCandidates'][0]['positionUU'],(1,2,3))
            target=root/'package/fixture/save.pcsav'
            bad=bytearray(target.read_bytes());bad[24]^=1;target.write_bytes(bad)
            with self.assertRaises(ValueError):fixture.verify(root/'package')
            with self.assertRaises(ValueError):fixture.save_info(target)


if __name__=='__main__':
    if len(sys.argv)==3 and sys.argv[1]=='--make':make_fixture(sys.argv[2])
    else:unittest.main()
