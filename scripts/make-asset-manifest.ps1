# Generates assets.sha256 from a game folder. Run once against your clean Steam copy
# (or against ZeroHour-Dev AFTER you've applied whatever asset patches both of you should have),
# commit the output to the repo root, and every release will carry it. The launcher refuses
# to start when a .big on either machine doesn't match — the asset equivalent of a code desync.
#
#   .\scripts\make-asset-manifest.ps1 -GamePath "C:\Games\ZH\ZeroHour-Dev" -Out ..\assets.sha256
param(
    [Parameter(Mandatory)] [string] $GamePath,
    [string] $Out = "assets.sha256"
)
$root = (Resolve-Path $GamePath).Path
$lines = @("# sha256  relative-path   (generated $(Get-Date -Format s) from $root)")
Get-ChildItem $root -Recurse -Include *.big | Sort-Object FullName | ForEach-Object {
    $rel = $_.FullName.Substring($root.Length + 1).Replace('\', '/')
    $h = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $lines += "$h  $rel"
}
$lines | Set-Content -Encoding ascii $Out
Write-Host "Wrote $($lines.Count - 1) entries to $Out"
