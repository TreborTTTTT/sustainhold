# update_proxy.ps1 — overwrite the installed proxy DLL with the latest build.
$ErrorActionPreference = 'Stop'
$log = 'G:\Software\devtools\sustainhold\update_log.txt'
try {
  Copy-Item 'G:\Software\devtools\sustainhold\build\proxy.dll' 'C:\Program Files\VSTPlugins\TAL-U-No-62.dll' -Force
  $src  = (Get-Item 'G:\Software\devtools\sustainhold\build\proxy.dll').Length
  $dst  = (Get-Item 'C:\Program Files\VSTPlugins\TAL-U-No-62.dll').Length
  Set-Content -Path $log -Encoding utf8 -Value "RESULT: OK src=$src dst=$dst match=$($src -eq $dst)"
} catch {
  Set-Content -Path $log -Encoding utf8 -Value "RESULT: ERROR $_"
}
