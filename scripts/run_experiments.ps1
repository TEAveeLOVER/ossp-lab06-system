param([string]$Exe = "$PSScriptRoot\..\vs\x64\Release\evtexport.exe")
$data  = "$PSScriptRoot\..\data"
$stats = "$data\results.csv"
if (Test-Path $stats) { Remove-Item $stats }

$configs = @{ "A"=20; "B"=100; "V"=300; "IND"=130 }
foreach ($c in $configs.Keys) {
  1..3 | ForEach-Object {
    & $Exe --max $configs[$c] --format both --out "$data\events_$c" --stats $stats --config $c --run $_ | Out-Null
  }
}
# дополнительный эксперимент: только ошибки и предупреждения
1..3 | ForEach-Object {
  & $Exe --max 130 --level 3 --format both --out "$data\events_lvl3" --stats $stats --config "IND_L3" --run $_ | Out-Null
}
Get-Content $stats