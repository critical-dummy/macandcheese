$ErrorActionPreference = 'Stop'
$dmg = 'C:\Users\dummy135\Downloads\ChatGPTInstaller.dmg'
$out = Join-Path $PSScriptRoot 'chatgpt-dmg-xml.txt'
$bytes = [System.IO.File]::ReadAllBytes($dmg)
$trailerOffset = $bytes.Length - 512
function Read-U64BE([byte[]]$b, [int]$o) {
    [UInt64]$v = 0
    for ($i = 0; $i -lt 8; $i++) { $v = ($v -shl 8) -bor [UInt64]$b[$o + $i] }
    return $v
}
$xmlOffset = Read-U64BE $bytes ($trailerOffset + 216)
$xmlLength = Read-U64BE $bytes ($trailerOffset + 224)
$xml = [Text.Encoding]::UTF8.GetString($bytes, [int]$xmlOffset, [int]$xmlLength)
$xml | Set-Content -Encoding UTF8 $out
$matches = [regex]::Matches($xml, '(?is).{0,220}(?:Start Sector|<data>).{0,420}')
$matches | ForEach-Object { $_.Value; '' }
Write-Host "Saved XML to $out"
