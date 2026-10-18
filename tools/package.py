#!/usr/bin/env python3
"""Export two independent USB packages; no customer files or paid features."""
import hashlib
from pathlib import Path
import shutil
import zipfile
from manifest import build

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / 'dist'
LIBS = ('libcns2-carplay-altscreen-live-v9-showui-persistlog.so',
        'libcns2-autoload-carplay-v9.so', 'libcns2-most-pull-r43.so')

def main():
    DIST.mkdir(exist_ok=True)
    sums = []
    for model, fps in ((790, 30), (791, 40)):
        name = f'CNS2-{model}-VideoOnly-{fps}FPS'
        out = DIST / name
        # Never merge a stale export into a new release candidate.
        if out.exists():
            raise SystemExit(f'Export already exists: {out}; archive before rebuilding')
        out.mkdir()
        for directory in ('runtime', 'network', 'profiles', 'licenses', 'docs'):
            shutil.copytree(ROOT / directory, out / directory)
        for file in ('delphi.sh', 'LICENSE', 'README.md', 'THIRD_PARTY_NOTICES.md'):
            shutil.copy2(ROOT / file, out / file)
        (out / 'model.conf').write_text(f'{model} {fps}\n', encoding='ascii')
        payload = out / 'payload'
        payload.mkdir()
        rows = []
        for name2 in (*LIBS, 'CarPlay-cpmap-v2.sh', 'cpmap-bootstrap.sh'):
            if name2 in LIBS:
                where = str(model) if name2 == LIBS[0] else 'common'
                source = ROOT / '.build' / where / name2
            else:
                source = ROOT / 'runtime' / name2
            shutil.copy2(source, payload / name2)
            data = source.read_bytes()
            rows.append(f'{name2} 700 {len(data)} {hashlib.blake2b(data,digest_size=32).hexdigest()}\n')
        shutil.copy2(ROOT / '.build/common/cns2-tool', payload / 'cns2-tool')
        (payload / 'manifest.txt').write_text(''.join(rows), encoding='ascii')
        build(out)
        archive = DIST / f'CNS2-{model}-VideoOnly-{fps}FPS.zip'
        with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as z:
            for file in sorted(out.rglob('*')):
                if file.is_file():
                    z.write(file, file.relative_to(out).as_posix())
        sums.append(f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n')
        print(f'Built {archive.name}: {archive.stat().st_size} bytes')
    (DIST / 'SHA256SUMS.txt').write_text(''.join(sums), encoding='ascii')

if __name__ == '__main__':
    main()
