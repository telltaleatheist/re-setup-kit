# files_manifest.py for a Windows package step with no Python: rebuild-files.sha256 at the top of a folder, the record
# the in-game menu's CHECK MY FILES reads (include/rekit/manifest.h). The same bytes as the .py: "<sha256 hex>  <path>\n"
# with '/' between folders, every regular file but the record itself, sorted by path (ordinal), UTF-8 without a BOM
# (checked against the .py on Windows PowerShell 5.1, 2026-11-27). When it runs: as the .py says, on the finished
# folder before the zip and the Setup are made from it.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File <kit>\tools\files_manifest.ps1 <folder> [-LeaveOut name,name]
param([Parameter(Mandatory = $true)][string]$Folder, [string[]]$LeaveOut = @())
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Folder).Path.TrimEnd('\')
$record = 'rebuild-files.sha256'
$leave = @{}
foreach ($n in $LeaveOut) { $leave[$n.ToLowerInvariant()] = $true }  # a file name left out, any folder (no case)
$rels = New-Object System.Collections.Generic.List[string]
foreach ($f in Get-ChildItem -LiteralPath $root -Recurse -File -Force) {
  if ($f.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
  $rel = $f.FullName.Substring($root.Length + 1).Replace('\', '/')
  if ($rel -eq $record -or $rel -eq "$record.part" -or $leave.ContainsKey($f.Name.ToLowerInvariant())) { continue }
  $rels.Add($rel)
}
$sorted = [string[]]$rels.ToArray()
[Array]::Sort($sorted, [StringComparer]::Ordinal)
$text = New-Object System.Text.StringBuilder
foreach ($rel in $sorted) {
  $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $root $rel)).Hash.ToLowerInvariant()
  [void]$text.Append("$hash  $rel`n")
}
$part = Join-Path $root "$record.part"
[IO.File]::WriteAllText($part, $text.ToString(), (New-Object System.Text.UTF8Encoding $false))
Move-Item -LiteralPath $part -Destination (Join-Path $root $record) -Force
Write-Output "files_manifest: $($sorted.Count) files in $(Join-Path $root $record)"
