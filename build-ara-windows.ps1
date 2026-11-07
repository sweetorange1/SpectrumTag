param(
    [string]$BuildDir = "D:\Codex\Temp\SpectrumTag-ARA-build",
    [string]$JuceDir = "D:\Codex\Temp\JUCE-8.0.15-src\JUCE-8.0.15",
    [string]$AraSdkDir = ""
)
$ErrorActionPreference = "Stop"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$config = @('-S', $PSScriptRoot, '-B', $BuildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DSPECTRUMTAG_BUILD_TESTS=ON', '-DSPECTRUMTAG_BUILD_ARA=ON', '-DSPECTRUMTAG_ENABLE_NETWORK=OFF')
if (Test-Path -LiteralPath $JuceDir) { $config += "-DFETCHCONTENT_SOURCE_DIR_JUCE=$JuceDir" }
if ($AraSdkDir) { $config += "-DSPECTRUMTAG_ARA_SDK_DIR=$AraSdkDir" }
& $cmake @config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $BuildDir --config Release --target SpectrumTagARA_VST3 SpectrumTagARAVerify -j 6
exit $LASTEXITCODE
