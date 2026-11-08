#!/usr/bin/env python3
"""Validate freshly installed AUv2 bundles after the runner registry notices them."""
import os, pathlib, plistlib, subprocess, sys, time

root, logs = map(pathlib.Path, sys.argv[1:3])
logs.mkdir(parents=True, exist_ok=True)
units = [('SpectrumTag.component', 'SpTg', 'au.log'),
         ('SpectrumTag ARA.component', 'SpAr', 'ara-au.log')]
for name, subtype, _ in units:
    bundle = root / name
    metadata = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
    print(name, metadata.get('AudioComponents'), flush=True)
    components = metadata.get('AudioComponents', [])
    assert any(c.get('type') == 'aufx' and c.get('subtype') == subtype
               and c.get('manufacturer') == 'Iisa' for c in components), metadata
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)

# Only used on disposable CI runners, immediately after copying/installing AUs.
# The registrar may still have its pre-install component list cached. SIGTERM
# did not invalidate that list on Intel runners; explicitly restart this
# user's registrar after touching the installed bundle directories. Never
# delete the user's AU cache or relax system signing/security settings.
for path in [root] + [root / name for name, _, _ in units]:
    try:
        os.utime(path, None)
    except PermissionError:
        # System-wide PKG files belong to root; the installer already updated
        # these paths. Validation itself must run as the normal host user.
        print(f'Using installer-owned timestamp: {path}', flush=True)
restart = subprocess.run(['killall', '-9', 'AudioComponentRegistrar'], text=True,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(logs / 'au-registrar.log').write_text(f'exit={restart.returncode}\n{restart.stdout}')
time.sleep(2)
for attempt in range(15):
    listing = subprocess.run(['auval', '-a'], text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT).stdout
    (logs / 'au-discovery.log').write_text(listing)
    if all(any('aufx' in line and subtype in line and 'Iisa' in line
               for line in listing.splitlines()) for _, subtype, _ in units):
        break
    time.sleep(1)
else:
    print(listing, flush=True)
    diagnostics = subprocess.run(['log', 'show', '--last', '2m', '--style', 'compact',
                                  '--predicate', 'process == "AudioComponentRegistrar"'],
                                 text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (logs / 'au-registration-diagnostics.log').write_text(diagnostics.stdout)
    raise RuntimeError('Installed AU components were not discovered')
for _, subtype, filename in units:
    result = subprocess.run(['auval', '-v', 'aufx', subtype, 'Iisa'], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (logs / filename).write_text(result.stdout)
    print(result.stdout[-12000:], flush=True)
    result.check_returncode()
