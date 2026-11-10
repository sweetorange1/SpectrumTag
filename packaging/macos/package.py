#!/usr/bin/env python3
"""Merge tested native bundles into one universal macOS installer."""
import argparse, pathlib, plistlib, shutil, subprocess, xml.etree.ElementTree as ET

def call(*args): subprocess.check_call([str(a) for a in args])

def merge_bundle(arm, intel, target):
    shutil.copytree(arm, target)
    for file in arm.rglob('*'):
        if not file.is_file() or 'Helpers' in file.parts: continue
        other = intel / file.relative_to(arm)
        kind = subprocess.check_output(['file', '-b', str(file)], text=True)
        if 'Mach-O' in kind:
            if not other.is_file(): raise RuntimeError(f'Missing Intel slice: {other}')
            call('lipo', '-create', file, other, '-output', target / file.relative_to(arm))
            call('lipo', target / file.relative_to(arm), '-verify_arch', 'arm64', 'x86_64')
    helpers = intel / 'Contents' / 'Helpers' / 'x86_64'
    if helpers.exists(): shutil.copytree(helpers, target / 'Contents' / 'Helpers' / 'x86_64')
    notices = intel / 'Contents' / 'Resources' / 'ThirdParty' / 'x86_64'
    if notices.exists(): shutil.copytree(notices, target / 'Contents' / 'Resources' / 'ThirdParty' / 'x86_64')
    resources=target/'Contents/Resources'; resources.mkdir(exist_ok=True)
    shutil.copy2('LICENSE', resources/'SpectrumTag-LICENSE.txt')
    for guide in ['安装说明.txt', 'INSTALL-EN.txt']:
        shutil.copy2(pathlib.Path('docs')/guide, resources/guide)
    for file in target.rglob('*'):
        if file.is_file() and 'Mach-O' in subprocess.check_output(['file', '-b', str(file)], text=True):
            call('codesign', '--force', '--sign', '-', file)
    call('codesign', '--force', '--sign', '-', target)
    call('codesign', '--verify', '--deep', '--strict', '--verbose=2', target)

def main():
    p=argparse.ArgumentParser(); p.add_argument('arm'); p.add_argument('intel'); p.add_argument('output'); p.add_argument('--version', required=True); a=p.parse_args()
    arm, intel, out = map(pathlib.Path, (a.arm, a.intel, a.output)); out.mkdir(parents=True, exist_ok=True)
    staging = out / 'staging'; staging.mkdir()
    components = [
      ('standalone', 'SpectrumTag.app', 'Applications', 'Standalone audio/video editor'),
      ('vst3', 'SpectrumTag.vst3', 'Library/Audio/Plug-Ins/VST3', 'Realtime VST3'),
      ('ara-vst3', 'SpectrumTag ARA.vst3', 'Library/Audio/Plug-Ins/VST3', 'ARA VST3 extension'),
      ('au', 'SpectrumTag.component', 'Library/Audio/Plug-Ins/Components', 'Realtime Audio Unit'),
      ('ara-au', 'SpectrumTag ARA.component', 'Library/Audio/Plug-Ins/Components', 'ARA Audio Unit extension')]
    distribution=ET.Element('installer-gui-script', {'minSpecVersion':'2'})
    ET.SubElement(distribution,'title').text=f'SpectrumTag {a.version}'
    ET.SubElement(distribution,'options',{'customize':'always','require-scripts':'false','hostArchitectures':'arm64,x86_64'})
    ET.SubElement(distribution,'domains',{'enable_anywhere':'false','enable_currentUserHome':'false','enable_localSystem':'true'})
    osversions=ET.SubElement(distribution,'allowed-os-versions'); ET.SubElement(osversions,'os-version',{'min':'15.0'})
    outline=ET.SubElement(distribution,'choices-outline')
    for key, name, location, title in components:
        root=staging / key; target=root / location / name; target.parent.mkdir(parents=True)
        merge_bundle(arm / name, intel / name, target)
        plist=plistlib.loads((target/'Contents/Info.plist').read_bytes())
        if plist['CFBundleShortVersionString'] != a.version: raise RuntimeError(f'Incorrect bundle version: {name}')
        component_id='cn.iisaacbeats.SpectrumTag.package.'+key
        pkg=out/(key+'.pkg'); plistfile=out/(key+'-components.plist')
        call('pkgbuild','--analyze','--root',root,plistfile)
        info=plistlib.loads(plistfile.read_bytes())
        for bundle in info:
            bundle['BundleIsRelocatable']=False
            bundle['BundleHasStrictIdentifier']=True
        plistfile.write_bytes(plistlib.dumps(info))
        call('pkgbuild','--root',root,'--identifier',component_id,'--version',a.version,'--install-location','/','--component-plist',plistfile,pkg)
        ET.SubElement(outline,'line',{'choice':key})
        choice=ET.SubElement(distribution,'choice',{'id':key,'title':title,'start_selected':'true'})
        ET.SubElement(choice,'pkg-ref',{'id':component_id})
        ET.SubElement(distribution,'pkg-ref',{'id':component_id,'version':a.version}).text=key+'.pkg'
    xml=out/'Distribution.xml'; ET.ElementTree(distribution).write(xml,encoding='utf-8',xml_declaration=True)
    installer=out/f'SpectrumTag-{a.version}-macOS.pkg'
    call('productbuild','--distribution',xml,'--package-path',out,installer)
    call('pkgutil','--expand-full',installer,out/'expanded-installer')
    print(installer)

if __name__=='__main__': main()
