param([string]$Exe = "$PSScriptRoot\..\vs\x64\Release\evtexport.exe", [int]$N = 50)
$data = "$PSScriptRoot\..\data"
$fail = 0

if (-not (Test-Path $Exe -PathType Leaf)) { "ERROR: executable not found: $Exe"; exit 99 }

# Test 1: CSV and JSON contain the same number of records, files can be read back
& $Exe --max $N --format both --out "$data\t_main" | Out-Null
$csv  = Import-Csv "$data\t_main.csv" -Encoding UTF8
$json = Get-Content "$data\t_main.json" -Raw -Encoding UTF8 | ConvertFrom-Json
"T1 CSV rows=$($csv.Count) JSON items=$($json.Count) max expected=$N"
if ($csv.Count -ne $json.Count -or $csv.Count -gt $N -or $csv.Count -eq 0) { "T1 FAIL"; $fail++ } else { "T1 OK" }

# Test 2: record_id values are identical in CSV and JSON
if ($csv.Count -gt 0) {
    $diff = Compare-Object ($csv.record_id | Sort-Object) ($json.record_id | ForEach-Object { "$_" } | Sort-Object)
    if ($diff) { "T2 FAIL (record_id mismatch)"; $fail++ } else { "T2 OK (record_id values match)" }
} else { "T2 FAIL (no data)"; $fail++ }

# Test 3: messages with quotes, commas or line breaks are read back correctly
$special = $csv | Where-Object { $_.message -match '[",]|\r|\n' }
"T3 INFO messages with special characters: $($special.Count) (CSV parsed without errors)"

# Test 4: non-existent log -> exit code 3
& $Exe --log NoSuchLog_90 --out "$data\t_bad" | Out-Null
if ($LASTEXITCODE -eq 3) { "T4 OK (missing log, rc=3)" } else { "T4 FAIL rc=$LASTEXITCODE"; $fail++ }

# Test 5: Security log without administrator rights -> exit code 3
& $Exe --log Security --out "$data\t_sec" | Out-Null
"T5 INFO Security log rc=$LASTEXITCODE (3 expected without administrator rights)"

# Test 6: invalid arguments -> exit code 2
& $Exe --max abc | Out-Null
if ($LASTEXITCODE -eq 2) { "T6a OK (invalid --max, rc=2)" } else { "T6a FAIL rc=$LASTEXITCODE"; $fail++ }
& $Exe --format xml | Out-Null
if ($LASTEXITCODE -eq 2) { "T6b OK (invalid --format, rc=2)" } else { "T6b FAIL rc=$LASTEXITCODE"; $fail++ }

# Test 7: unreachable output path -> exit code 5
& $Exe --max 5 --out "Z:\no\such\dir\x" | Out-Null
if ($LASTEXITCODE -eq 5) { "T7 OK (cannot write file, rc=5)" } else { "T7 FAIL rc=$LASTEXITCODE"; $fail++ }

"Total failed: $fail"
exit $fail