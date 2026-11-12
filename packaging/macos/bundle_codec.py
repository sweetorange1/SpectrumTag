#!/usr/bin/env python3
"""Bundle a Homebrew FFmpeg dependency closure without changing Homebrew files."""
import argparse, hashlib, json, os, pathlib, re, shutil, subprocess

def run(*args):
    return subprocess.check_output(args, text=True).strip()

def try_run(*args):
    try:
        return run(*args)
    except (OSError, subprocess.CalledProcessError):
        return ''

def dependencies(path):
    return [line.strip().split(' (compatibility version')[0]
            for line in run('otool', '-L', str(path)).splitlines()[1:]]

def rpaths(path):
    return re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', run('otool', '-l', str(path)))

def main():
    p = argparse.ArgumentParser(); p.add_argument('encoder'); p.add_argument('app'); p.add_argument('arch'); a = p.parse_args()
    # Recent Homebrew bottles can contain linker metadata unsupported by the
    # runner's default Xcode 16 tools. Use the newest installed stable tools for
    # relocation only; this does not change the plug-in compiler or minimum OS.
    xc = []
    for item in pathlib.Path('/Applications').glob('Xcode_*.app'):
        match = re.fullmatch(r'Xcode_([0-9.]+)\.app', item.name)
        if match: xc.append((tuple(map(int, match[1].split('.'))), item))
    if xc: os.environ['DEVELOPER_DIR'] = str(max(xc)[1] / 'Contents/Developer')
    print('Relocation tool:', run('xcrun', '--find', 'install_name_tool'), flush=True)
    source = pathlib.Path(a.encoder).resolve()
    dest = pathlib.Path(a.app) / 'Contents' / 'Helpers' / a.arch
    libs = dest / 'lib'; libs.mkdir(parents=True, exist_ok=True)
    # A static build (packaging/macos/fetch-codec.sh) needs no package manager;
    # Homebrew formulae are only recorded when the encoder actually came from it.
    prefix_name = try_run('brew', '--prefix')
    prefix = pathlib.Path(prefix_name) if prefix_name else None
    mapped = {source: dest / 'ffmpeg'}; pending = [source]; edges = {}; names = {}; formulas = set()
    while pending:
        current = pending.pop(); edges[current] = []
        paths = rpaths(current)
        for link in dependencies(current):
            if link.startswith(('/System/Library/', '/usr/lib/')): continue
            candidate = link.replace('@loader_path', str(current.parent)).replace('@executable_path', str(source.parent))
            if candidate.startswith('@rpath/'):
                matches = [pathlib.Path(r.replace('@loader_path', str(current.parent))) / candidate[7:] for r in paths]
                candidate = next((str(m) for m in matches if m.exists()), '')
            dependency = pathlib.Path(candidate).resolve()
            if not candidate or not dependency.is_file(): raise RuntimeError(f'Unresolved dependency: {current}: {link}')
            if dependency == current: continue  # dylib install ID
            edges[current].append((link, dependency))
            if dependency not in mapped:
                previous = names.get(dependency.name)
                if previous and previous != dependency: raise RuntimeError(f'Colliding dylib names: {previous}, {dependency}')
                names[dependency.name] = dependency; mapped[dependency] = libs / dependency.name; pending.append(dependency)
    for original, copied in mapped.items():
        shutil.copy2(original, copied); copied.chmod(0o755)
        # Keep the original signature's link-edit layout while editing and
        # replace the signature afterwards. Removing it first can leave an
        # invalid __LINKEDIT extent in newer bottles with older OS tools.
        edits = ['install_name_tool']
        if copied != dest / 'ffmpeg': edits += ['-id', '@rpath/' + copied.name]
        for old, dependency in edges[original]:
            replacement = '@loader_path/' + ('lib/' if original == source else '') + mapped[dependency].name
            edits += ['-change', old, replacement]
        for rpath in rpaths(copied):
            if rpath.startswith(('/opt/homebrew', '/usr/local')):
                edits += ['-delete_rpath', rpath]
        if len(edits) > 1: subprocess.check_call(edits + [str(copied)])
        # These are copied helpers, never files in the build machine's Homebrew cellar.
        subprocess.check_call(['codesign', '--force', '--sign', '-', str(copied)])
        for link in dependencies(copied):
            if not link.startswith(('/usr/lib/', '/System/Library/', '@loader_path/', '@rpath/')):
                raise RuntimeError(f'External dependency survived relocation: {copied}: {link}')
        if 'Cellar' in original.parts:
            formulas.add(original.parts[original.parts.index('Cellar') + 1])
    notices = pathlib.Path(a.app) / 'Contents' / 'Resources' / 'ThirdParty' / a.arch
    notices.mkdir(parents=True, exist_ok=True)
    for name in ('COPYING.GPLv3', 'FFmpeg-NOTICE.txt'):
        sidecar = source.parent / name
        if sidecar.is_file(): shutil.copy2(sidecar, notices / name)
    if formulas and prefix:
        metadata = json.loads(run('brew', 'info', '--json=v2', *sorted(formulas)))
        (notices / 'homebrew-sources.json').write_text(json.dumps(metadata, indent=2) + '\n')
        for formula in metadata['formulae']:
            formula_dir = prefix / 'opt' / formula['name']
            for root in [formula_dir, formula_dir / 'share' / 'licenses', formula_dir / '.brew']:
                if not root.exists(): continue
                for item in root.iterdir():
                    if item.is_file() and (re.match(r'(?i)(copying|license|notice|copyright)', item.name) or item.suffix == '.rb'):
                        target = notices / formula['name']; target.mkdir(exist_ok=True); shutil.copy2(item, target / item.name)
    else:
        (notices / 'codec-sources.json').write_text(json.dumps({
            'encoder': str(source),
            'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
            'note': 'Single static executable; no external libraries were relocated.'}, indent=2) + '\n')
    # A cross-architecture helper cannot run on the packaging machine; record
    # whatever the host can read instead of failing the build.
    version = try_run(str(dest / 'ffmpeg'), '-version')
    buildconf = try_run(str(dest / 'ffmpeg'), '-buildconf')
    (notices / 'FFmpeg-build.txt').write_text(
        (version or 'Version unavailable: this architecture cannot run on the packaging machine.') + '\n\n'
        + (buildconf or '') + '\n')
    (notices / 'README.txt').write_text('FFmpeg and its libraries are separate open-source programs.\nExact source URLs, versions, checksums and build formulae are recorded alongside this notice.\nFFmpeg: https://ffmpeg.org/ ; Homebrew formulae: https://github.com/Homebrew/homebrew-core\nThe application invokes FFmpeg as a child process. No Homebrew installation is needed by the end user.\n')
    manifest = {str(v.relative_to(dest)): {'source': str(k), 'sha256': hashlib.sha256(v.read_bytes()).hexdigest()} for k, v in mapped.items()}
    (notices / 'runtime-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Bundled {len(mapped)} files for {a.arch}')

if __name__ == '__main__': main()
