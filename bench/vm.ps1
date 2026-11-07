# xisto, Node and Bun on this Windows machine (the counterpart of bench/vm.py): every program of bench/programs,
# bench/rt, bench/lang and bench/std built by xisto.exe here, run interleaved; median wall time, CPU time
# (user + kernel) and peak working set.
# Usage (PowerShell, from a folder with bench\ and the runtimes):
#   .\vm.ps1 -Xisto C:\xn\build\xisto.exe -Node C:\rt\node-v26.12.0-win-x64\node.exe -Bun C:\rt\bun-windows-x64\bun.exe -Runs 3
param([string]$Xisto, [string]$Node, [string]$Bun, [int]$Runs = 3)
$env:XISTO_NODE_COMPAT = "1"
if (-not $env:XISTO_ZIG -and (Test-Path C:\zig\zig.exe)) { $env:XISTO_ZIG = "C:/zig/zig.exe" }
$groups = @(@("bench/programs", @("fib", "mandelbrot", "nbody", "objects", "sieve")),
            @("bench/rt", @("binarytrees", "cycles")),
            @("bench/lang", @("accessors", "closures", "generators", "generics", "shapes", "unions")),
            @("bench/std", @("bytes", "date", "promise", "redos", "regex")))
New-Item -ItemType Directory -Force bench\out\vm | Out-Null
function Run-One([string]$exe, [string[]]$argv) {
  $out = [System.IO.Path]::GetTempFileName()
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  if ($argv.Count -gt 0) { $p = Start-Process -FilePath $exe -ArgumentList $argv -PassThru -NoNewWindow -RedirectStandardOutput $out }
  else { $p = Start-Process -FilePath $exe -PassThru -NoNewWindow -RedirectStandardOutput $out } # (an empty list is refused)
  $null = $p.Handle # (read now, or ExitCode stays empty after the exit)
  $pk = 0
  while (-not $p.HasExited) { try { $p.Refresh(); $pk = [Math]::Max($pk, $p.PeakWorkingSet64) } catch {} ; Start-Sleep -Milliseconds 5 } # (Refresh: the properties are cached)
  $p.WaitForExit()
  $sw.Stop()
  $text = [System.IO.File]::ReadAllText($out)
  Remove-Item $out
  if ($p.ExitCode -ne 0) { return $null }
  return @($sw.Elapsed.TotalSeconds, ($pk / 1MB), $text, $p.TotalProcessorTime.TotalSeconds)
}
function Cell($rs) {
  $ok = @($rs | Where-Object { $_ -ne $null })
  if ($ok.Count -eq 0) { return "          failed           " }
  $t = @($ok | ForEach-Object { $_[0] } | Sort-Object)
  $m = ($ok | ForEach-Object { $_[1] } | Measure-Object -Maximum).Maximum
  $c = @($ok | ForEach-Object { $_[3] } | Sort-Object)
  $mid = [int][Math]::Floor($t.Count / 2)
  return ("{0,8:N3}s {1,7:N2}cpu {2,6:N1}MB" -f $t[$mid], $c[$mid], $m)
}
"# Windows $env:PROCESSOR_ARCHITECTURE, $env:NUMBER_OF_PROCESSORS cpu; node $(& $Node --version), bun $(& $Bun --version); median of $Runs (peak working set sampled)"
"{0,-14}{1,27}{2,27}{3,27}" -f "program", "xisto", "node", "bun"
foreach ($g in $groups) {
  foreach ($n in $g[1]) {
    $src = "$($g[0])/$n.ts"
    $exe = "bench\out\vm\$n.exe"
    cmd /c "`"$Xisto`" build -o `"$exe`" $src > NUL 2>&1"
    if ($LASTEXITCODE -ne 0) { "{0,-14}  xisto build failed" -f $n; continue }
    $cmds = [ordered]@{ xisto = @((Resolve-Path $exe).Path, @()); node = @($Node, @($src)); bun = @($Bun, @($src)) }
    $res = @{ xisto = @(); node = @(); bun = @() }
    foreach ($k in $cmds.Keys) { Run-One $cmds[$k][0] $cmds[$k][1] | Out-Null } # warm-up
    for ($i = 0; $i -lt $Runs; $i++) { foreach ($k in $cmds.Keys) { $res[$k] += , (Run-One $cmds[$k][0] $cmds[$k][1]) } }
    "{0,-14}{1,27}{2,27}{3,27}" -f $n, (Cell $res.xisto), (Cell $res.node), (Cell $res.bun)
  }
}
