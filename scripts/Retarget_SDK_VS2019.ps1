param(
    [string]$SdkRoot = (Join-Path $PSScriptRoot "..\sdk")
)
$ErrorActionPreference = 'Stop'
$SdkRoot = [IO.Path]::GetFullPath($SdkRoot)
if (-not (Test-Path (Join-Path $SdkRoot 'foobar2000\SDK\foobar2000.h'))) {
    throw "SDK not found: $SdkRoot"
}

$projects = Get-ChildItem -Path $SdkRoot -Recurse -Filter *.vcxproj -File
$changed = 0
foreach ($f in $projects) {
    $text = [IO.File]::ReadAllText($f.FullName)
    $new = $text
    $new = $new -replace '<PlatformToolset>v14[3-9]</PlatformToolset>', '<PlatformToolset>v142</PlatformToolset>'
    $new = $new -replace '<LanguageStandard>stdcpp20</LanguageStandard>', '<LanguageStandard>stdcpp17</LanguageStandard>'
    $new = $new -replace '<LanguageStandard>stdcpplatest</LanguageStandard>', '<LanguageStandard>stdcpp17</LanguageStandard>'
    if ($new -ne $text) {
        [IO.File]::WriteAllText($f.FullName, $new, (New-Object Text.UTF8Encoding($true)))
        $changed++
    }
}
Write-Host "[OK] Retargeted SDK projects for VS2019/v142. Changed: $changed"
