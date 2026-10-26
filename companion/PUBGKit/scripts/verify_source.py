from pathlib import Path
import plistlib,re
r=Path(__file__).resolve().parents[1]
mk=(r/'Makefile').read_text()
sources=re.search(r'^PUBGKit_FILES = (.+)$',mk,re.M).group(1).split()
assert all((r/f).is_file() for f in sources)
assert not any('Stealth' in f for f in sources)
assert 'THEOS_PACKAGE_SCHEME = rootless' in mk
assert 'Architecture: iphoneos-arm64' in (r/'control').read_text()
assert mk.index('common.mk')<mk.index('TWEAK_NAME')
config=plistlib.loads((r/'resources/offsets.plist').read_bytes())
assert config['controllerControlRotation']==0
assert 'com.vng.pubgmobile' in (r/'PUBGKit.plist').read_text()
for name in sources:
    s=(r/name).read_text(encoding='utf-8')
    if name.endswith(('.mm','.xm')):
        assert s.count('@interface')+s.count('@implementation')==s.count('@end'),name
print('PASS: source inventory, rootless settings, metadata, resource plist and Objective-C block inventory')
print('NOT RUN: C++ test, iOS compilation/link, .deb packaging, device/runtime validation')
