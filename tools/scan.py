"""Comprueba que nada de lo que se va a subir contiene bytes de una ROM ni derivados.
    python tools/scan.py [--history] [--rom ROM]...
  Sin --history mira los ficheros que git sigue (o tiene en el índice). Con --history mira todos los blobs del historial
  de ramas, etiquetas y remotas (lo que un push publicaría; las referencias de otras herramientas, como refs/codex, no se miran). Las ROM se buscan en --rom y, si no hay, en ../*/roms/*.gba y ./roms/*.gba.
  Falla (código 1) si encuentra: ficheros con extensión de ROM/partida/volcado, trozos de 16 bytes de una ROM
  (huellas contiguas cada 16 bytes; se avisa con 3 o más distintas, o sea tramos copiados de unos 64 bytes o más, para que una frase suelta no dé falsas alarmas), o tablas C/hex de 64 bytes o más.
  Sin ninguna ROM a mano solo hace las comprobaciones de nombre y de tablas."""
import glob, os, re, subprocess, sys

BAD_EXT = {'.gba', '.srm', '.sav', '.sst', '.ppm', '.wav', '.rom', '.agb', '.bin', '.dump'}
KEY, STRIDE, MIN_HITS = 16, 16, 3
HEXRUN = re.compile(rb'(?:0x[0-9A-Fa-f]{2}\s*,\s*){64,}|(?:\\x[0-9A-Fa-f]{2}){64,}|(?:[0-9A-Fa-f]{2}[ ,]){127,}[0-9A-Fa-f]{2}')

sys.stdout.reconfigure(encoding='utf-8')

def git(*a):
    return subprocess.run(['git', *a], capture_output=True, check=True).stdout

def load_keys(roms):
    keys = {}
    for r in roms:
        d = open(r, 'rb').read()
        for i in range(0, len(d) - KEY, STRIDE):
            k = d[i:i + KEY]
            if len(set(k)) >= 6:  # skip padding and other low-entropy runs
                keys[k] = os.path.basename(r)
    return keys

def check(name, data, keys, problems):
    ext = os.path.splitext(name)[1].lower()
    if ext in BAD_EXT:
        problems.append(f'{name}: extensión {ext} (ROM, partida o volcado)')
    if HEXRUN.search(data):
        problems.append(f'{name}: tabla de 64 bytes o más escrita en texto (¿volcado de la ROM?)')
    if keys and len(data) >= KEY:
        found = {}
        for i in range(len(data) - KEY + 1):
            k = data[i:i + KEY]
            if k in keys:
                found.setdefault(keys[k], set()).add(k)
        for r, ks in found.items():
            if len(ks) >= MIN_HITS:
                problems.append(f'{name}: {len(ks)} trozos distintos de 16 bytes de {r}')

def main():
    args = sys.argv[1:]
    history = '--history' in args
    roms = [args[i + 1] for i, a in enumerate(args) if a == '--rom' and i + 1 < len(args)]
    root = git('rev-parse', '--show-toplevel').decode().strip()
    os.chdir(root)
    if not roms:
        roms = glob.glob('../*/roms/*.gba') + glob.glob('roms/*.gba')
    keys = load_keys(roms)
    print(f'ROM para comparar: {len(roms)} ({len(keys)} huellas)' if roms else 'AVISO: no hay ninguna ROM a mano; solo se comprueban nombres y tablas')
    problems = []
    n = 0
    if history:
        out = git('rev-list', '--objects', '--branches', '--tags', '--remotes').decode().splitlines()
        names = {}
        for l in out:
            h, _, nm = l.partition(' ')
            names.setdefault(h, nm)
        p = subprocess.Popen(['git', 'cat-file', '--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        for h in names:
            p.stdin.write((h + '\n').encode()); p.stdin.flush()
            hdr = p.stdout.readline().split()
            size = int(hdr[2]); data = p.stdout.read(size); p.stdout.read(1)
            if hdr[1] == b'blob':
                check(names[h] or h, data, keys, problems); n += 1
        p.stdin.close(); p.wait()
    else:
        files = set(git('ls-files', '-z').decode().split('\0')) | set(git('diff', '--cached', '--name-only', '-z').decode().split('\0'))
        for f in sorted(x for x in files if x and os.path.isfile(x)):
            check(f, open(f, 'rb').read(), keys, problems); n += 1
    print(f'{n} ficheros mirados')
    if problems:
        print('NO SUBIR:'); [print('  ' + x) for x in sorted(set(problems))]
        sys.exit(1)
    print('limpio')

main()
