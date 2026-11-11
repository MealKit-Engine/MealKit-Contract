# Copies mkcontract into another repository's third_party folder (MealKit Manager, MkAudio, other tools).
#   powershell -ExecutionPolicy Bypass -File tools\vendor.ps1 -To <repo>\third_party\mkcontract [-Ref v0.1.0]
# - Extracts -Ref (tag/commit, default HEAD) with git archive, so uncommitted edits in this checkout don't go along
# - Copies CMakeLists.txt, include/, src/, LICENSE and writes VERSION.txt ("<tag or version+commit> (<commit>)"). Tests, docs and CI stay here
# (The MealKit engine uses its own tools\update_mkcontract.ps1, which also keeps its third-party notices.)
param(
    [Parameter(Mandatory = $true)][string]$To,
    [string]$Ref = "HEAD"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

$commit = (& git -C $repo rev-parse --short "$Ref^{commit}").Trim()
if ($LASTEXITCODE -ne 0) { throw "not a git commit: $Ref" }
$tag = (& cmd /c "git -C `"$repo`" describe --tags --exact-match $commit 2>nul") # no tag is not an error (cmd keeps git's stderr out of PowerShell)
if ($LASTEXITCODE -ne 0) { $tag = "" }
$cmake = (& git -C $repo show "${commit}:CMakeLists.txt") -join "`n"
if ($cmake -notmatch 'project\(mkcontract VERSION ([0-9.]+)') { throw "CMakeLists.txt has no project(mkcontract VERSION ...)" }
$version = if ($tag) { $tag.Trim() } else { "v$($Matches[1])+$commit" }

$tmp = Join-Path ([IO.Path]::GetTempPath()) ("mkcontract-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $tmp | Out-Null
try {
    $tar = Join-Path $tmp "src.tar"
    & git -C $repo archive --format=tar -o $tar $commit CMakeLists.txt include src LICENSE
    if ($LASTEXITCODE -ne 0) { throw "git archive failed" }
    $tarExe = if ($env:SystemRoot) { Join-Path $env:SystemRoot "System32\tar.exe" } else { "tar" } # Windows tar (Git's tar reads C: paths as hosts)
    & $tarExe -xf $tar -C $tmp
    if ($LASTEXITCODE -ne 0) { throw "tar failed" }
    Remove-Item $tar
    if (Test-Path $To) { Remove-Item -Recurse -Force $To }
    New-Item -ItemType Directory -Force $To | Out-Null
    Get-ChildItem $tmp | Copy-Item -Destination $To -Recurse -Force
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
[IO.File]::WriteAllText((Join-Path $To "VERSION.txt"), "mkcontract $version ($commit) from https://github.com/MealKit-Engine/MealKit-Contract`n",
    (New-Object System.Text.UTF8Encoding $false))
Write-Host "$To <- mkcontract $version ($commit)"
