#!/usr/bin/env python3
"""Validate freshly installed AUv2 bundles after the runner registry notices them."""
import pathlib, plistlib, subprocess, sys, time

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
# The registrar may still have its pre-install component list cached.
subprocess.run(['killall', 'AudioComponentRegistrar'], check=False, capture_output=True)
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
    raise RuntimeError('Installed AU components were not discovered')
for _, subtype, filename in units:
    result = subprocess.run(['auval', '-v', 'aufx', subtype, 'Iisa'], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (logs / filename).write_text(result.stdout)
    print(result.stdout[-12000:], flush=True)
    result.check_returncode()
