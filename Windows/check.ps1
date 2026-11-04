# Windows engine validation: shared engine lock before every Windows build.
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$root = (Resolve-Path "$PSScriptRoot/..").Path
$as = Join-Path $root 'Tools/autoswitch'
$env:TMPDIR = $env:TEMP

Write-Host '== Configure engine test tools =='
cmake -S "$root/Windows" -B "$root/Windows/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build "$root/Windows/build" --config Release --target harness sim rtsim drvlazy bench undo_test touchsim
if ($LASTEXITCODE -ne 0) { throw 'Building engine validation tools failed' }

$harness = Join-Path $as 'test/harness.exe'
$sim = Join-Path $as 'sim/sim.exe'
$rtsim = Join-Path $as 'sim/rtsim.exe'
$driver = Join-Path $as 'driver/drvlazy.exe'
$bench = Join-Path $as 'driver/bench.exe'
$undo = Join-Path $as 'driver/undo_test.exe'
$touch = Join-Path $as 'touch/touchsim.exe'
$env:VIKEY_HARNESS = $harness
$env:VIKEY_SIM = $sim

function Invoke-PythonCheck([string]$Script, [string[]]$Arguments = @()) {
    & python $Script @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Python check failed: $Script $($Arguments -join ' ')" }
}

Write-Host '== Rule cases: default Telex =='
Remove-Item Env:VIKEY_TELEX_STYLE -ErrorAction SilentlyContinue
Invoke-PythonCheck (Join-Path $as 'rules/run_rules.py')

Write-Host '== Rule cases: Simple Telex styles =='
$env:VIKEY_TELEX_STYLE = '1'
Invoke-PythonCheck (Join-Path $as 'rules/run_rules.py') @('telex_simple1.tsv')
$env:VIKEY_TELEX_STYLE = '2'
Invoke-PythonCheck (Join-Path $as 'rules/run_rules.py') @('telex_simple2.tsv')
Remove-Item Env:VIKEY_TELEX_STYLE -ErrorAction SilentlyContinue

Write-Host '== Previously reported engine bugs =='
Invoke-PythonCheck (Join-Path $as 'rules/run_reported.py')

Write-Host '== Golden snapshots, Telex syllables, and typing metrics =='
$mode = if ($args.Count -gt 0) { $args[0] } else { 'check' }
Invoke-PythonCheck (Join-Path $as 'rules/golden.py') @($mode)

function New-Units([string]$Name, [string]$Corpus) {
    $output = Join-Path $env:TMPDIR $Name
    $env:VIKEY_UNITS_OUT = $output
    $env:VIKEY_UNITS_INPUT = Join-Path $as $Corpus
    $env:VIKEY_UNITS_PREP = Join-Path $as 'sim/prep.py'
    $code = "import os,runpy,sys,io; sys.stdout=io.TextIOWrapper(open(os.environ['VIKEY_UNITS_OUT'],'wb'),encoding='utf-8',newline='\n'); sys.argv=[os.environ['VIKEY_UNITS_PREP'],os.environ['VIKEY_UNITS_INPUT']]; runpy.run_path(os.environ['VIKEY_UNITS_PREP'],run_name='__main__'); sys.stdout.flush()"
    & python -c $code
    if ($LASTEXITCODE -ne 0) { throw "Failed to prepare corpus: $Corpus" }
    return $output
}

$chatUnits = New-Units 'vikey_check_chat.units' 'sim/corpus/chat_messages.txt'
$syntheticUnits = New-Units 'vikey_check_synthetic.units' 'sim/corpus/synthetic_val.txt'

Write-Host '== Driver and simulator output parity =='
$driverOutput = Join-Path $env:TMPDIR 'vikey_driver.txt'
$simOutput = Join-Path $env:TMPDIR 'vikey_simulator.txt'
$env:DRV_OUT = $driverOutput
& $driver $chatUnits --start vi
if ($LASTEXITCODE -ne 0) { throw 'Driver simulation failed' }
& $sim $chatUnits --start vi --lazy --dump $simOutput
if ($LASTEXITCODE -ne 0) { throw 'Engine simulator failed' }
$env:VIKEY_DRIVER_OUTPUT = $driverOutput
$env:VIKEY_SIM_OUTPUT = $simOutput
$compare = @'
import os,re,sys
with open(os.environ["VIKEY_DRIVER_OUTPUT"],encoding="utf-8") as f: d=f.read().split("\n")
with open(os.environ["VIKEY_SIM_OUTPUT"],encoding="utf-8") as f: s=[line.split("\t")[0] for line in f.read().rstrip("\n").split("\n")]
D=re.sub(r"[\W\d_]+","", "".join(d)); S=re.sub(r"[\W\d_]+","", "".join(s))
print("driver == simulator:", "ok" if D==S else "FAIL", len(D), len(S))
sys.exit(0 if D==S else 1)
'@
& python -c $compare
if ($LASTEXITCODE -ne 0) { throw 'Driver output differs from simulator' }

