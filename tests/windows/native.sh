#!/bin/sh
# The compiler itself on Windows: xisto.exe (built here by zig) compiles every tests/ok program on the Windows
# machine, runs it and compares with Node's output (build/tests/*.node: run `make test` first); every tests/err
# program must fail there with its message (runtime errors: when run). The Windows machine needs zig on PATH
# (or at C:\zig\zig.exe) and an OpenSSH server.
# Usage: XISTO_WIN=Administrator@host XISTO_WIN_KEY=key.pem sh tests/windows/native.sh
cd "$(dirname "$0")/../.." || exit 1
[ -n "$XISTO_WIN" ] || { echo "skip: set XISTO_WIN=user@host (and XISTO_WIN_KEY)"; exit 0; }
KEY=${XISTO_WIN_KEY:+-i $XISTO_WIN_KEY}
ls build/tests/*.node > /dev/null 2>&1 || { echo "run make test first (Node's outputs)"; exit 1; }
W=bench/out/windows-native; rm -rf "$W"; mkdir -p "$W/xisto/build" "$W/xisto/third_party" "$W/xisto/oracle"
zig cc -target x86_64-windows-gnu -std=c11 -O2 -D_GNU_SOURCE -w -o "$W/xisto/build/xisto.exe" src/*.c build/rt_embed.c -lm || exit 1
cp build/sqlite3.h "$W/xisto/build/"
mkdir -p "$W/xisto/third_party/bearssl" "$W/xisto/third_party/sqlite"
cp -r third_party/bearssl/src third_party/bearssl/inc "$W/xisto/third_party/bearssl/"
cp third_party/sqlite/sqlite3.c third_party/sqlite/sqlite3.h "$W/xisto/third_party/sqlite/"
mkdir -p "$W/xisto/tests"; cp -r tests/ok tests/err tests/stack tests/unit tests/fmt "$W/xisto/tests/"
cp build/tests/*.node "$W/xisto/oracle/"
: > "$W/xisto/err.txt" # name|expected message, for the runner to check there
for f in tests/err/*.ts; do printf '%s|%s\n' "$(basename "$f" .ts)" "$(sed -n '1s|^// expect: ||p' "$f")" >> "$W/xisto/err.txt"; done
cat > "$W/xisto/run.ps1" <<'IN'
$env:TZ = "EST5EDT"
$env:XISTO_NODE_COMPAT = "1"
if (-not $env:XISTO_ZIG -and (Test-Path C:\zig\zig.exe)) { $env:XISTO_ZIG = "C:/zig/zig.exe" }
New-Item -ItemType Directory -Force C:\tmp, C:\xn\bin, C:\xn\out | Out-Null
Set-Location C:\xn
$x = "C:\xn\build\xisto.exe"
$pass = 0; $fail = 0
# tests/ok: built here by xisto.exe, run, compared with Node's output
Get-ChildItem C:\xn\tests\ok\*.ts | ForEach-Object {
  $n = $_.BaseName
  if (-not (Test-Path "C:\xn\oracle\$n.node")) { return }
  if ($n -eq "process-timers") { return } # shell commands (printf)
  $exe = "C:\xn\bin\$n.exe"
  cmd /c "`"$x`" build -o `"$exe`" tests/ok/$n.ts > C:\xn\out\$n.build 2>&1"
  if ($LASTEXITCODE -ne 0) { $fail++; Write-Output "FAIL build $n"; Get-Content C:\xn\out\$n.build | Select-Object -First 4; return }
  Push-Location C:\xn\tests\ok
  cmd /c "`"$exe`" > C:\xn\out\$n.txt 2>&1 < NUL"
  Pop-Location
  $a = [System.IO.File]::ReadAllBytes("C:\xn\out\$n.txt")
  $b = [System.IO.File]::ReadAllBytes("C:\xn\oracle\$n.node")
  if ([System.Linq.Enumerable]::SequenceEqual($a, $b)) { $pass++ } else { $fail++; Write-Output "FAIL $n" }
}
Write-Output "windows native ok: $pass passed, $fail failed"
# tests/err: the compile error's message, or the runtime error's
$ep = 0; $ef = 0
Get-Content C:\xn\err.txt | ForEach-Object {
  $p = $_.Split('|', 2); $n = $p[0]; $want = $p[1]
  $exe = "C:\xn\bin\err-$n.exe"
  cmd /c "`"$x`" build -o `"$exe`" tests/err/$n.ts > C:\xn\out\err-$n.txt 2>&1"
  $built = $LASTEXITCODE -eq 0
  if ($want.StartsWith("runtime error")) {
    if (-not $built) { $ef++; Write-Output "FAIL err $n (should compile)"; return }
    cmd /c "`"$exe`" > C:\xn\out\err-$n.txt 2>&1 < NUL"
    $want = $want.Substring(15)
  } elseif ($built) { $ef++; Write-Output "FAIL err $n (compiled)"; return }
  $text = [System.IO.File]::ReadAllText("C:\xn\out\err-$n.txt")
  if ($text.Contains($want)) { $ep++ } else { $ef++; Write-Output "FAIL err $n (expected '$want')" }
}
Write-Output "windows native err: $ep passed, $ef failed"
# the compiler's own commands: fmt, xisto test, clean
$tp = 0; $tf = 0
Copy-Item C:\xn\tests\fmt\messy.input C:\xn\out\messy.ts
cmd /c "`"$x`" fmt C:\xn\out\messy.ts > NUL 2>&1"
$a = [System.IO.File]::ReadAllBytes("C:\xn\out\messy.ts"); $b = [System.IO.File]::ReadAllBytes("C:\xn\tests\fmt\messy.expected")
cmd /c "`"$x`" fmt --check C:\xn\out\messy.ts > NUL 2>&1"
if ([System.Linq.Enumerable]::SequenceEqual($a, $b) -and $LASTEXITCODE -eq 0) { $tp++ } else { $tf++; Write-Output "FAIL fmt messy" }
$files = (Get-ChildItem -Recurse C:\xn\tests\ok,C:\xn\tests\unit,C:\xn\tests\stack -Filter *.ts | ForEach-Object { "`"$($_.FullName)`"" }) -join " "
cmd /c "`"$x`" fmt --check $files > C:\xn\out\fmt.txt 2>&1"
if ($LASTEXITCODE -eq 0) { $tp++ } else { $tf++; Write-Output "FAIL fmt --check"; Get-Content C:\xn\out\fmt.txt | Select-Object -First 4 }
cmd /c "`"$x`" test tests/unit > C:\xn\out\unit.txt 2>&1"
$u = [System.IO.File]::ReadAllText("C:\xn\out\unit.txt")
if ($LASTEXITCODE -eq 0 -and $u.Contains(" 0 failed")) { $tp++ } else { $tf++; Write-Output "FAIL xisto test"; Write-Output $u }
New-Item -ItemType Directory -Force C:\xn\unit-fail | Out-Null
Copy-Item C:\xn\tests\unit\failing.test.ts.disabled C:\xn\unit-fail\failing.test.ts
cmd /c "`"$x`" test C:/xn/unit-fail > C:\xn\out\unitf.txt 2>&1"
$uf = [System.IO.File]::ReadAllText("C:\xn\out\unitf.txt")
if ($LASTEXITCODE -ne 0 -and $uf.Contains("assertion failed: math is broken")) { $tp++ } else { $tf++; Write-Output "FAIL xisto test: the failing assert"; Write-Output $uf }
cmd /c "`"$x`" clean tests/ok > C:\xn\out\clean.txt 2>&1"
if ($LASTEXITCODE -eq 0 -and -not (Test-Path C:\xn\tests\ok\.xisto\cache)) { $tp++ } else { $tf++; Write-Output "FAIL clean"; Get-Content C:\xn\out\clean.txt }
Write-Output "windows native tools: $tp passed, $tf failed"
IN
(cd "$W" && rm -f xn.zip && zip -qr xn.zip xisto)
scp $KEY -o BatchMode=yes -q "$W/xn.zip" "$XISTO_WIN:C:/xn.zip" || exit 1
ssh $KEY -o BatchMode=yes "$XISTO_WIN" 'powershell -NoProfile -Command "Remove-Item -Recurse -Force C:\xn,C:\xnt -ErrorAction SilentlyContinue; Expand-Archive -Force C:\xn.zip C:\xnt; Move-Item C:\xnt\xisto C:\xn; Remove-Item C:\xnt; & C:\xn\run.ps1"' | tee "$W/result.txt"
grep -q "ok: .* 0 failed" "$W/result.txt" && grep -q "err: .* 0 failed" "$W/result.txt" && grep -q "tools: .* 0 failed" "$W/result.txt"
