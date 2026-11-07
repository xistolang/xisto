#!/bin/sh
# xisto build --target=windows-x64 on a real Windows machine over SSH: every tests/ok program that builds for
# Windows runs there and must print exactly what Node printed (build/tests/*.node: run `make test` first).
# Usage: XISTO_WIN=Administrator@host XISTO_WIN_KEY=key.pem sh tests/windows/run.sh   (needs zig, an OpenSSH server)
cd "$(dirname "$0")/../.." || exit 1
[ -n "$XISTO_WIN" ] || { echo "skip: set XISTO_WIN=user@host (and XISTO_WIN_KEY)"; exit 0; }
KEY=${XISTO_WIN_KEY:+-i $XISTO_WIN_KEY}
W=bench/out/windows; rm -rf "$W"; mkdir -p "$W/pkg/bin" "$W/pkg/oracle" "$W/pkg/ok"
built=0; skipped=""
for f in tests/ok/*.ts; do
    n=$(basename "$f" .ts)
    [ -f "build/tests/$n.node" ] || continue
    case $n in process-timers) skipped="$skipped $n(shell commands: printf)"; continue ;; esac
    if XISTO_NODE_COMPAT=1 build/xisto build --target=windows-x64 "$f" -o "$W/pkg/bin/$n.exe" > "$W/$n.build" 2>&1; then
        built=$((built+1)); cp "build/tests/$n.node" "$W/pkg/oracle/"
    else skipped="$skipped $n"; fi
done
echo "built for windows-x64: $built (left out:$skipped)"
cp -r tests/ok/. "$W/pkg/ok/"; rm -f "$W/pkg/ok/"*.ts
cat > "$W/pkg/run.ps1" <<'IN'
$env:TZ = "EST5EDT"
New-Item -ItemType Directory -Force C:\tmp | Out-Null  # a few tests write to /tmp, which is C:\tmp there
$pass = 0; $fail = 0
Set-Location C:\xisto\ok
Get-ChildItem C:\xisto\bin\*.exe | ForEach-Object {
  $n = $_.BaseName
  $out = "C:\xisto\out\$n.txt"
  cmd /c "`"$($_.FullName)`" > `"$out`" 2>&1 < NUL"
  $a = [System.IO.File]::ReadAllBytes($out)
  $b = [System.IO.File]::ReadAllBytes("C:\xisto\oracle\$n.node")
  if ([System.Linq.Enumerable]::SequenceEqual($a, $b)) { $pass++ } else { $fail++; Write-Output "FAIL $n" }
}
Write-Output "windows: $pass passed, $fail failed"
IN
(cd "$W" && rm -f pkg.zip && zip -qr pkg.zip pkg)
scp $KEY -o BatchMode=yes -q "$W/pkg.zip" "$XISTO_WIN:C:/xisto-pkg.zip" || exit 1
ssh $KEY -o BatchMode=yes "$XISTO_WIN" 'powershell -NoProfile -Command "Remove-Item -Recurse -Force C:\xisto,C:\xtmp -ErrorAction SilentlyContinue; Expand-Archive -Force C:\xisto-pkg.zip C:\xtmp; Move-Item C:\xtmp\pkg C:\xisto; New-Item -ItemType Directory C:\xisto\out | Out-Null; & C:\xisto\run.ps1"' | tee "$W/result.txt"
# ---- the rest of the suite that means something on Windows (XB-032): built here, run there, compared here ----
# tests/err (compile errors: compiled here for windows-x64, the message must be the same; runtime errors run
# there), stack traces, the memory check and the differential crypto tests. What stays on the compiler's host
# (fmt, xisto test, packages, the debugger, the noinline scan, size budgets, qbe) is listed at the end.
X="$W/x"; mkdir -p "$X/bin"; : > "$X/cases.txt"
xpass=0; xfail=0
xok() { xpass=$((xpass+1)); }
xbad() { echo "FAIL $1"; xfail=$((xfail+1)); }
for f in tests/err/*.ts; do
    n=$(basename "$f" .ts); want=$(sed -n '1s|^// expect: ||p' "$f")
    case "$want" in
    "runtime error"*)
        if XISTO_NODE_COMPAT=1 build/xisto build --target=windows-x64 -o "$X/bin/err-$n.exe" "$f" > /dev/null 2>&1; then echo "err-$n|err-$n.exe|" >> "$X/cases.txt"
        else xbad "err $n: should compile"; fi ;;
    *)
        if XISTO_NODE_COMPAT=1 build/xisto build --target=windows-x64 -o "$X/bin/none.exe" "$f" > "$X/err-$n.txt" 2>&1; then xbad "err $n: compiled"
        elif grep -qF -- "$want" "$X/err-$n.txt"; then xok; else xbad "err $n: expected '$want'"; fi ;;
    esac
done
for t in nested recursion uncaught catch; do
    XISTO_NODE_COMPAT=1 build/xisto build --target=windows-x64 -o "$X/bin/stack-$t.exe" tests/stack/$t.ts > /dev/null 2>&1 && echo "stack-$t|stack-$t.exe|" >> "$X/cases.txt"
done
XISTO_NODE_COMPAT=1 build/xisto build --target=windows-x64 -o "$X/bin/stack-frames.exe" tests/stack/frames.ts > /dev/null 2>&1
FRAMES="filter some sort foreach method field-ctor field clone"
for c in $FRAMES; do echo "frames-$c|stack-frames.exe|$c" >> "$X/cases.txt"; done
build/xisto build --target=windows-x64 -o "$X/bin/mem.exe" tests/mem/loop-temps.ts > /dev/null 2>&1 && echo "mem|mem.exe|" >> "$X/cases.txt"
TC=$HOME/.cache/xisto/target/x86_64-windows-gnu
for t in p256_comb p256_sign p256_montymul; do
    extra=""; [ $t = p256_sign ] && extra=tests/crypto/ecdsa_sign_orig.c
    if zig cc -target x86_64-windows-gnu -O2 -Ibuild/bearssl-inc -Ithird_party/bearssl/inc -Ithird_party/bearssl/src -o "$X/bin/crypto-$t.exe" tests/crypto/$t.c $extra "$TC/libbearssl.a" > "$X/crypto-$t.build" 2>&1; then
        echo "crypto-$t|crypto-$t.exe|20000" >> "$X/cases.txt"
    else xbad "crypto $t: build"; head -3 "$X/crypto-$t.build"; fi
done
cat > "$X/run.ps1" <<'IN'
$env:TZ = "EST5EDT"
New-Item -ItemType Directory -Force C:\xisto-x\out | Out-Null
Set-Location C:\xisto-x
Get-Content C:\xisto-x\cases.txt | ForEach-Object {
  $p = $_.Split('|'); $n = $p[0]; $exe = "C:\xisto-x\bin\" + $p[1]; $a = $p[2]
  $out = "C:\xisto-x\out\$n.txt"
  if ($n -eq "mem") { # the peak working set, sampled while it runs
    $pr = Start-Process -FilePath $exe -PassThru -NoNewWindow -RedirectStandardOutput $out
    $pk = 0
    while (-not $pr.HasExited) { try { $pk = [Math]::Max($pk, $pr.PeakWorkingSet64) } catch {} ; Start-Sleep -Milliseconds 20 }
    [System.IO.File]::WriteAllText("C:\xisto-x\out\mem-peak.txt", [string][Math]::Floor($pk / 1048576))
    return # (in ForEach-Object, return goes to the next item; continue would end the whole pipeline)
  }
  cmd /c "`"$exe`" $a > `"$out`" 2>&1 < NUL"
  [System.IO.File]::AppendAllText($out, "exit $LASTEXITCODE`n")
}
Compress-Archive -Force C:\xisto-x\out C:\xisto-xout.zip
IN
(cd "$W" && rm -f x.zip && zip -qr x.zip x)
scp $KEY -o BatchMode=yes -q "$W/x.zip" "$XISTO_WIN:C:/xisto-x.zip" || exit 1
ssh $KEY -o BatchMode=yes "$XISTO_WIN" 'powershell -NoProfile -Command "Remove-Item -Recurse -Force C:\xisto-x,C:\xtmpx,C:\xisto-xout.zip -ErrorAction SilentlyContinue; Expand-Archive -Force C:\xisto-x.zip C:\xtmpx; Move-Item C:\xtmpx\x C:\xisto-x; & C:\xisto-x\run.ps1"' > "$W/x-run.txt" 2>&1
rm -rf "$W/xout" && mkdir -p "$W/xout" && scp $KEY -o BatchMode=yes -q "$XISTO_WIN:C:/xisto-xout.zip" "$W/xout.zip" && (cd "$W/xout" && unzip -qo ../xout.zip)
O="$W/xout/out"
for f in tests/err/*.ts; do
    n=$(basename "$f" .ts); want=$(sed -n '1s|^// expect: ||p' "$f")
    case "$want" in "runtime error"*) ;; *) continue ;; esac
    w=${want#runtime error: }
    if [ -f "$O/err-$n.txt" ] && grep -qF -- "$w" "$O/err-$n.txt" && ! grep -q "^exit 0$" "$O/err-$n.txt"; then xok; else xbad "err $n (runtime): expected '$w'"; fi
done
for t in nested recursion uncaught catch; do
    if [ -f "$O/stack-$t.txt" ] && cmp -s tests/stack/$t.expected "$O/stack-$t.txt"; then xok; else xbad "stack $t"; diff tests/stack/$t.expected "$O/stack-$t.txt" 2>&1 | head -6; fi
done
: > "$W/frames.out"
for c in $FRAMES; do echo "== $c" >> "$W/frames.out"; cat "$O/frames-$c.txt" >> "$W/frames.out" 2>/dev/null; done
if cmp -s tests/stack/frames.expected "$W/frames.out"; then xok; else xbad "stack frames"; diff tests/stack/frames.expected "$W/frames.out" | head -6; fi
peak=$(cat "$O/mem-peak.txt" 2>/dev/null)
if [ "$(head -1 "$O/mem.txt" 2>/dev/null | tr -d '\r')" = "9000000" ] && [ -n "$peak" ] && [ "$peak" -lt 20 ]; then xok; else xbad "loop-temps: output $(head -1 "$O/mem.txt" 2>/dev/null), peak ${peak:-?} MB"; fi
for t in p256_comb p256_sign p256_montymul; do
    if [ -f "$O/crypto-$t.txt" ] && grep -q "^exit 0$" "$O/crypto-$t.txt"; then xok; else xbad "crypto $t"; head -3 "$O/crypto-$t.txt" 2>/dev/null; fi
done
echo "windows suite (err, stack, memory, crypto): $xpass passed, $xfail failed"
# the network: the HTTP server and client of tests/http, then HTTPS (BearSSL built for Windows) of tests/tls
mkdir -p "$W/net/tests/tls"; cp tests/tls/*.pem "$W/net/tests/tls/"
# workers: tests/http/server.ts uses channels (one event loop only), so the bench server and client
XISTO_NODE_COMPAT=1 build/xisto build --target=windows-x64 bench/http/server.xisto.ts -o "$W/net/w-server.exe" > /dev/null || exit 1
XISTO_NODE_COMPAT=1 build/xisto build --target=windows-x64 bench/http/client.ts -o "$W/net/w-client.exe" > /dev/null || exit 1
for p in server client; do
    XISTO_NODE_COMPAT=1 build/xisto build --target=windows-x64 "tests/http/$p.ts" -o "$W/net/http-$p.exe" > /dev/null || exit 1
    XISTO_NODE_COMPAT=1 build/xisto build --target=windows-x64 "tests/tls/$p.ts" -o "$W/net/tls-$p.exe" > /dev/null || exit 1
done
(cd "$W" && rm -f net.zip && zip -qr net.zip net)
scp $KEY -o BatchMode=yes -q "$W/net.zip" "$XISTO_WIN:C:/xisto-net.zip" || exit 1
ssh $KEY -o BatchMode=yes "$XISTO_WIN" 'powershell -NoProfile -Command "Remove-Item -Recurse -Force C:\xnet -ErrorAction SilentlyContinue; Expand-Archive -Force C:\xisto-net.zip C:\xtmp2; Move-Item C:\xtmp2\net C:\xnet; Remove-Item C:\xtmp2; Set-Location C:\xnet; $p = Start-Process -FilePath .\http-server.exe -PassThru -NoNewWindow -RedirectStandardOutput C:\xnet\hs.log; Start-Sleep 2; .\http-client.exe; Stop-Process $p.Id -ErrorAction SilentlyContinue; $q = Start-Process -FilePath .\tls-server.exe -PassThru -NoNewWindow -RedirectStandardOutput C:\xnet\ts.log; Start-Sleep 2; $env:NODE_EXTRA_CA_CERTS = \"tests\tls\cert.pem\"; .\tls-client.exe; Stop-Process $q.Id -ErrorAction SilentlyContinue"' > "$W/net.txt" 2>&1
# the same HTTP pair with four workers (one event loop per thread, the kqueue look-alike per thread)
ssh $KEY -o BatchMode=yes "$XISTO_WIN" 'powershell -NoProfile -Command "Set-Location C:\xnet; $env:XISTO_WORKERS = \"4\"; $p = Start-Process -FilePath .\w-server.exe -PassThru -NoNewWindow -RedirectStandardError C:\xnet\hw.err -RedirectStandardOutput C:\xnet\hw.log; Start-Sleep 2; .\w-client.exe; Stop-Process $p.Id -ErrorAction SilentlyContinue; Get-Content C:\xnet\hw.err"' > "$W/net-workers.txt" 2>&1
cat "$W/net.txt"
echo "-- with XISTO_WORKERS=4:"; cat "$W/net-workers.txt"
net_ok=true
grep -q "^calls 40000 ok 20000" "$W/net-workers.txt" && grep -q "4 workers" "$W/net-workers.txt" || { echo "windows workers: FAIL"; net_ok=false; }
grep -q "^200 POST abc123 7 xisto" "$W/net.txt" && grep -q "^200 slept 20" "$W/net.txt" || net_ok=false
grep -q '^200 {"secure":true,"name":"isa"}' "$W/net.txt" && grep -q "^200 200000" "$W/net.txt" || net_ok=false
$net_ok && echo "windows network: ok" || echo "windows network: FAIL"
echo "on the compiler's host only (not a Windows program): fmt (2), xisto test (2), packages, debugger, noinline scan, size budgets (2), qbe"
grep -q " 0 failed" "$W/result.txt" && $net_ok && [ $xfail -eq 0 ]
