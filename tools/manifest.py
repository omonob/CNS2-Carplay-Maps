#!/usr/bin/env python3
"""Regenerate integrity lists without ever accepting a private-key payload."""
import hashlib
from pathlib import Path
import sys

def build(root):
    root = Path(root)
    rows = []
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError(f'symlink rejected: {path.name}')
        if not path.is_file() or path.name == 'package-proof.txt':
            continue
        relative = path.relative_to(root).as_posix()
        if any(x.isspace() for x in relative) or '..' in relative:
            raise ValueError('unsafe package filename')
        data = path.read_bytes()
        if data.startswith((b'-----BEGIN ', b'PuTTY-User-Key-File-')) or path.suffix.lower() in ('.ppk', '.pem', '.key'):
            raise ValueError(f'private key rejected: {relative}')
        mode = '700' if path.suffix == '.sh' or path.name in ('cns2-tool', 'sshd', 'ssh-keygen', 'sftp-server') else '600'
        rows.append(f'{relative} {mode} {len(data)} {hashlib.blake2b(data,digest_size=32).hexdigest()}\n')
    if not rows or len(rows) > 128:
        raise ValueError('invalid manifest size')
    (root / 'package-proof.txt').write_bytes(''.join(rows).encode('ascii'))

if __name__ == '__main__':
    build(sys.argv[1])
