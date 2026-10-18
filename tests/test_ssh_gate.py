#!/usr/bin/env python3
"""Execute SSH installer with isolated filesystem and fake QNX executables."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SERIAL='7WJ-00000.00.0000000000'

class SSHGate(unittest.TestCase):
    def exercise(self,fail=''):
        with tempfile.TemporaryDirectory(prefix='cns2-ssh-offline-') as temp:
            p=Path(temp);unit=p/'unit';usb=p/'usb';binpath=p/'bin'
            for name in ('persistence','tmp','ffs/sbin','home/root'):(unit/name).mkdir(parents=True,exist_ok=True)
            binpath.mkdir();usb.mkdir()
            shutil.copytree(ROOT/'network',usb/'network')
            def translate(text):
                text=text.replace('/proc/boot/ksh','/bin/bash')
                text=re.sub(r'(?<![A-Za-z0-9_.])/(persistence|tmp|ffs|home)(?=/|"|\s)',lambda m:str(unit/m[1]),text)
                return '\n'.join(f'PATH={binpath}:/usr/bin:/bin' if line.startswith('PATH=') or line.startswith('export PATH=') else line for line in text.splitlines())+'\n'
            for f in (usb/'network').rglob('*'):
                if f.is_file() and (f.suffix=='.sh' or f.name=='sshd_config'):
                    f.write_text(translate(f.read_text()))
            cfg=usb/'network/cns275_sshd/etc/sshd_config'
            if fail=='password':cfg.write_text(cfg.read_text().replace('PasswordAuthentication no','PasswordAuthentication yes'))
            if fail=='duplicate':cfg.write_text(cfg.read_text()+'PasswordAuthentication yes\n')
            keygen=usb/'network/cns275_sshd/usr/bin/ssh-keygen'
            keygen.write_text('#!/bin/bash\n[ "$FAIL" != keygen ] || exit 82\nif [ "$1" = -l ];then exit 0;fi\nwhile [ "$#" -gt 1 ];do shift;done\necho FIXTURE_HOST_PRIVATE > "$1"\necho FIXTURE_HOST_PUBLIC > "$1.pub"\n')
            daemon=usb/'network/cns275_sshd/usr/sbin/sshd'
            daemon.write_text('#!/bin/bash\nif [ "$1" = -t ];then [ "$FAIL" != config ];exit $?;fi\n[ "$FAIL" != daemon ] || exit 84\ntouch "'+str(p/'listener')+'"\n')
            for f in (keygen,daemon):f.chmod(0o700)
            (unit/'ffs/sbin/qconnstart').write_text('# stock boot fixture\n')
            # Bound the asynchronous boot-hook test to a one-shot stub.
            for name in ('qconnstart_cns275_v24.sh','remote_access_supervisor_v24.sh'):
                (usb/'network'/name).write_text('#!/bin/bash\n# CNS275_REMOTE_ACCESS_V24 CNS2_FREE_SSH_ONLY\necho BOOT_HOOK_FIXTURE\n')
            tool=p/'tool'
            tool.write_text('#!/bin/bash\ncase "$1" in\n--identity) echo SERIAL='+SERIAL+';;\n--ssh-probe) [ "$FAIL" != banner ] && [ -f "'+str(p/'listener')+'" ];;\n--digest) echo fixture-digest;;\n*) exit 2;;\nesac\n')
            tool.chmod(0o700)
            daemonpath=unit/'persistence/cns275/sshd/usr/sbin/sshd'
            configpath=unit/'persistence/cns275/sshd/etc/sshd_config'
            for name,text in {'pidin':f'if [ -f "{p}/listener" ];then echo "321 {daemonpath} -f {configpath}";fi','mount':'exit 0','sync':'exit 0','sleep':'exit 0'}.items():
                f=binpath/name;f.write_text('#!/bin/bash\n'+text+'\n');f.chmod(0o700)
            script=p/'install-ssh-under-test.sh';script.write_text(translate((ROOT/'runtime/install-ssh.sh').read_text()))
            g='0123456789abcdef'
            result=subprocess.run(['bash',str(script),str(usb),str(tool),SERIAL,g],capture_output=True,text=True,env=dict(os.environ,FAIL=fail))
            ready=unit/'persistence/cns275'/('SSH_READY-'+g)
            if fail:
                self.assertNotEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertFalse(ready.exists())
            else:
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertEqual(ready.read_text().strip(),g+' '+SERIAL)
                self.assertTrue((usb/'cns275_ssh_host_rsa_key.pub').is_file())
                self.assertFalse((usb/'ssh_host_rsa_key').exists())
                self.assertTrue((unit/'ffs/sbin/qconnstart.stock').is_file())
            self.assertFalse(any((unit/'persistence').glob('.v43*')))

    def test_success(self):self.exercise()
    def test_hostkey_generation_fails(self):self.exercise('keygen')
    def test_invalid_configuration(self):self.exercise('config')
    def test_password_enabled(self):self.exercise('password')
    def test_duplicate_directive(self):self.exercise('duplicate')
    def test_daemon_start_fails(self):self.exercise('daemon')
    def test_no_ssh_banner(self):self.exercise('banner')

if __name__=='__main__':unittest.main(verbosity=2)
