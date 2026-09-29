param()

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$proj = Join-Path $root "sdk\foobar2000\SDK\foobar2000_SDK.vcxproj"

if (!(Test-Path $proj)) {
    Write-Host "[ERROR] SDK project not found:" -ForegroundColor Red
    Write-Host "  $proj"
    exit 1
}

$backup = "$proj.v1_4_backup"
if (!(Test-Path $backup)) {
    Copy-Item $proj $backup -Force
    Write-Host "[OK] Backup created:" -ForegroundColor Green
    Write-Host "  $backup"
}

[xml]$xml = Get-Content -LiteralPath $proj
$ns = New-Object System.Xml.XmlNamespaceManager($xml.NameTable)
$ns.AddNamespace("m", "http://schemas.microsoft.com/developer/msbuild/2003")

$changed = 0

# Disable PCH in every compiler settings block.
$clNodes = $xml.SelectNodes("//m:ItemDefinitionGroup/m:ClCompile", $ns)
foreach ($cl in $clNodes) {
    $pch = $cl.SelectSingleNode("m:PrecompiledHeader", $ns)
    if ($null -eq $pch) {
        $pch = $xml.CreateElement("PrecompiledHeader", $xml.Project.NamespaceURI)
        $cl.AppendChild($pch) | Out-Null
    }
    if ($pch.InnerText -ne "NotUsing") {
        $pch.InnerText = "NotUsing"
        $changed++
    }

    $mp = $cl.SelectSingleNode("m:MultiProcessorCompilation", $ns)
    if ($null -eq $mp) {
        $mp = $xml.CreateElement("MultiProcessorCompilation", $xml.Project.NamespaceURI)
        $cl.AppendChild($mp) | Out-Null
    }
    if ($mp.InnerText -ne "false") {
        $mp.InnerText = "false"
        $changed++
    }

    # /Zm is not appropriate for this symptom; remove an inherited explicit /Zm if present.
    $opts = $cl.SelectSingleNode("m:AdditionalOptions", $ns)
    if ($null -ne $opts -and $opts.InnerText -match "(?i)/Zm\d+") {
        $opts.InnerText = [regex]::Replace($opts.InnerText, "(?i)(^|\s)/Zm\d+(\s|$)", " ")
        $changed++
    }

    # Release static SDK lib does not need compiler PDB info for this local build.
    $group = $cl.ParentNode
    if ($group.Condition -match "Release") {
        $dbg = $cl.SelectSingleNode("m:DebugInformationFormat", $ns)
        if ($null -eq $dbg) {
            $dbg = $xml.CreateElement("DebugInformationFormat", $xml.Project.NamespaceURI)
            $cl.AppendChild($dbg) | Out-Null
        }
        if ($dbg.InnerText -ne "None") {
            $dbg.InnerText = "None"
            $changed++
        }
    }
}

# stdafx.cpp can also override the project-wide setting.
$stdafxNodes = $xml.SelectNodes("//m:ClCompile[contains(translate(@Include,'ABCDEFGHIJKLMNOPQRSTUVWXYZ','abcdefghijklmnopqrstuvwxyz'),'stdafx.cpp')]", $ns)
foreach ($node in $stdafxNodes) {
    foreach ($pch in $node.SelectNodes("m:PrecompiledHeader", $ns)) {
        if ($pch.InnerText -ne "NotUsing") {
            $pch.InnerText = "NotUsing"
            $changed++
        }
    }
}

$settings = New-Object System.Xml.XmlWriterSettings
$settings.Indent = $true
$settings.Encoding = New-Object System.Text.UTF8Encoding($false)
$writer = [System.Xml.XmlWriter]::Create($proj, $settings)
$xml.Save($writer)
$writer.Close()

Write-Host "[OK] foobar2000_SDK.vcxproj patched for low memory." -ForegroundColor Green
Write-Host "     PCH                  : OFF"
Write-Host "     /MP                  : OFF"
Write-Host "     Release debug info   : OFF"
Write-Host "     Explicit /Zm         : removed if present"
Write-Host "     XML changes          : $changed"
exit 0
