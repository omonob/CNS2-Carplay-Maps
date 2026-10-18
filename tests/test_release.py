#!/usr/bin/env python3
"""Verify both exported USB roots and ZIPs, without executing ARM code."""
import hashlib
from pathlib import Path
import subprocess
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[1]

class ReleaseTests(unittest.TestCase):
    def test_two_separate_exports(self):
        producer_hashes=[]
        for model,fps in ((790,30),(791,40)):
            name=f'CNS2-{model}-VideoOnly-{fps}FPS'
            directory=ROOT/'dist'/name
            archive=directory.with_suffix('.zip')
            with self.subTest(package=name):
                self.assertEqual((directory/'model.conf').read_text(),f'{model} {fps}\n')
                expected={p.relative_to(directory).as_posix():p for p in directory.rglob('*') if p.is_file()}
                with zipfile.ZipFile(archive) as z:
                    self.assertEqual(set(z.namelist()),set(expected))
                    self.assertIn('delphi.sh',z.namelist())
                    for entry in z.namelist():
                        self.assertEqual(z.read(entry),expected[entry].read_bytes())
                        self.assertFalse(entry.startswith('/') or '..' in entry.split('/'))
                        self.assertNotIn(Path(entry).suffix.lower(),('.jar','.enc','.ppk','.pem','.key','.dpapi','.rar','.log'))
                rows=(directory/'package-proof.txt').read_text().splitlines()
                self.assertEqual(len(rows),len(expected)-1)
                for row in rows:
                    path,mode,size,hashvalue=row.split()
                    data=(directory/path).read_bytes()
                    self.assertEqual(int(size),len(data))
                    self.assertEqual(hashvalue,hashlib.blake2b(data,digest_size=32).hexdigest())
                for path in directory.glob('payload/*.so'):
                    result=subprocess.check_output(['readelf','-h','-Ws',str(path)],text=True)
                    self.assertIn('ARM',result)
                    self.assertFalse(any('UND' in line and '__aeabi_uldivmod' in line for line in result.splitlines()))
                self.assertFalse((directory/'source').exists())
                self.assertFalse(list(directory.rglob('ssh_host_rsa_key')))
                self.assertTrue((directory/'licenses/OPENSSH-LICENCE.txt').is_file())
                self.assertTrue((directory/'licenses/OPENSSL-LICENSE.txt').is_file())
                data=(directory/'payload/libcns2-carplay-altscreen-live-v9-showui-persistlog.so').read_bytes()
                producer_hashes.append(hashlib.sha256(data).hexdigest())
                self.assertNotIn(b'roller_activate',data)
        self.assertNotEqual(*producer_hashes)

    def test_sha256_assets(self):
        for line in (ROOT/'dist/SHA256SUMS.txt').read_text().splitlines():
            hashvalue,name=line.split()
            self.assertEqual(hashvalue,hashlib.sha256((ROOT/'dist'/name).read_bytes()).hexdigest())

if __name__=='__main__':unittest.main(verbosity=2)