Write-Host '== Undo, switch, and click regression tests =='
& $undo
if ($LASTEXITCODE -ne 0) { throw 'Undo regression tests failed' }

Write-Host '== Realistic typing simulation =='
$rtsimLogs = @()
foreach ($seed in @(1, 2, 3)) {
    $rtsimLogs += (& $rtsim $chatUnits --seed $seed --show 3 2>&1 | Out-String)
}
$rtsimLogs += (& $rtsim $syntheticUnits --seed 1 --show 3 2>&1 | Out-String)
$rtsimLog = Join-Path $env:TMPDIR 'vikey_rtsim.log'
[System.IO.File]::WriteAllText($rtsimLog, ($rtsimLogs -join "`n"), [System.Text.UTF8Encoding]::new($false))
$env:VIKEY_RTSIM_LOG = $rtsimLog
$env:VIKEY_RTSIM_GOLDEN = Join-Path $as 'rules/golden/rtsim.json'
$rtsimCheck = @'
import json,os,re,sys
out=open(os.environ["VIKEY_RTSIM_LOG"],encoding="utf-8").read()
runs=len(re.findall(r"^realistic typing:",out,re.M))
bad=sum(map(int,re.findall(r"(?:guard|elsewhere) (\d+)",out)))
fixed={"stuck":[sum(map(int,re.findall(r"stuck (\d+)",out))),0]}
for k,a,b in re.findall(r"  ([a-z-]+) (\d+)/(\d+)",out):
    v=fixed.setdefault(k,[0,0]); v[0]+=int(a); v[1]+=int(b)
gold=json.load(open(os.environ["VIKEY_RTSIM_GOLDEN"],encoding="utf-8"))
worse=[f"{k} {v[0]} (was {gold[k]})" for k,v in fixed.items() if k in gold and v[0]>gold[k]*1.05+3]
print(f"realistic typing: {'ok' if runs==4 and bad==0 and not worse else 'FAIL'} ({runs}/4 runs, {bad} guard/elsewhere errors; "+", ".join(f"{k} {v[0]}/{v[1]}" for k,v in fixed.items())+")")
if worse: print("Worse than baseline: "+", ".join(worse))
sys.exit(0 if runs==4 and bad==0 and not worse else 1)
'@
& python -c $rtsimCheck
if ($LASTEXITCODE -ne 0) { throw 'Realistic typing quality gate failed' }

Write-Host '== Fat-finger and auto-correct checks =='
$touchOutput = & $touch $chatUnits 0.30
if ($LASTEXITCODE -ne 0) { throw 'Fat-finger simulator failed' }
$env:TOUCH_CORRECT = '1'
$correctOutput = & $touch $chatUnits 0.30
if ($LASTEXITCODE -ne 0) { throw 'Auto-correct simulator failed' }
$cleanOutput = & $touch $chatUnits 0.0
if ($LASTEXITCODE -ne 0) { throw 'Clean typing auto-correct simulator failed' }
Remove-Item Env:TOUCH_CORRECT -ErrorAction SilentlyContinue
$env:VIKEY_TOUCH_OUTPUT = ($touchOutput -join "`n")
$env:VIKEY_CORRECT_OUTPUT = ($correctOutput -join "`n")
$env:VIKEY_CLEAN_OUTPUT = ($cleanOutput -join "`n")
$touchCheck = @'
import os,re,sys
m=re.search(r"nearest ([\d.]+)% wrong, model ([\d.]+)% wrong",os.environ["VIKEY_TOUCH_OUTPUT"])
ns=re.search(r"pick: (\d+) ns",os.environ["VIKEY_TOUCH_OUTPUT"])
if not m or not ns: raise SystemExit("Could not parse fat-finger metrics")
near,model=float(m.group(1)),float(m.group(2)); speed=float(ns.group(1))
ok=model<=15.0 and model<near*0.6 and speed<20000
print(("fat-finger: ok" if ok else "FAIL fat-finger")+f" (wrong words {near}% -> {model}%, {speed:.0f} ns per touch)")
c0=re.search(r"auto-correct: ([\d.]+)% wrong",os.environ["VIKEY_CLEAN_OUTPUT"])
c3=re.search(r"auto-correct: ([\d.]+)% wrong",os.environ["VIKEY_CORRECT_OUTPUT"])
if not c0 or not c3: raise SystemExit("Could not parse auto-correct metrics")
clean,sloppy=float(c0.group(1)),float(c3.group(1))
good=clean<=0.10 and sloppy<model*0.75
print(("auto-correct: ok" if good else "FAIL auto-correct")+f" (clean {clean}% wrong, sloppy {model}% -> {sloppy}%)")
sys.exit(0 if ok and good else 1)
'@
& python -c $touchCheck
if ($LASTEXITCODE -ne 0) { throw 'Fat-finger or auto-correct quality gate failed' }

Write-Host '== Performance budget =='
& $bench $chatUnits 1000 20000
if ($LASTEXITCODE -ne 0) { throw 'Engine performance budget failed' }
Write-Host 'ALL ENGINE CHECKS PASSED'
