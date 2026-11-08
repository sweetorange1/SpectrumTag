param(
    [string]$BuildDir = "D:\Codex\Temp\SpectrumTag-build",
    [string]$JuceDir = "D:\Codex\Temp\JUCE-8.0.15-src\JUCE-8.0.15",
    [string]$RuntimeDir = "D:\Codex\Temp\SpectrumTag-EmbeddedRuntime"
)
$ErrorActionPreference = "Stop"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$config = @('-S', $PSScriptRoot, '-B', $BuildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DSPECTRUMTAG_BUILD_TESTS=ON', '-DSPECTRUMTAG_ENABLE_NETWORK=OFF')
if (Test-Path -LiteralPath $JuceDir) { $config += "-DFETCHCONTENT_SOURCE_DIR_JUCE=$JuceDir" }
$config += "-DSPECTRUMTAG_STANDALONE_RUNTIME_DIR=$RuntimeDir"
& $cmake @config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $BuildDir --config Release --target SpectrumTagStandalone -j 6
exit $LASTEXITCODE
