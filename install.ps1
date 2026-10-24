# install.ps1 — install the sustain-hold proxy in front of TAL-U-No-62.
# Run elevated. Writes result to install_log.txt next to this script.
$ErrorActionPreference = 'Stop'
$log = 'G:\Software\devtools\sustainhold\install_log.txt'
$out = @()
try {
  $dir      = 'C:\Program Files\VSTPlugins'
  $orig     = Join-Path $dir 'TAL-U-No-62.dll'
  $sub      = Join-Path $dir 'TAL-U-No-62-original'
  $real     = Join-Path $sub 'TAL-U-No-62-original.dll'
  $proxySrc = 'G:\Software\devtools\sustainhold\build\proxy.dll'

  if (Test-Path $real) {
    $out += "real plugin already at: $real ($((Get-Item $real).Length) bytes)"
  } else {
    if (-not (Test-Path $orig)) { throw "original DLL not found at $orig" }
    New-Item -ItemType Directory -Path $sub -Force | Out-Null
    Move-Item $orig $real
    $out += "moved original -> $real ($((Get-Item $real).Length) bytes)"
  }
  Copy-Item $proxySrc $orig -Force
  $out += "proxy installed -> $orig ($((Get-Item $orig).Length) bytes)"

  $ini = Join-Path $dir 'sustainhold.ini'
  Set-Content -Path $ini -Encoding ascii -Value @(
    '[sustainhold]',
    'target=TAL-U-No-62-original\TAL-U-No-62-original.dll'
  )
  $out += "ini written -> $ini"
  $out += 'RESULT: OK'
} catch {
  $out += "RESULT: ERROR $_"
}
Set-Content -Path $log -Encoding utf8 -Value $out
