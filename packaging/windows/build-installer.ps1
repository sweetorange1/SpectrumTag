param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$StageDir,
    [Parameter(Mandatory=$true)][string]$OutputDir,
    [Parameter(Mandatory=$true)][string]$Compiler,
    [Parameter(Mandatory=$true)][string]$VCRuntime,
    [string]$CodecRuntimeDir='D:\Codex\Temp\SpectrumTag-EmbeddedRuntime'
)
$ErrorActionPreference='Stop'
$source=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$match=[regex]::Match([IO.File]::ReadAllText((Join-Path $source 'CMakeLists.txt')), 'project\(SpectrumTag VERSION ([0-9.]+)')
if(!$match.Success){throw 'Cannot identify product version'}
$version=$match.Groups[1].Value
$items=@(
    @{Relative='SpectrumTag_artefacts\Release\VST3\SpectrumTag.vst3'; Name='SpectrumTag.vst3'; Binary='Contents\x86_64-win\SpectrumTag.vst3'},
    @{Relative='SpectrumTagARA_artefacts\Release\VST3\SpectrumTag ARA.vst3'; Name='SpectrumTag ARA.vst3'; Binary='Contents\x86_64-win\SpectrumTag ARA.vst3'},
    @{Relative='SpectrumTagStandalone_artefacts\Release\SpectrumTag.exe'; Name='SpectrumTag.exe'; Binary=''}
)
New-Item -ItemType Directory -Path $StageDir,$OutputDir,(Join-Path $StageDir 'Docs') -Force | Out-Null
foreach($item in $items){
    $input=Join-Path $BuildDir $item.Relative
    $binary=if($item.Binary){Join-Path $input $item.Binary}else{$input}
    if((Get-Item -LiteralPath $binary).VersionInfo.ProductVersion -ne $version){throw "Incorrect version: $binary"}
    Copy-Item -LiteralPath $input -Destination $StageDir -Recurse -Force
}
if((Get-AuthenticodeSignature -LiteralPath $VCRuntime).Status -ne 'Valid'){throw 'Microsoft runtime signature verification failed'}
Copy-Item -LiteralPath $VCRuntime -Destination (Join-Path $StageDir 'vc_redist.x64.exe') -Force
Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination (Join-Path $StageDir 'Docs\LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $source 'docs\安装说明.txt'),(Join-Path $source 'docs\INSTALL-EN.txt') -Destination (Join-Path $StageDir 'Docs') -Force
Copy-Item -LiteralPath (Join-Path $CodecRuntimeDir 'COPYING.GPLv3'),(Join-Path $CodecRuntimeDir 'FFmpeg-NOTICE.txt') -Destination (Join-Path $StageDir 'Docs') -Force
& $Compiler "/DVersion=$version" "/DPayload=$StageDir" "/DOutput=$OutputDir" (Join-Path $PSScriptRoot 'installer.iss')
if($LASTEXITCODE -ne 0){throw 'Installer compiler failed'}
$installer=Join-Path $OutputDir "SpectrumTag-$version-Windows-Setup.exe"
Get-Item -LiteralPath $installer | Select-Object FullName,Length,@{n='Version';e={$_.VersionInfo.ProductVersion}}
Get-FileHash -LiteralPath $installer -Algorithm SHA256
