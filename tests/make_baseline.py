# Prepare an isolated upstream source tree without changing its DSP.
from pathlib import Path
import subprocess, sys
repo=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]) if len(sys.argv)>1 else Path('D:/Codex/Temp/SpectrumTag-baseline-source')
if out.drive.lower()!='d:': raise SystemExit('Use a D: source/build directory.')
if out.exists(): raise SystemExit('Destination already exists; use a new directory.')
commit='ac7d32470d1e7d5ae447e00fb01ebb8ee12631e6'
def git(*args): return subprocess.check_output(['git','-c','core.fsmonitor=false','-C',str(repo),*args])
for raw in git('ls-tree','-rz','--name-only',commit).split(b'\0'):
    if not raw: continue
    rel=raw.decode(); f=out/rel; f.parent.mkdir(parents=True,exist_ok=True)
    f.write_bytes(git('show',commit+':'+rel))
f=out/'CMakeLists.txt';s=f.read_text(encoding='utf-8-sig').replace('COPY_PLUGIN_AFTER_BUILD TRUE','COPY_PLUGIN_AFTER_BUILD FALSE').replace('NEEDS_CURL TRUE','NEEDS_CURL FALSE').replace('JUCE_USE_CURL=1','JUCE_USE_CURL=0')
s+='''
juce_add_console_app(SpectrumTagVerify PRODUCT_NAME "SpectrumTagVerify")
juce_generate_juce_header(SpectrumTagVerify)
target_sources(SpectrumTagVerify PRIVATE tests/Verify.cpp)
target_link_libraries(SpectrumTagVerify PRIVATE SpectrumTag)
target_include_directories(SpectrumTagVerify PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} $<TARGET_PROPERTY:SpectrumTag,INCLUDE_DIRECTORIES>)
target_compile_definitions(SpectrumTagVerify PRIVATE $<TARGET_PROPERTY:SpectrumTag,COMPILE_DEFINITIONS>)
'''
f.write_text(s,encoding='utf-8')
f=out/'PluginProcessor.cpp';s=f.read_text(encoding='utf-8-sig');s=s.replace('    // 启动时延迟 5 秒检查一次更新','#if 0\n    // 启动时延迟 5 秒检查一次更新').replace('\n}\n\nSpectrumTagAudioProcessor::~','\n#endif\n}\n\nSpectrumTagAudioProcessor::~');f.write_text(s,encoding='utf-8')
f=out/'PluginEditor.cpp';s=f.read_text(encoding='utf-8-sig');s=s.replace('f.getStringWidthFloat (text)','juce::GlyphArrangement::getStringWidth (f, text)');s=s.replace('    telemetrySession = std::make_unique','#if 0\n    telemetrySession = std::make_unique').replace('JucePlugin_VersionString, processor.wrapperType));','JucePlugin_VersionString, processor.wrapperType));\n#endif');f.write_text(s,encoding='utf-8')
(out/'tests').mkdir();(out/'tests'/'Verify.cpp').write_text((repo/'tests'/'BaselineVerify.cpp').read_text(encoding='utf-8'),encoding='utf-8')
print(out)
