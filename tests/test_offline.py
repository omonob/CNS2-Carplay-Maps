#!/usr/bin/env python3
"""Host-only checks. No SSH client, no live unit, no real mounts/reboots."""
import hashlib
import importlib.util
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / 'validation/cns2-tool-host'
SERIAL = '7WJ-00000.00.0000000000'  # Synthetic fixture, not a customer ID.

def run(*args, **kwargs):
    return subprocess.run(args, capture_output=True, text=True, **kwargs)

def digest(path):
    b = Path(path).read_bytes()
    return hashlib.blake2b(b, digest_size=32).hexdigest()

def proof(root, filename, names):
    rows = []
    for name in names:
        p = root / name
        rows.append(f'{name} 700 {p.stat().st_size} {digest(p)}\n')
    (root / filename).write_text(''.join(rows))

class HelperTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        HOST.parent.mkdir(exist_ok=True)
        r = run('gcc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                '-DHOST_TEST', '-Isource/vendor', 'source/install-tool.c',
                'source/vendor/monocypher.c', '-o', str(HOST), cwd=ROOT)
        if r.returncode:
            raise RuntimeError(r.stderr)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='cns2-offline-')
        self.path = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def tool(self, *args, **kwargs):
        return run(str(HOST), *map(str, args), **kwargs)

    def test_hash_identity_random(self):
        p = self.path / 'payload'
        p.write_bytes(bytes(range(256)) * 1024)
        self.assertEqual(self.tool('--digest', p).stdout.strip(), f'{digest(p)} {p.stat().st_size}')
        env = dict(os.environ, CNS2_FIXTURE_SERIAL=SERIAL)
        self.assertEqual(self.tool('--identity', env=env).stdout.strip(), 'SERIAL=' + SERIAL)
        env['CNS2_FIXTURE_SERIAL'] = 'bad'
        self.assertNotEqual(self.tool('--identity', env=env).returncode, 0)
        tokens = {self.tool('--generation').stdout.strip() for _ in range(25)}
        self.assertEqual(len(tokens), 25)
        self.assertTrue(all(len(t)==16 and set(t)<=set('0123456789abcdef') for t in tokens))

    def test_manifest_corruption_duplicate_traversal_symlink(self):
        (self.path / 'a').write_text('good')
        proof(self.path, 'proof', ['a'])
        args = ('--check-list', self.path/'proof', self.path)
        self.assertEqual(self.tool(*args).returncode, 0)
        original = (self.path/'proof').read_text()
        for text in (original*2, original.replace('a ', '../a ', 1), original.replace('a ', '/a ', 1), original.rstrip(), ''):
            with self.subTest(text=text):
                (self.path/'proof').write_text(text)
                self.assertNotEqual(self.tool(*args).returncode, 0)
        (self.path/'proof').write_text(original)
        (self.path/'a').write_text('bad')
        self.assertNotEqual(self.tool(*args).returncode, 0)
        (self.path/'a').unlink()
        (self.path/'b').write_text('good')
        (self.path/'a').symlink_to(self.path/'b')
        self.assertNotEqual(self.tool(*args).returncode, 0)

    def test_procman_only_four_execs_idempotent_and_no_overwrite(self):
        stock = ('# preserved\nother -n"network" -f netstart\n'
                 'proc -n"hmi" -f /extbin/apps/hmi/HMI/runHMI.sh -a abc\n'
                 'proc -n"most" -f Most -p 20\nproc -n"carplay" -f CarPlay\n'
                 'proc -n"navi_interface" -f NavInterface --keep\n')
        a, b, c = (self.path/n for n in ('a', 'b', 'c'))
        a.write_text(stock)
        self.assertEqual(self.tool('--patch-pm', a, b).returncode, 0)
        self.assertIn('other -n"network" -f netstart', b.read_text())
        self.assertIn('/persistence/.h -a abc', b.read_text())
        self.assertEqual(self.tool('--patch-pm', b, c).returncode, 0)
        self.assertEqual(b.read_bytes(), c.read_bytes())
        self.assertNotEqual(self.tool('--patch-pm', a, b).returncode, 0)
        c.unlink()
        a.write_text(stock.replace('-f Most', '-f UNKNOWN'))
        self.assertNotEqual(self.tool('--patch-pm', a, c).returncode, 0)
        self.assertFalse(c.exists())
        a.write_text(stock + 'proc -n"most" -f Most\n')
        self.assertNotEqual(self.tool('--patch-pm', a, c).returncode, 0)

    def test_unknown_rc_rejected_without_output(self):
        a, b = self.path/'rc', self.path/'out'
        a.write_bytes(b'not OEM firmware')
        self.assertNotEqual(self.tool('--prepare-rc', a, b).returncode, 0)
        self.assertFalse(b.exists())

    def test_private_local_rc_fixture(self):
        fixture = os.environ.get('CNS2_PRIVATE_RC_FIXTURE')
        if not fixture:
            self.skipTest('Optional original-rc fixture is deliberately not distributed')
        b = self.path/'prepared'
        self.assertEqual(self.tool('--prepare-rc', fixture, b).returncode, 0)
        self.assertEqual(digest(b), '8591e0a88dbbde53cc9425e1d4a62f8dab52bef1ae0d8e2c9cecd6e307c5d205')
        import zlib
        self.assertEqual(zlib.crc32(Path(fixture).read_bytes()), zlib.crc32(b.read_bytes()))

    def test_shell_syntax_and_scope(self):
        scripts = list(ROOT.glob('runtime/*.sh'))+list(ROOT.glob('network/*.sh'))+[ROOT/'delphi.sh',ROOT/'network/cns275_sshd/start_sshd.sh']
        for path in scripts:
            with self.subTest(script=str(path.relative_to(ROOT))):
                data=path.read_bytes()
                self.assertNotIn(b'\r',data)
                self.assertEqual(run('bash','-n',str(path)).returncode,0)
                self.assertNotIn(b'slay ',data)
                self.assertNotIn(b'rm -rf',data)
        install=(ROOT/'runtime/install.sh').read_text()
        self.assertLess(install.index('"$USB/runtime/install-ssh.sh"'),install.index('for P in .v43c'))
        self.assertLess(install.index('SSH_READY-$G'),install.index('"$C/commit.sh"'))
        entry=(ROOT/'runtime/entry.sh').read_text()
        self.assertIn('hmi) exec /extbin/apps/hmi/HMI/runHMI.sh',entry)
        self.assertNotIn('V531A.jar',entry)
        self.assertNotIn('runHMI.candidate',entry)
        self.assertNotIn('cpvc-rgd-sync', (ROOT/'runtime/cpmap-bootstrap.sh').read_text())

    def test_package_manifest_rejects_private_key(self):
        sys.path.insert(0,str(ROOT/'tools'))
        from manifest import build
        (self.path/'safe').write_text('public contents')
        build(self.path)
        (self.path/'client.ppk').write_text('dummy')
        with self.assertRaises(ValueError):build(self.path)

    def test_usb_ssh_failure_prevents_video_and_reboot(self):
        # Execute the REAL installer body with its filesystem names redirected
        # into a temporary sandbox. All device commands are local fixture stubs.
        unit=self.path/'unit';usb=self.path/'usb';binpath=self.path/'bin'
        unit.mkdir();usb.mkdir();binpath.mkdir()
        for p in ('ramdisk','persistence','tmp','ffs'):(unit/p).mkdir()
        (usb/'runtime').mkdir();(usb/'payload').mkdir();(usb/'profiles').mkdir()
        (usb/'model.conf').write_text('790 30\n')
        tool=usb/'payload/cns2-tool'
        tool.write_text('#!/bin/bash\ncase "$1" in\n--identity) echo SERIAL='+SERIAL+';;\n--generation) echo 0123456789abcdef;;\n*) exit 0;;\nesac\n')
        tool.chmod(0o700)
        (usb/'runtime/install-ssh.sh').write_text('#!/bin/bash\necho SSH_INJECTED_FAILURE\nexit 125\n')
        (usb/'profiles/native-factory-proof.txt').write_text('fixture')
        (usb/'package-proof.txt').write_text('fixture')
        scripts={
            'mount':f'echo "/dev/fs1p0 on {unit}/ramdisk type flash"',
            'id':'echo 0', 'pidin':'echo "123 /extbin/apps/bin/Most"',
            'sync':'exit 0','shutdown':f'touch "{self.path}/REBOOT_FORBIDDEN"',
        }
        for name,text in scripts.items():
            p=binpath/name;p.write_text('#!/bin/bash\n'+text+'\n');p.chmod(0o700)
        text=(ROOT/'runtime/install.sh').read_text()
        text=text.replace('/proc/boot/ksh','/bin/bash')
        text=re.sub(r'(?<![A-Za-z0-9_.])/(ramdisk|persistence|tmp|ffs)(?=/|"|\s)',lambda m:str(unit/m[1]),text)
        lines=text.splitlines()
        lines=[f'PATH={binpath}:/usr/bin:/bin' if line.startswith('PATH=') else line for line in lines]
        script=self.path/'install-under-test.sh';script.write_text('\n'.join(lines)+'\n')
        r=run('bash',str(script),str(usb))
        self.assertEqual(r.returncode,125,r.stdout+r.stderr)
        self.assertIn('SSH_INJECTED_FAILURE',(usb/'cns2-free-install-latest.log').read_text())
        self.assertFalse(any((unit/'persistence').glob('.v43*')))
        self.assertFalse((self.path/'REBOOT_FORBIDDEN').exists())

    def test_usb_complete_commit_with_private_local_rc(self):
        fixture=os.environ.get('CNS2_PRIVATE_RC_FIXTURE')
        if not fixture:self.skipTest('Optional original-rc fixture not distributed')
        unit=self.path/'unit';usb=self.path/'usb';binpath=self.path/'bin'
        for p in ('ramdisk','persistence','tmp','ffs/etc','extbin/apps/bin','etc'):(unit/p).mkdir(parents=True,exist_ok=True)
        usb.mkdir();binpath.mkdir()
        for p in ('runtime','payload','profiles'):(usb/p).mkdir()
        def translate(text):
            text=text.replace('/proc/boot/ksh','/bin/bash')
            text=re.sub(r'(?<![A-Za-z0-9_.])/(ramdisk|persistence|tmp|ffs|extbin|etc)(?=/|"|\s)',lambda m:str(unit/m[1]),text)
            return '\n'.join(f'PATH={binpath}:/usr/bin:/bin' if line.startswith('PATH=') else line for line in text.splitlines())+'\n'
        for src in ROOT.glob('runtime/*.sh'):
            p=usb/'runtime'/src.name;p.write_text(translate(src.read_text()));p.chmod(0o700)
        # Simulated SSH gate only: a host test cannot execute the QNX daemon.
        (usb/'runtime/install-ssh.sh').write_text('#!/bin/bash\nset -e\nmkdir -p "'+str(unit/'persistence/cns275')+'"\necho "$4 $3" > "'+str(unit/'persistence/cns275/SSH_READY-')+'$4"\necho SSH_GATE_FIXTURE_OK\n')
        (usb/'model.conf').write_text('791 40\n')
        shutil.copy2(fixture,unit/'ffs/etc/rc')
        stock=('proc -n"hmi" -f /extbin/apps/hmi/HMI/runHMI.sh\nproc -n"most" -f Most\nproc -n"carplay" -f CarPlay\nproc -n"navi_interface" -f NavInterface\n')
        (unit/'etc/procman_core.cfg').write_text(stock)
        names=[]
        for name in ('libcns2-most-pull-r43.so','libcns2-autoload-carplay-v9.so','libcns2-carplay-altscreen-live-v9-showui-persistlog.so','CarPlay-cpmap-v2.sh','cpmap-bootstrap.sh'):
            (usb/'payload'/name).write_text('FIXTURE '+name+'\n');names.append(name)
        proof(usb/'payload','manifest.txt',names)
        adapter=usb/'payload/cns2-tool'
        adapter.write_text('#!/bin/bash\nif [ "$1" = --ssh-probe ];then echo SSH_BANNER_FIXTURE;exit 0;fi\nexec "'+str(HOST)+'" "$@"\n')
        adapter.chmod(0o700)
        rc=unit/'ffs/etc/rc'
        (usb/'profiles/native-factory-proof.txt').write_text(f'{rc} 000 {rc.stat().st_size} {digest(rc)}\n')
        (usb/'profiles/native-ready-proof.txt').write_text(f'{rc} 000 3732 8591e0a88dbbde53cc9425e1d4a62f8dab52bef1ae0d8e2c9cecd6e307c5d205\n')
        sys.path.insert(0,str(ROOT/'tools'))
        from manifest import build
        build(usb)
        for name,text in {'mount':f'echo "/dev/fs1p0 on {unit}/ramdisk type flash"','id':'echo 0','pidin':'echo "123 /extbin/apps/bin/Most"','sync':'exit 0','shutdown':f'echo once >> "{self.path}/reboots"'}.items():
            p=binpath/name;p.write_text('#!/bin/bash\n'+text+'\n');p.chmod(0o700)
        env=dict(os.environ,CNS2_FIXTURE_SERIAL=SERIAL)
        r=run('bash',str(usb/'runtime/install.sh'),str(usb),env=env)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr+'\n'+(usb/'cns2-free-install-latest.log').read_text())
        g=(unit/'persistence/.v43c/ACTIVE').read_text().strip()
        c=unit/'persistence/.v43c'/g
        self.assertEqual((c/'COMMITTED').read_text().strip(),g+' '+SERIAL)
        self.assertEqual((c/'edition').read_text().strip(),'video-only')
        self.assertEqual(digest(rc),'8591e0a88dbbde53cc9425e1d4a62f8dab52bef1ae0d8e2c9cecd6e307c5d205')
        self.assertEqual((self.path/'reboots').read_text(),'once\n')
        self.assertTrue((unit/'persistence/.v43b'/g/'rc.original').is_file())
        self.assertTrue((unit/'persistence/cns275/install-records'/SERIAL/(g+'.log')).is_file())
        for name in ('.h','.m','.c','.n'):
            p=unit/'persistence'/name
            self.assertIn('V43_SHORT_ENTRY',p.read_text())
            self.assertEqual(run('bash','-n',str(p)).returncode,0)

if __name__=='__main__':
    os.chdir(ROOT)
    unittest.main(verbosity=2)
