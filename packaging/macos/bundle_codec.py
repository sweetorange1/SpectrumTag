#!/usr/bin/env python3
"""Bundle a Homebrew FFmpeg dependency closure without changing Homebrew files."""
import argparse, hashlib, json, os, pathlib, re, shutil, subprocess

def run(*args):
    return subprocess.check_output(args, text=True).strip()

def dependencies(path):
    return [line.strip().split(' (compatibility version')[0]
            for line in run('otool', '-L', str(path)).splitlines()[1:]]

def rpaths(path):
    return re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', run('otool', '-l', str(path)))

def main():
    p = argparse.ArgumentParser(); p.add_argument('encoder'); p.add_argument('app'); p.add_argument('arch'); a = p.parse_args()
    source = pathlib.Path(a.encoder).resolve()
    dest = pathlib.Path(a.app) / 'Contents' / 'Helpers' / a.arch
    libs = dest / 'lib'; libs.mkdir(parents=True, exist_ok=True)
    prefix = pathlib.Path(run('brew', '--prefix'))
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
        subprocess.run(['codesign', '--remove-signature', str(copied)], capture_output=True)
        if copied != dest / 'ffmpeg': subprocess.check_call(['install_name_tool', '-id', '@rpath/' + copied.name, str(copied)])
        for old, dependency in edges[original]:
            replacement = '@loader_path/' + ('lib/' if original == source else '') + mapped[dependency].name
            subprocess.check_call(['install_name_tool', '-change', old, replacement, str(copied)])
        for rpath in rpaths(copied):
            if rpath.startswith(('/opt/homebrew', '/usr/local')):
                subprocess.check_call(['install_name_tool', '-delete_rpath', rpath, str(copied)])
        # These are copied helpers, never files in the build machine's Homebrew cellar.
        subprocess.check_call(['codesign', '--force', '--sign', '-', str(copied)])
        for link in dependencies(copied):
            if not link.startswith(('/usr/lib/', '/System/Library/', '@loader_path/', '@rpath/')):
                raise RuntimeError(f'External dependency survived relocation: {copied}: {link}')
        if 'Cellar' in original.parts:
            formulas.add(original.parts[original.parts.index('Cellar') + 1])
    notices = pathlib.Path(a.app) / 'Contents' / 'Resources' / 'ThirdParty' / a.arch
    notices.mkdir(parents=True, exist_ok=True)
    metadata = json.loads(run('brew', 'info', '--json=v2', *sorted(formulas)))
    (notices / 'homebrew-sources.json').write_text(json.dumps(metadata, indent=2) + '\n')
    for formula in metadata['formulae']:
        formula_dir = prefix / 'opt' / formula['name']
        for root in [formula_dir, formula_dir / 'share' / 'licenses', formula_dir / '.brew']:
            if not root.exists(): continue
            for item in root.iterdir():
                if item.is_file() and (re.match(r'(?i)(copying|license|notice|copyright)', item.name) or item.suffix == '.rb'):
                    target = notices / formula['name']; target.mkdir(exist_ok=True); shutil.copy2(item, target / item.name)
    (notices / 'FFmpeg-build.txt').write_text(run(str(dest / 'ffmpeg'), '-version') + '\n\n' + run(str(dest / 'ffmpeg'), '-buildconf') + '\n')
    (notices / 'README.txt').write_text('FFmpeg and its libraries are separate open-source programs.\nExact source URLs, versions, checksums and build formulae are recorded alongside this notice.\nFFmpeg: https://ffmpeg.org/ ; Homebrew formulae: https://github.com/Homebrew/homebrew-core\nThe application invokes FFmpeg as a child process. No Homebrew installation is needed by the end user.\n')
    manifest = {str(v.relative_to(dest)): {'source': str(k), 'sha256': hashlib.sha256(v.read_bytes()).hexdigest()} for k, v in mapped.items()}
    (notices / 'runtime-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Bundled {len(mapped)} files for {a.arch}')

if __name__ == '__main__': main()
