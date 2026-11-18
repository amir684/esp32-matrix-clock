# Builds tools/sim/sim.cpp with the MinGW compiler that PlatformIO ships, runs it, and draws the
# frames as PNG images into tools/sim/out/. Run from the project folder:  .\tools\sim\run.ps1
$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\..\.."
$sim = "$root\tools\sim"
$bin = "$env:USERPROFILE\.platformio\packages\toolchain-gccmingw32\bin"
if (-not (Test-Path "$bin\g++.exe")) { throw "MinGW not found. Install it once with: pio pkg install -g -t platformio/toolchain-gccmingw32" }
$json = Get-ChildItem "$root\.pio\libdeps" -Recurse -Directory -Filter ArduinoJson | Select-Object -First 1
if (-not $json) { throw "Build the firmware once first (pio run) so ArduinoJson is downloaded." }
$env:PATH = "$bin;$env:PATH" # g++ needs its own DLLs on the PATH

New-Item -ItemType Directory -Force "$sim\out" | Out-Null
$src = "$root\src"
& "$bin\g++.exe" -std=gnu++14 -O1 "-I$sim\stubs" "-I$src" "-I$root\include" "-I$($json.FullName)\src" `
  "$sim\sim.cpp" "$src\ui.cpp" "$src\matrix.cpp" "$src\info.cpp" "$src\hebcal.cpp" "$src\zmanim.cpp" -o "$sim\out\sim.exe"
if ($LASTEXITCODE) { throw "compile failed" }

& "$sim\out\sim.exe" > "$sim\out\frames.txt"
$ok = $LASTEXITCODE -eq 0
Get-Content "$sim\out\frames.txt" -TotalCount 1
python "$sim\render.py" "$sim\out\frames.txt" "$sim\out\faces-32.png" 32_big,32_seconds,32_thin,32_gradient
python "$sim\render.py" "$sim\out\frames.txt" "$sim\out\faces-64.png" 64_big,64_info,64_seconds,64_rainbow
python "$sim\render.py" "$sim\out\frames.txt" "$sim\out\ticker.png" ticker_01,ticker_02,ticker_04,ticker_06
python "$sim\render.py" "$sim\out\frames.txt" "$sim\out\friday.png" friday_01,friday_05,friday_07
if (-not $ok) { throw "mapping test failed" }
