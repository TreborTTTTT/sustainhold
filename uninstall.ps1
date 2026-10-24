# uninstall.ps1 — revert TAL-U-No-62 to the original plugin. Run elevated.
$ErrorActionPreference = 'Stop'
$log = 'G:\Software\devtools\sustainhold\uninstall_log.txt'
$out = @()
try {
  $dir  = 'C:\Program Files\VSTPlugins'
  $orig = Join-Path $dir 'TAL-U-No-62.dll'
  $sub  = Join-Path $dir 'TAL-U-No-62-original'
  $real = Join-Path $sub 'TAL-U-No-62-original.dll'
  $ini  = Join-Path $dir 'sustainhold.ini'

  if (Test-Path $real) {
    Remove-Item $orig -Force -ErrorAction SilentlyContinue
    Move-Item $real $orig
    $out += "original restored -> $orig"
    Remove-Item $sub -Force -ErrorAction SilentlyContinue
    if (Test-Path $ini) { Remove-Item $ini -Force; $out += "ini removed" }
    $out += 'RESULT: OK'
  } else {
    $out += 'RESULT: nothing to revert (no original subdir found)'
  }
} catch {
  $out += "RESULT: ERROR $_"
}
Set-Content -Path $log -Encoding utf8 -Value $out
