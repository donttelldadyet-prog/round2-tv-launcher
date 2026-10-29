<#
  round2tv.ps1 - reversible ADB toolkit for the Round2 shop TVs (Windows)
  Tested: TCL Google TV (Android 11), Sony KD-65X8500C (Android TV 8.0)
  Each TV gets its own folder (backups, screenshots, undo.ps1) under round2-tv\tvs\<ip>.
  'connect <ip>' picks which TV the other commands work on. 'connect usb' picks the TV on the USB cable.

  Run from the folder holding this file:
    powershell -ExecutionPolicy Bypass -File .\round2tv.ps1 <command> [args]
  Run with no command for help. Every change is recorded in undo.ps1.
#>
param(
  [Parameter(Position = 0)][string]$Command = 'help',
  [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Rest
)
$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

$Base = if ($env:USERPROFILE) { $env:USERPROFILE } else { $HOME }
$Dir = if ($env:R2_DIR) { $env:R2_DIR } else { Join-Path $Base 'round2-tv' }
foreach ($d in 'logs', 'apk', 'tvs') { New-Item -ItemType Directory -Force -Path (Join-Path $Dir $d) | Out-Null }
$Log = Join-Path (Join-Path $Dir 'logs') 'monitor.log'
$TargetFile = Join-Path $Dir 'tv_target'
$YT = 'com.google.android.youtube.tv'
$TouchedKeys = @('window_animation_scale', 'transition_animation_scale', 'animator_duration_scale', 'private_dns_mode', 'private_dns_specifier')

$script:TV = if ($env:TV) { $env:TV } elseif (Test-Path $TargetFile) { (Get-Content $TargetFile -Raw).Trim() } else { '192.168.88.14:5555' }

# One folder per TV, so each TV keeps its own backup and undo list. Until the TV answers, the folder is
# named after its address; once connected it is named after the TV's serial number (Resolve-TvDir), so the
# backups follow the TV when the router gives it a new address.
function Set-TvDir([string]$path) {
  $script:TvDir = $path
  foreach ($d in 'backup', 'shots') { New-Item -ItemType Directory -Force -Path (Join-Path $script:TvDir $d) | Out-Null }
  $script:Undo = Join-Path $script:TvDir 'undo.ps1'
  $script:HomeFile = Join-Path $script:TvDir 'original_home.txt'
}
function Set-Tv([string]$tv) {
  $script:TV = $tv
  $script:TvResolved = $false
  Set-TvDir (Join-Path (Join-Path $Dir 'tvs') ($tv -replace '[^A-Za-z0-9.-]', '_'))
}
# Older toolkit versions kept the first TV's (the TCL's) files at the top level: move them into its own folder once
if (Test-Path (Join-Path $Dir 'undo.ps1')) {
  $old = $script:TV
  $oldDir = Join-Path (Join-Path $Dir 'tvs') ($old -replace '[^A-Za-z0-9.-]', '_')
  if (-not (Test-Path (Join-Path $oldDir 'undo.ps1'))) {
    New-Item -ItemType Directory -Force -Path $oldDir | Out-Null
    foreach ($n in 'undo.ps1', 'original_home.txt', 'backup', 'shots', 'enabled-packages.txt', 'cleanup-draft.txt') {
      $src = Join-Path $Dir $n
      if (Test-Path $src) { Move-Item $src (Join-Path $oldDir $n) -Force }
    }
    $u = Join-Path $oldDir 'undo.ps1'
    (Get-Content $u) | ForEach-Object { if ($_ -like '$TV = *') { "`$TV = '$old'" } else { $_ } } | Set-Content $u -Encoding UTF8
    Write-Host ">> Moved the files for $old into $oldDir" -ForegroundColor Yellow
  }
}
Set-Tv $script:TV

# Never disabled by 'cleanup', whatever the list says (the launcher has its own command)
$Protect = 'launcher|katniss|\.gms|gsf|vending|inputmethod|keyboard|leanbackime|\.ime|tv\.provider|providers|tvinput|livetv|tuner|dtv|tvsettings|\.settings|systemui|setupwizard|packageinstaller|permissioncontroller|webview|youtube|bluetooth|remote|audio|hdmi|cec'
# Reviewed exceptions: these match a protected word (e.g. Sony's com.sony.dtv.*) but are promotions/extras checked by hand
$Reviewed = '^com\.sony\.dtv\.(sonyselect|sonyselect\.overlay|discovery|indiametarecommendation|touchpad\.tutorial|shopsettings|imanual|smarthelp|customersupport|osat\.album|osat\.music|osat\.video)$'
# Keyword hints for the draft proposal (Claude reviews the real list)
$Candidate = 'adservice|adsdk|advert|promo|recommend|feedback|demo|retail|storemode|shopmode|analytics|telemetry|survey|marketing|sonyselect|showcase|discover|(^|[._])ads?([._]|$)'
function Is-Protected([string]$p) { return ($p -match $Protect) -and ($p -notmatch $Reviewed) }

# ---------------- helpers ----------------
function Ts { Get-Date -Format 'yyyy-MM-dd HH:mm:ss' }
function Log([string]$m) { Add-Content -Path $Log -Value "[$(Ts)] $m" -Encoding UTF8 }
function Say([string]$m) { Write-Host ">> $m" -ForegroundColor Yellow; Log "# $m" }
function Die([string]$m) { Write-Host "XX $m" -ForegroundColor Red; Log "ERROR $m"; exit 1 }

function Find-Adb {
  $c = Get-Command adb -ErrorAction SilentlyContinue
  if ($c) { return $c.Source }
  foreach ($n in 'adb.exe', 'adb') {
    $p = Join-Path (Join-Path $Dir 'platform-tools') $n
    if (Test-Path $p) { return $p }
  }
  return $null
}
$script:Adb = Find-Adb

function Get-TvState {
  if (-not $script:Adb) { return '' }
  $s = & $script:Adb -s $script:TV get-state 2>$null
  return "$s".Trim()
}
function Need-Adb { if (-not $script:Adb) { Die 'adb is missing - run: .\round2tv.ps1 setup' } }
function Need-Tv { Need-Adb; if ((Get-TvState) -ne 'device') { Die "Not connected to $($script:TV) - run: .\round2tv.ps1 connect" }; Resolve-TvDir }
function Need-Backup { if (-not (Test-Path $Undo)) { Die 'No backup yet - run: .\round2tv.ps1 backup' } }
function Add-Undo([string]$c) { Add-Content -Path $Undo -Value "#STEP $c" -Encoding UTF8 }

# Live monitor: prints each adb command with a timestamp, plus the TV's
# logcat (warnings and errors) captured only while that command runs.
function Run-Adb {
  param([string[]]$A, [switch]$NoLogcat, [switch]$NoSerial)
  $line = 'adb ' + ($A -join ' ')
  Write-Host "[$(Ts)] `$ $line" -ForegroundColor Cyan
  Log "`$ $line"
  $lc = $null
  $lcFile = Join-Path (Join-Path $Dir 'logs') '_logcat.tmp'
  if (-not $NoLogcat -and (Get-TvState) -eq 'device') {
    Remove-Item $lcFile, "$lcFile.err" -ErrorAction SilentlyContinue
    $lc = Start-Process -FilePath $script:Adb -ArgumentList @('-s', $script:TV, 'logcat', '-T', '1', '-v', 'time', 'RHWC2:S', '*:W') `
      -NoNewWindow -PassThru -RedirectStandardOutput $lcFile -RedirectStandardError "$lcFile.err"
  }
  if ($NoSerial) { $out = & $script:Adb @A 2>&1 | ForEach-Object { "$_".TrimEnd("`r") } }
  else { $out = & $script:Adb -s $script:TV @A 2>&1 | ForEach-Object { "$_".TrimEnd("`r") } }
  $script:Rc = $LASTEXITCODE
  if ($lc) {
    Start-Sleep -Milliseconds 500
    try { $lc.Kill() } catch {}
    try { $lc.WaitForExit(3000) | Out-Null } catch {}
    if (Test-Path $lcFile) {
      Get-Content $lcFile | Where-Object { $_ -and $_ -notmatch '^-{5}' } | ForEach-Object {
        Write-Host "   | $_" -ForegroundColor DarkGray; Log "   | $_"
      }
    }
  }
  if ($script:Rc -ne 0) { Write-Host "   (exit $($script:Rc))" -ForegroundColor Red }
  return $out
}
function Sh([string]$c) { $o = Run-Adb -A @('shell', $c); foreach ($l in $o) { if ($l) { Write-Host "   $l" } } }
function ShTo([string]$c, [string]$file) { Run-Adb -A @('shell', $c) | Set-Content -Path $file -Encoding UTF8 }
function Q([string]$c) {   # quiet read-only check
  Log "  (check) $c"
  $o = & $script:Adb -s $script:TV shell $c 2>$null
  return ((@($o) | ForEach-Object { "$_".TrimEnd("`r") }) -join "`n").Trim()
}

# The TV's own folder, named after its model and serial number. Folders from before (named after an address)
# that belong to this TV are found by the serial number in their backups and merged in: the oldest backup's
# undo list stays the main one (it holds the TV's original state), newer ones are kept under backup\.
function Resolve-TvDir {
  if ($script:TvResolved) { return }
  $script:TvResolved = $true
  $serial = Q 'getprop ro.serialno'
  if (-not $serial -or $serial -eq 'unknown') { $serial = Q 'getprop ro.boot.serialno' }
  if (-not $serial -or $serial -eq 'unknown') { $serial = Q 'settings get secure android_id' }
  if (-not $serial -or $serial -eq 'unknown' -or $serial -eq 'null') { return }   # keep the address folder
  $serial = $serial.Trim()
  $tvs = Join-Path $Dir 'tvs'
  $want = Join-Path $tvs ((((Q 'getprop ro.product.model') + '-' + $serial)) -replace '[^A-Za-z0-9.-]', '_')
  $mine = @()
  foreach ($d in @(Get-ChildItem $tvs -Directory -ErrorAction SilentlyContinue)) {
    if ($d.FullName -eq $want) { continue }
    $m = Join-Path $d.FullName 'serial.txt'
    $hit = (Test-Path $m) -and ((Get-Content $m -Raw).Trim() -eq $serial)
    if (-not $hit) {
      foreach ($g in @(Get-ChildItem (Join-Path $d.FullName 'backup') -Recurse -Filter 'getprop.txt' -ErrorAction SilentlyContinue)) {
        if (Select-String -Path $g.FullName -SimpleMatch "[ro.serialno]: [$serial]" -Quiet) { $hit = $true; break }
      }
    }
    if ($hit) { $mine += $d }
  }
  if ($mine.Count) {
    # oldest backup first: that folder becomes this TV's folder (if it has none yet)
    $first = { param($f) $b = @(Get-ChildItem (Join-Path $f.FullName 'backup') -Directory -ErrorAction SilentlyContinue | Sort-Object Name); if ($b.Count) { $b[0].Name } else { '99999999' } }
    $mine = @($mine | Sort-Object { & $first $_ })
    if (-not (Test-Path (Join-Path $want 'undo.ps1')) -and (Test-Path (Join-Path $mine[0].FullName 'undo.ps1'))) {
      if (Test-Path $want) { Remove-Item $want -Recurse -Force }
      Move-Item $mine[0].FullName $want
      $mine = @($mine | Select-Object -Skip 1)
    }
    foreach ($d in $mine) {   # the rest: their backups, screenshots and undo lists go under this TV's folder
      foreach ($sub in 'backup', 'shots') {
        New-Item -ItemType Directory -Force -Path (Join-Path $want $sub) | Out-Null
        foreach ($x in @(Get-ChildItem (Join-Path $d.FullName $sub) -ErrorAction SilentlyContinue)) {
          $to = Join-Path (Join-Path $want $sub) $x.Name
          if (-not (Test-Path $to)) { Move-Item $x.FullName $to }
        }
      }
      $u = Join-Path $d.FullName 'undo.ps1'
      if (Test-Path $u) { Move-Item $u (Join-Path (Join-Path $want 'backup') ("undo-from-" + $d.Name + ".ps1")) -Force }
      Remove-Item $d.FullName -Recurse -Force -ErrorAction SilentlyContinue
    }
    Say "This TV's earlier backups are now together in $want"
  }
  Set-TvDir $want
  Set-Content (Join-Path $want 'serial.txt') $serial -Encoding ASCII
  Set-Content (Join-Path $want 'address.txt') $script:TV -Encoding ASCII
  # the undo list talks to the TV by address: keep it pointing at the current one
  if (Test-Path $script:Undo) { (Get-Content $script:Undo) | ForEach-Object { if ($_ -like '$TV = *') { "`$TV = '$($script:TV)'" } else { $_ } } | Set-Content $script:Undo -Encoding UTF8 }
}
function Key([string]$k, [double]$wait = 0.8) { Sh "input keyevent $k"; Start-Sleep -Milliseconds ([int]($wait * 1000)) }

# Screen check: screenshot + UI dump, prints the focused window and visible text
$script:Focus = ''; $script:Texts = ''
function Look {
  $n = Join-Path (Join-Path $script:TvDir 'shots') (Get-Date -Format 'yyyyMMdd-HHmmss')
  & $script:Adb -s $script:TV shell 'rm -f /sdcard/r2ui.xml; screencap -p /sdcard/r2shot.png; uiautomator dump /sdcard/r2ui.xml' 2>$null | Out-Null
  & $script:Adb -s $script:TV pull /sdcard/r2shot.png "$n.png" 2>$null | Out-Null
  & $script:Adb -s $script:TV pull /sdcard/r2ui.xml "$n.xml" 2>$null | Out-Null
  $f = (Q 'dumpsys window') -split "`n" | Where-Object { $_ -match 'mCurrentFocus' } | Select-Object -First 1
  $script:Focus = "$f" -replace '.*mCurrentFocus=', ''
  $script:Texts = ''
  if (Test-Path "$n.xml") {
    $x = Get-Content "$n.xml" -Raw
    $script:Texts = (@([regex]::Matches($x, '(?:text|content-desc)="([^"]+)"')) | Select-Object -First 25 | ForEach-Object { $_.Groups[1].Value }) -join ' | '
  }
  Say "Screen focus: $($script:Focus)"
  if ($script:Texts) { Say "Screen text: $($script:Texts)" } else { Say "Screen text: (none readable - look at the TV)" }
  Say "Screenshot: $n.png"
}
function Press-Ok { Look; Say 'Pressing OK on the remote'; Key 'KEYCODE_DPAD_CENTER' 2 }

function Get-Home {
  $r = Q 'cmd package resolve-activity --brief -a android.intent.action.MAIN -c android.intent.category.HOME'
  $m = @([regex]::Matches($r, '[A-Za-z0-9_.]+/[A-Za-z0-9_.$]+'))
  if ($m.Count -eq 0) { return '' }
  $c = $m[$m.Count - 1].Value
  if ($c -match 'ResolverActivity') { return '' }
  return $c
}
function Get-Packages([string]$flag) {
  return @((Q "pm list packages $flag") -split "`n" | ForEach-Object { $_ -replace '^package:', '' } | Where-Object { $_ } | Sort-Object)
}

# ---------------- commands ----------------
function Cmd-Setup {
  if ($script:Adb) { Say "adb already installed: $($script:Adb)"; Write-Host ((& $script:Adb version) | Select-Object -First 1); return }
  $url = 'https://dl.google.com/android/repository/platform-tools-latest-windows.zip'
  $zip = Join-Path $Dir 'platform-tools.zip'
  Say "Downloading Google's official Android platform-tools: $url"
  Log "`$ download $url"
  try { Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing } catch { Die "Download failed: $_" }
  Expand-Archive -Path $zip -DestinationPath $Dir -Force
  $script:Adb = Find-Adb
  if (-not $script:Adb) { Die 'adb not found after unzipping' }
  Say ("Installed: " + ((& $script:Adb version) | Select-Object -First 1))
}

# Room name -> TV address, as saved in the Theme manager (Rooms > Advanced > TV address)
$RoomNames = @('vale', 'kaeden', 'scarlett', 'bella', 'kai', 'atlas', 'maximillion')
function Resolve-Room([string]$name) {
  $cache = Join-Path $Dir 'tv-addresses.json'
  try {
    $j = Invoke-RestMethod -UseBasicParsing -Uri 'https://raw.githubusercontent.com/donttelldadyet-prog/round2-tv-launcher/main/themes/themes.json'
    if ($j.tvs) { $j.tvs | ConvertTo-Json | Set-Content -Path $cache -Encoding UTF8 }
  } catch { Say 'Offline: using the last saved TV addresses' }
  if (-not (Test-Path $cache)) { return '' }
  $t = Get-Content $cache -Raw | ConvertFrom-Json
  return "$($t.$name)".Trim()
}

function Cmd-Connect([string]$ip) {
  Need-Adb
  if ($ip -and $RoomNames -contains $ip.ToLower()) {
    $room = $ip.ToLower()
    $ip = Resolve-Room $room
    if (-not $ip) { Die "No TV address saved for $room. Add it in the Theme manager: Rooms > $room > Advanced > TV address (the TV shows it under Options > Staff mode > About this TV)." }
    Say "$room's TV is at $ip"
  }
  $usb = $ip -eq 'usb'
  if ($usb) {
    # a TV on a USB cable (Atlas, the Prism+, when its network debugging is closed): the other commands use its serial number
    & $script:Adb start-server 2>$null | Out-Null
    $sn = ((& $script:Adb -d get-serialno 2>$null) -join '').Trim()
    if (-not $sn -or $sn -match 'unknown|error') {
      Die "No TV on the USB cable. Check: the cable carries data (not charge-only), it's in the TV's other USB port, and USB debugging is on. Then run: .\round2tv.ps1 connect usb"
    }
    Say "TV on the USB cable: $sn"
    Set-Tv $sn
    $out = 'connected to usb'
  } else {
  if ($ip) { Set-Tv $(if ($ip -match ':') { $ip } else { "$($ip):5555" }) }
  & $script:Adb start-server 2>$null | Out-Null
  $out = (Run-Adb -A @('connect', $script:TV) -NoLogcat -NoSerial) -join "`n"
  Write-Host $out
  }
  if ($out -match 'connected to') {
    # ok, continue to authorisation check
  }
  elseif ($out -match 'failed to authenticate|unauthori') {
    # The TV answered and is showing its 'Allow USB debugging?' prompt (older Android TVs say this)
    Say "The TV is asking permission. On the TV: tick 'Always allow from this computer', then press OK on 'Allow'."
    for ($i = 1; $i -le 30; $i++) {
      Start-Sleep -Seconds 2
      if ((Get-TvState) -eq 'device') { break }
      & $script:Adb disconnect $script:TV 2>$null | Out-Null
      $o = (& $script:Adb connect $script:TV 2>&1) -join ' '
      if ($o -match 'connected to' -and (Get-TvState) -eq 'device') { break }
    }
  }
  elseif ($out -match '10060|timed out|did not properly respond') {
    Die ("The TV didn't answer at $($script:TV). Check: TV and PC on the SAME Wi-Fi (not a guest network); " +
      "shared or guest Wi-Fi often blocks devices from seeing each other ('client isolation'); the IP in TV Settings > Network still matches.")
  }
  else {
    Say 'Plain connect was refused.'
    Say "Older TVs (Android 8-10, e.g. the 2015 Sony): on the TV turn on Developer options > USB debugging AND Network debugging, then run connect again."
    if ((Read-Host "  Does this TV have 'Wireless debugging' (Android 11 or newer)? [y/N]") -notmatch '^[Yy]') { Die 'Turn on network debugging on the TV and run connect again' }
    Say 'Switching to Wireless debugging pairing.'
    Say "On the TV: Developer options > Wireless debugging > ON > 'Pair device with pairing code'. Leave that screen open."
    $pp = Read-Host '  Pairing IP:port shown on the TV (e.g. 192.168.88.14:41234)'
    $code = Read-Host '  6-digit pairing code shown on the TV'
    $o = (Run-Adb -A @('pair', $pp, $code) -NoLogcat -NoSerial) -join "`n"; Write-Host $o
    if ($o -notmatch 'Successfully paired') { Die 'Pairing failed' }
    Set-Tv ((Read-Host "  IP:port under 'IP address & Port' on the Wireless debugging screen").Trim())
    $o = (Run-Adb -A @('connect', $script:TV) -NoLogcat -NoSerial) -join "`n"; Write-Host $o
    if ($o -notmatch 'connected to') { Die 'Connect after pairing failed' }
  }
  $st = ''
  for ($i = 1; $i -le 40; $i++) {
    $st = Get-TvState
    if ($st -eq 'device') { break }
    if ($i -eq 1) { Say "On the TV: tick 'Always allow from this computer', then press OK on 'Allow'." }
    Start-Sleep -Seconds 1
  }
  if ($st -ne 'device') { Die "The TV didn't authorise this computer ($st)" }
  Set-Content -Path $TargetFile -Value $script:TV -Encoding ASCII
  Resolve-TvDir
  Say "This TV's backups and undo list: $($script:TvDir)"
  Say ("Connected to $($script:TV) - " + (Q 'getprop ro.product.model') + ', Android ' + (Q 'getprop ro.build.version.release') + ', CPU ' + (Q 'getprop ro.product.cpu.abi'))
}

function Cmd-Backup {
  Need-Tv
  $b = Join-Path (Join-Path $script:TvDir 'backup') (Get-Date -Format 'yyyyMMdd-HHmmss')
  New-Item -ItemType Directory -Force -Path $b | Out-Null
  ShTo 'getprop' (Join-Path $b 'getprop.txt')
  ShTo 'pm list packages -f' (Join-Path $b 'packages-all.txt')
  ShTo 'pm list packages -e' (Join-Path $b 'packages-enabled.txt')
  ShTo 'pm list packages -d' (Join-Path $b 'packages-disabled.txt')
  ShTo 'pm list packages -s' (Join-Path $b 'packages-system.txt')
  ShTo 'pm list packages -3' (Join-Path $b 'packages-thirdparty.txt')
  foreach ($ns in 'global', 'secure', 'system') { ShTo "settings list $ns" (Join-Path $b "settings-$ns.txt") }
  $touched = @()
  foreach ($k in $TouchedKeys) { $touched += "global $k=" + (Q "settings get global $k") }
  $homeAct = Get-Home
  $touched += "home=$homeAct"
  $touched | Set-Content -Path (Join-Path $b 'touched-settings.txt') -Encoding UTF8
  if (-not (Test-Path $Undo)) {   # first backup = the original state we restore to
    if (-not (Test-Path $HomeFile)) { Set-Content -Path $HomeFile -Value $homeAct -Encoding ASCII }
    $h = @(
      "# Round2 TV undo - returns the TV to its state of $(Ts)."
      "# Run: powershell -ExecutionPolicy Bypass -File `"$Undo`""
      "# Steps are recorded below as #STEP lines and replayed newest-first."
      "`$Adb = if (`$env:ADB) { `$env:ADB } else { '$($script:Adb)' }"
      "`$TV = '$($script:TV)'"
      "`$steps = @(Get-Content `$PSCommandPath | Where-Object { `$_ -like '#STEP *' } | ForEach-Object { `$_.Substring(6) })"
      "[array]::Reverse(`$steps)"
      "foreach (`$c in `$steps) { Write-Host `"+ adb shell `$c`" -ForegroundColor Cyan; & `$Adb -s `$TV shell `$c }"
      "Write-Host 'Undo finished.' -ForegroundColor Green"
    )
    $h | Set-Content -Path $Undo -Encoding UTF8
    foreach ($k in $TouchedKeys) {
      $v = Q "settings get global $k"
      if (-not $v -or $v -eq 'null') { Add-Undo "settings delete global $k" } else { Add-Undo "settings put global $k $v" }
    }
  }
  Copy-Item $Undo (Join-Path $b 'undo.ps1')
  Get-Packages '-e' | Set-Content -Path (Join-Path $script:TvDir 'enabled-packages.txt') -Encoding UTF8
  Say "Backup saved: $b"
  Say "Current home screen app: $homeAct"
  Say "Package list for review: $(Join-Path $script:TvDir 'enabled-packages.txt')   Undo: $Undo"
}

function Cmd-Propose {
  Need-Tv
  $f = Join-Path $script:TvDir 'cleanup-draft.txt'
  $enabled = Get-Packages '-e'
  $enabled | Set-Content -Path (Join-Path $script:TvDir 'enabled-packages.txt') -Encoding UTF8
  $hits = @($enabled | Where-Object { $_ -match $Candidate })
  $lines = @('# Round2 TV cleanup - DRAFT from keyword matching. Nothing is applied until you run cleanup.',
    '# One package per line. Lines starting with # are ignored. Protected packages are refused anyway.')
  $lines += @($hits | Where-Object { -not (Is-Protected $_) })
  $lines += '', '# --- matched a keyword but PROTECTED (never touched) ---'
  $lines += @($hits | Where-Object { Is-Protected $_ } | ForEach-Object { "# $_" })
  $lines | Set-Content -Path $f -Encoding UTF8
  $lines | ForEach-Object { Write-Host $_ }
  Say "Draft: $f - send Claude this and enabled-packages.txt (same folder) for the reviewed proposal."
}

function Cmd-Cleanup([string]$file) {
  Need-Tv; Need-Backup
  if (-not $file) { $file = Join-Path $script:TvDir 'cleanup.txt' }
  if (-not (Test-Path $file)) { Die "No approved list at $file" }
  $enabled = Get-Packages '-e'
  foreach ($raw in Get-Content $file) {
    $p = ($raw -replace '#.*$', '').Trim().Split(' ')[0]
    if (-not $p) { continue }
    if (Is-Protected $p) { Say "SKIP protected: $p"; continue }
    if ($enabled -notcontains $p) { Say "SKIP not installed / already disabled: $p"; continue }
    $o = Run-Adb -A @('shell', "pm disable-user --user 0 $p")
    $o | ForEach-Object { Write-Host "   $_" }
    if (($o -join ' ') -match 'disabled') { Add-Undo "pm enable $p" }
  }
  Say 'Cleanup done. Disabled apps stay installed; undo re-enables them.'
}

function Cmd-Tweaks {
  Need-Tv; Need-Backup
  foreach ($k in 'window_animation_scale', 'transition_animation_scale', 'animator_duration_scale') { Sh "settings put global $k 0.5" }
  $sdk = [int]("0" + (Q 'getprop ro.build.version.sdk'))
  if ($sdk -ge 28) {
    Sh 'settings put global private_dns_mode hostname'
    Sh 'settings put global private_dns_specifier dns.adguard-dns.com'
  } else {
    Say "Private DNS needs Android 9 or newer; this TV is Android $(Q 'getprop ro.build.version.release'), so DNS is left as it is (ad blocking can be set on the shop router instead)."
  }
  foreach ($k in $TouchedKeys) { Say ("now $k = " + (Q "settings get global $k")) }
  Say "Check TV Settings > Network still says 'Connected'. If not: .\round2tv.ps1 undo"
}

function Cmd-Install([string]$repo) {
  Need-Tv; Need-Backup
  if (-not $repo) { $repo = 'truefedex/tv-bro' }
  if ($repo -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { Die 'Give a GitHub owner/repo, e.g. truefedex/tv-bro' }
  $abi = Q 'getprop ro.product.cpu.abi'
  Log "`$ GET https://api.github.com/repos/$repo/releases/latest"
  try { $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" -UseBasicParsing }
  catch {
    # some projects only publish beta (pre-release) versions; take the newest one
    # Windows PowerShell returns the list as one object; Select-Object unrolls it so only the newest release is used
    try { $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases?per_page=5" -UseBasicParsing | ForEach-Object { $_ } | Select-Object -First 1 } catch { $rel = $null }
    if (-not $rel) { Die "No official GitHub release found for $repo" }
    Say "Using pre-release $($rel.tag_name) (this project has no stable release yet)"
  }
  $assets = @($rel.assets | Where-Object { $_.name -like '*.apk' -and $_.browser_download_url -like "https://github.com/$repo/releases/download/*" })
  if ($assets.Count -eq 0) { Die "Release $($rel.tag_name) of $repo has no APK" }
  Say "APKs in $repo $($rel.tag_name) (TV CPU: $abi):"
  for ($i = 0; $i -lt $assets.Count; $i++) { Write-Host ("  {0}) {1}" -f ($i + 1), $assets[$i].name) }
  $n = Read-Host '  Pick a number [1]'
  if (-not $n) { $n = 1 }
  $a = $assets[[int]$n - 1]
  if (-not $a) { Die 'Invalid choice' }
  $apk = Join-Path (Join-Path $Dir 'apk') $a.name
  Log "`$ download $($a.browser_download_url)"
  try { Invoke-WebRequest -Uri $a.browser_download_url -OutFile $apk -UseBasicParsing } catch { Die "Download failed: $_" }
  Say ('SHA-256: ' + (Get-FileHash -Algorithm SHA256 -Path $apk).Hash)
  $before = Get-Packages '-3'
  $o = Run-Adb -A @('install', '-r', $apk)
  $o | ForEach-Object { Write-Host "   $_" }
  if (($o -join ' ') -notmatch 'Success') { Die 'Install failed' }
  $new = @(Get-Packages '-3' | Where-Object { $before -notcontains $_ })
  if ($new.Count -gt 0) {
    foreach ($p in $new) { Add-Undo "pm uninstall $p" }
    Say "Installed $($new -join ', ') (undo will uninstall it)"
    return $new[0]
  }
  Say 'Updated an app that was already installed (no undo line added)'
  return ''
}

function Cmd-Launcher([string]$repo) {
  Need-Tv; Need-Backup
  if (-not $repo) { $repo = 'donttelldadyet-prog/round2-tv-launcher' }
  $orig = Get-Home
  $guardWasOn = (Q 'settings get secure enabled_accessibility_services') -match 'HomeGuard'
  $pkg = Cmd-Install $repo
  if (-not $pkg -and $repo -like '*round2-tv-launcher') { $pkg = 'sg.round2.tvhome' }
  if (-not $pkg) { $pkg = (Read-Host '  Package name of the launcher (e.g. sg.round2.tvhome)').Trim() }
  $acts = Q 'cmd package query-activities --brief -a android.intent.action.MAIN -c android.intent.category.HOME'
  $comp = @([regex]::Matches($acts, '[A-Za-z0-9_.]+/[A-Za-z0-9_.$]+') | ForEach-Object { $_.Value } | Where-Object { $_ -like "$pkg/*" }) | Select-Object -First 1
  if (-not $comp) { Die "$pkg doesn't register as a home-screen app" }
  # 'Update now' on the TV installs new versions itself: allow Round2 Home to install apps (undo sets it back)
  $ia = Q "appops get $pkg REQUEST_INSTALL_PACKAGES"
  if ($ia -notmatch 'allow') {
    Sh "appops set $pkg REQUEST_INSTALL_PACKAGES allow"
    Add-Undo "appops set $pkg REQUEST_INSTALL_PACKAGES default"
  }
  Sh "cmd package set-home-activity --user 0 $comp"
  if ($orig) { Add-Undo "cmd package set-home-activity --user 0 $orig" }
  Say 'Pressing Home to test the new home screen'
  Key 'KEYCODE_HOME' 3
  Look
  if ($guardWasOn) { Cmd-HomeGuard; return }
  if ($script:Focus -match [regex]::Escape($pkg)) { Say "Done: the Home button now opens $pkg" }
  else { Say "This TV keeps its own home screen ($orig). Next step: .\round2tv.ps1 homeguard (reversible with undo)." }
}

function Cmd-HomeGuard {
  Need-Tv; Need-Backup
  $svc = 'sg.round2.tvhome/sg.round2.tvhome.HomeGuard'
  if ((Get-Packages '') -notcontains 'sg.round2.tvhome') { Die 'Install Round2 Home first: .\round2tv.ps1 launcher' }
  # TCL blocks background helpers unless the app has TCL's auto-start permission
  $ops = Q 'appops get sg.round2.tvhome APP_AUTO_START'
  if ($ops -match 'APP_AUTO_START: (\w+)') {
    if ($Matches[1] -ne 'allow') {
      Add-Undo "appops set sg.round2.tvhome APP_AUTO_START $($Matches[1])"
      Sh 'appops set sg.round2.tvhome APP_AUTO_START allow'
    }
  }
  # lets the app switch its Home-button helper off and on by itself after an 'Update now' (some TVs stop it after updates)
  $gr = Q 'dumpsys package sg.round2.tvhome'
  if ($gr -notmatch 'WRITE_SECURE_SETTINGS: granted=true') {
    Sh 'pm grant sg.round2.tvhome android.permission.WRITE_SECURE_SETTINGS'
    Add-Undo 'pm revoke sg.round2.tvhome android.permission.WRITE_SECURE_SETTINGS'
  }
  $cur = Q 'settings get secure enabled_accessibility_services'
  $on = Q 'settings get secure accessibility_enabled'
  if ($cur -match [regex]::Escape($svc)) {
    # Already enabled (e.g. after an app update): switch it off and on so Android starts it again
    Say 'Restarting the Round2 Home button'
    $others = (($cur -split ':') | Where-Object { $_ -and $_ -ne $svc }) -join ':'
    if ($others) { Sh "settings put secure enabled_accessibility_services $others" } else { Sh 'settings delete secure enabled_accessibility_services' }
    Start-Sleep -Seconds 1
    $new = $cur
  }
  else {
    if (-not $cur -or $cur -eq 'null') { Add-Undo 'settings delete secure enabled_accessibility_services'; $new = $svc }
    else { Add-Undo "settings put secure enabled_accessibility_services $cur"; $new = "$($cur):$svc" }
    if (-not $on -or $on -eq 'null') { Add-Undo 'settings delete secure accessibility_enabled' } else { Add-Undo "settings put secure accessibility_enabled $on" }
  }
  Sh "settings put secure enabled_accessibility_services $new"
  Sh 'settings put secure accessibility_enabled 1'
  Start-Sleep -Seconds 2
  $bound = (Q 'dumpsys accessibility') -split "`n" | Where-Object { $_ -match 'Bound services' } | Select-Object -First 1
  Say "Redirect status: $bound"
  if ("$bound" -notmatch 'HomeGuard') {
    Say 'Android has not started the redirect yet. On the TV: Settings > Device Preferences > Accessibility >'
    Say "'Round2 Home button': switch it Off, then On again. That starts it."
  }
  Say 'Pressing Home to test'
  Key 'KEYCODE_HOME' 3
  Look
  if ($script:Focus -match 'sg\.round2\.tvhome') { Say 'Done: the Home button now opens Round2 Home.' }
  else { Say 'Home did not land on Round2 Home yet. Press Home on the remote and tell Claude what shows.' }
}

function Cmd-LauncherForce {
  Say 'Not for Google TV: disabling its home screen starts a recovery loop. Use: .\round2tv.ps1 homeguard'
  if ((Read-Host '  This is NOT Google TV and you want to continue? Type YES') -ne 'YES') { Die 'Cancelled' }
  Need-Tv; Need-Backup
  $orig = if (Test-Path $HomeFile) { (Get-Content $HomeFile -Raw).Trim() } else { '' }
  if (-not $orig) { Die 'Original home screen unknown - run backup before installing the launcher' }
  $stock = $orig.Split('/')[0]
  Say "This disables the stock home screen app $stock so Home opens your new launcher."
  Say 'It is not uninstalled; undo re-enables it.'
  if ((Read-Host '  Type YES to continue') -ne 'YES') { Die 'Cancelled' }
  $o = Run-Adb -A @('shell', "pm disable-user --user 0 $stock")
  $o | ForEach-Object { Write-Host "   $_" }
  if (($o -join ' ') -match 'disabled') { Add-Undo "pm enable $stock" }
  Key 'KEYCODE_HOME' 3
  Look
}

function Cmd-Room([string]$name) {
  Need-Tv
  $rooms = @('vale', 'kaeden', 'scarlett', 'bella', 'kai', 'atlas', 'maximillion')
  $name = "$name".ToLower()
  if ($rooms -notcontains $name) { Die ("Usage: .\round2tv.ps1 room <" + ($rooms -join '|') + ">") }
  Sh "am start -n sg.round2.tvhome/.MainActivity --es room $name"
  Say "This TV now shows the $name room."
}

function Cmd-Alerts([string]$what) {
  # Food order alerts: lets Round2 Home show 'order ready' over Netflix, the PlayStation input, etc.
  # 'alerts test' first installs the newest TEST build (a pre-release) on this TV only.
  Need-Tv
  $repo = 'donttelldadyet-prog/round2-tv-launcher'
  if ("$what".ToLower() -eq 'test') {
    Log "`$ GET https://api.github.com/repos/$repo/releases?per_page=20"
    $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases?per_page=20" -UseBasicParsing | ForEach-Object { $_ } |
      Where-Object { $_.prerelease -and $_.tag_name -match '^test-.*-\d+$' } |
      Sort-Object { [int]($_.tag_name -replace '^.*-', '') } -Descending | Select-Object -First 1
    if (-not $rel) { Die 'No test build published yet (run "Build Round2 Home" on the branch from the Actions tab)' }
    $a = @($rel.assets | Where-Object { $_.name -eq 'round2-home.apk' }) | Select-Object -First 1
    if (-not $a) { Die "$($rel.tag_name) has no APK" }
    $apk = Join-Path (Join-Path $Dir 'apk') ("round2-home-" + $rel.tag_name + ".apk")
    Say "Installing test build $($rel.tag_name) on this TV only"
    try { Invoke-WebRequest -Uri $a.browser_download_url -OutFile $apk -UseBasicParsing } catch { Die "Download failed: $_" }
    $o = Run-Adb -A @('install', '-r', $apk)
    $o | ForEach-Object { Write-Host "   $_" }
    if (($o -join ' ') -notmatch 'Success') { Die 'Install failed' }
    Say 'The next normal release replaces it by itself.'
  }
  $was = Q 'appops get sg.round2.tvhome SYSTEM_ALERT_WINDOW'
  Sh 'appops set sg.round2.tvhome SYSTEM_ALERT_WINDOW allow'
  if ($was -notmatch 'allow' -and (Test-Path $Undo)) { Add-Undo 'appops set sg.round2.tvhome SYSTEM_ALERT_WINDOW default' }
  Sh 'am start -n sg.round2.tvhome/.MainActivity'
  Say 'Food alerts can now show over other apps. Open the menu once so it knows the room.'
}

function Cmd-Diag {
  Need-Tv
  $f = Join-Path $script:TvDir ("diag-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.txt')
  Say 'Collecting what the TV knows about Round2 Home (changes nothing)...'
  $out = @()
  $out += "== TV: " + (Q 'getprop ro.product.model') + ', Android ' + (Q 'getprop ro.build.version.release') + ', ' + (Q 'getprop ro.product.cpu.abi')
  $out += "== Round2 Home: " + ((Q 'dumpsys package sg.round2.tvhome') -split "`n" | Where-Object { $_ -match 'versionName|lastUpdateTime|WRITE_SECURE_SETTINGS|REQUEST_INSTALL' } | ForEach-Object { $_.Trim() }) -join ' | '
  foreach ($wv in 'com.google.android.webview', 'com.android.webview', 'com.android.chrome') {
    $v = (Q "dumpsys package $wv") -split "`n" | Where-Object { $_ -match 'versionName' } | Select-Object -First 1
    if ($v) { $out += "== WebView ($wv): " + $v.Trim() }
  }
  $out += "== Home screen app: " + (Get-Home)
  $out += "== Accessibility enabled: " + (Q 'settings get secure enabled_accessibility_services')
  $out += "== " + (((Q 'dumpsys accessibility') -split "`n" | Where-Object { $_ -match 'Bound services|Enabled services|Crashed' }) -join ' | ')
  $out += "== Memory: " + (((Q 'dumpsys meminfo sg.round2.tvhome') -split "`n" | Where-Object { $_ -match 'TOTAL' } | Select-Object -First 1))
  $out += "== Audio volume (music stream): " + (((Q 'dumpsys audio') -split "`n" | Where-Object { $_ -match 'STREAM_MUSIC' -or $_ -match 'Muted' } | Select-Object -First 4) -join ' | ')
  $out += '== Log (Round2 Home, crashes, memory kills, accessibility):'
  $log = & $script:Adb -s $script:TV logcat -d -v time -t 6000 2>$null
  $out += @($log | Where-Object { $_ -match 'R2Home|sg\.round2|AndroidRuntime|FATAL|lowmemorykiller|lmkd|AccessibilityManager|HomeGuard|chromium.*(CONSOLE|Uncaught)' } | Select-Object -Last 400)
  $out | Set-Content -Path $f -Encoding UTF8
  Say "Saved: $f"
  Say 'Send this file to Claude.'
}

function Cmd-Themes {
  Need-Tv
  Say 'Asking Round2 Home to check GitHub for new event backgrounds now.'
  Sh 'am start -n sg.round2.tvhome/.MainActivity --ez syncThemes true'
  Say 'New backgrounds download in the background (about a minute) and show under Options > Background.'
}

function Cmd-Perf {
  Need-Tv
  $pkg = 'sg.round2.tvhome'
  Say 'Press Home so the Round2 menu is on screen.'
  Read-Host '  Then press Enter here, and hold Down/Up on the remote for about 10 seconds' | Out-Null
  & $script:Adb -s $script:TV shell "dumpsys gfxinfo $pkg reset" 2>$null | Out-Null
  for ($i = 10; $i -gt 0; $i--) { Write-Host -NoNewline "`r  measuring... $i  "; Start-Sleep -Seconds 1 }
  Write-Host ''
  $g = Q "dumpsys gfxinfo $pkg"
  $want = 'Total frames rendered|Janky frames|50th percentile|90th percentile|95th percentile|99th percentile|Slow UI thread|Slow bitmap uploads|Slow issue draw commands|Frame deadline missed'
  $lines = ($g -split "`n") | Where-Object { $_ -match $want } | Select-Object -First 10
  Say 'Frame statistics for the Round2 menu (lower is better; 16ms = 60 fps):'
  $lines | ForEach-Object { Write-Host "   $($_.Trim())" }
  Log ('perf: ' + ($lines -join ' ; '))
}

function Cmd-Wallpaper([string]$file) {
  Need-Tv; Need-Backup
  if (-not $file -or -not (Test-Path $file)) { Die 'Usage: .\round2tv.ps1 wallpaper C:\path\round2-wallpaper.png' }
  $ext = [IO.Path]::GetExtension($file)
  $dest = "/sdcard/Pictures/round2-wallpaper$ext"
  Sh 'mkdir -p /sdcard/Pictures'
  $o = Run-Adb -A @('push', $file, $dest); $o | ForEach-Object { Write-Host "   $_" }
  Add-Undo "rm -f $dest"
  Say "On the TV: open the launcher's settings > Wallpaper and pick round2-wallpaper$ext from Pictures."
}

function Cmd-YouTube([string]$q) {
  Need-Tv
  if (-not $q) { Die 'Usage: .\round2tv.ps1 youtube "video name or YouTube link"' }
  if ((Get-Packages '') -notcontains $YT) { Die "YouTube for Android TV isn't installed" }
  $id = ''
  $m = [regex]::Match($q, '(v=|youtu\.be/|shorts/|live/)([A-Za-z0-9_-]{11})')
  if ($m.Success) { $id = $m.Groups[2].Value }
  elseif ($q -match '^[A-Za-z0-9_-]{11}$') { $id = $q }
  else {
    Say "Looking up `"$q`" on YouTube"
    try {
      $html = (Invoke-WebRequest -Uri ("https://www.youtube.com/results?search_query=" + [uri]::EscapeDataString($q)) `
          -UseBasicParsing -UserAgent 'Mozilla/5.0' -Headers @{ 'Accept-Language' = 'en' }).Content
      $mm = [regex]::Match($html, '"videoId":"([A-Za-z0-9_-]{11})"')
      if ($mm.Success) { $id = $mm.Groups[1].Value }
    } catch {}
    if (-not $id) { Die "No result for `"$q`" - paste the YouTube link instead" }
  }
  $title = ''
  try { $title = (Invoke-RestMethod -Uri "https://www.youtube.com/oembed?format=json&url=https://www.youtube.com/watch?v=$id" -UseBasicParsing).title } catch {}
  if (-not $title) { $title = $id }
  Say "Video: $title"

  Say 'Step 1/5 - Home button'; Key 'KEYCODE_HOME' 1.5
  Say 'Step 2/5 - Open YouTube'; Sh "monkey -p $YT -c android.intent.category.LEANBACK_LAUNCHER 1"; Start-Sleep -Seconds 7
  Say "Step 3/5 - Check for 'Who's watching?'"; Look
  if ($script:Texts -match "who.?s watching|choose an account") { Press-Ok; Start-Sleep -Seconds 4 }
  elseif (-not $script:Texts) {
    $a = Read-Host "  YouTube's screen can't be read automatically. Is 'Who's watching?' showing with YOUR profile highlighted? [y/N]"
    if ($a -match '^[Yy]') { Press-Ok; Start-Sleep -Seconds 4 }
  }
  Say 'Step 4/5 - Open the video'; Sh "am start -a android.intent.action.VIEW -d 'https://www.youtube.com/watch?v=$id' $YT"
  Start-Sleep -Seconds 8
  Say 'Step 5/5 - Confirm playback (screenshots of video are black because of DRM, so the media session is checked instead)'
  $ms = [regex]::Match((Q 'dumpsys media_session'), 'state=PlaybackState \{state=(\d+)')
  $st = if ($ms.Success) { $ms.Groups[1].Value } else { '' }
  switch ($st) {
    '3' { Say "Playing: $title" }
    '2' { Say 'Paused - pressing Play'; Key 'KEYCODE_MEDIA_PLAY' }
    default { Look; Say 'Playback not confirmed yet (an ad or loading screen may be up). Watch the TV.' }
  }
}

function Cmd-Status {
  Need-Tv
  Say ("TV $($script:TV) - " + (Q 'getprop ro.product.model') + ', Android ' + (Q 'getprop ro.build.version.release'))
  Say ("Home screen app: " + (Get-Home))
  foreach ($k in $TouchedKeys) { Say ("$k = " + (Q "settings get global $k")) }
  if (Test-Path $Undo) {
    Say 'Changes recorded for undo (newest last):'
    Get-Content $Undo | Where-Object { $_ -like '#STEP *' } | ForEach-Object { Write-Host "   $($_.Substring(6))" }
  }
}

function Cmd-Undo {
  Need-Tv; Need-Backup
  Log "`$ undo $Undo"
  $env:ADB = $script:Adb
  & $Undo
  Say 'Every recorded change has been reversed. Run backup again before making new changes.'
  $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
  Move-Item $Undo (Join-Path (Join-Path $script:TvDir 'backup') "undo-used-$stamp.ps1")
}

function Cmd-Monitor { Say "Live monitor (Ctrl+C to stop): $Log"; Get-Content -Path $Log -Tail 40 -Wait }

function Cmd-Lockdown {
  Need-Tv
  Say 'Turning ADB off on the TV so nobody else on the network can connect.'
  Say 'Turn it back on with the remote: Settings > Device Preferences > Developer options > USB debugging.'
  Sh 'settings put global adb_enabled 0'
  Run-Adb -A @('disconnect', $script:TV) -NoLogcat -NoSerial | ForEach-Object { Write-Host "   $_" }
}

function Cmd-Rescue {
  # The way out if the menu is stuck: works even when the remote can't get past Round2 Home
  Need-Tv
  $svc = 'sg.round2.tvhome/sg.round2.tvhome.HomeGuard'
  Say 'Closing Round2 Home and switching its Home button helper off'
  Sh 'am force-stop sg.round2.tvhome'
  $cur = Q 'settings get secure enabled_accessibility_services'
  if ($cur -match [regex]::Escape($svc)) {
    $others = (($cur -split ':') | Where-Object { $_ -and $_ -ne $svc }) -join ':'
    if ($others) { Sh "settings put secure enabled_accessibility_services $others" } else { Sh 'settings delete secure enabled_accessibility_services' }
  }
  Key 'KEYCODE_HOME' 3
  Say 'The TV is on its own Android home screen now. When fixed, turn the Round2 Home button back on: .\round2tv.ps1 homeguard'
}

function Show-Help {
  @"
Round2 TV toolkit - this TV: $($script:TV)   files: $($script:TvDir)
Usage: powershell -ExecutionPolicy Bypass -File .\round2tv.ps1 <command>

  start [IP|room]        setup + connect + backup + draft cleanup list (changes nothing); IP picks the TV
  setup                  install adb from Google's official platform-tools if missing
  connect [IP|room]      connect to a TV by address, or by room name (kai, kaeden...) using the addresses saved in the Theme manager
  connect usb            use the TV on the USB cable (for TVs whose network debugging closes, like Atlas)
  backup                 save package lists, all settings, home app, and create undo.ps1
  propose                draft cleanup list for review (changes nothing)
  cleanup [file]         disable (never uninstall) the approved list (default: cleanup.txt)
  tweaks                 animations 0.5 + Private DNS dns.adguard-dns.com
  install owner/repo     install the latest APK from official GitHub releases (default truefedex/tv-bro)
  airplay                install AirPlay Server (jqssun, open source: iPhone screen mirroring, video and music) from its GitHub releases
  phairplay              install PhairPlay instead (the older AirPlay receiver)
  tizentube              install TizenTube Cobalt (ad-free YouTube); the YouTube button then opens it
  launcher [owner/repo]  install Round2 Home and make it the home screen (default donttelldadyet-prog/round2-tv-launcher)
  diag                   collect logs and settings from this TV into a file to send to Claude (changes nothing)
  themes                 make this TV check for new event backgrounds now (see themes/README.md)
  perf                   measure how smoothly the menu draws (dropped and slow frames)
  alerts [test]          let food order alerts show over other apps ('test' first installs the newest test build here only)
  room <name>            set which room this TV is in (vale, kaeden, scarlett, bella, kai, atlas, maximillion)
  homeguard              make the Home button open Round2 Home when the TV keeps its own home (undo turns it off)
  rescue                 menu stuck? close Round2 Home, switch its Home button off, show the Android home screen
  launcher-force         older Android TV only: disable the stock home app (never on Google TV)
  wallpaper file.png     copy a Round2 wallpaper to the TV for the launcher
  youtube "name|link"    open YouTube with key presses and play that video
  status                 show current values and everything recorded for undo
  monitor                live timestamped log (run in a second PowerShell window)
  undo                   reverse every recorded change, newest first
  lockdown               turn ADB off on the TV when finished
"@ | Write-Host
}

$a1 = if ($Rest) { $Rest[0] } else { '' }
switch ($Command.ToLower()) {
  'start' { Cmd-Setup; Cmd-Connect $a1; Cmd-Backup; Cmd-Propose }
  'setup' { Cmd-Setup }
  'connect' { Cmd-Connect $a1 }
  'backup' { Cmd-Backup }
  'propose' { Cmd-Propose }
  'cleanup' { Cmd-Cleanup $a1 }
  'tweaks' { Cmd-Tweaks }
  'install' { $null = Cmd-Install $a1 }
  'tizentube' { $null = Cmd-Install 'reisxd/TizenTubeCobalt'; Say 'Pick the armeabi-v7a (32-bit ARM) or universal APK. The Round2 YouTube button now opens TizenTube (no ads).' }
  'airplay' { $null = Cmd-Install 'jqssun/android-airplay-server'; Say 'Pick the armeabi-v7a (or universal) APK. Open AirPlay Server once on the TV, allow what it asks, and set its name to this room (e.g. Round2 Kai). The Round2 AirPlay button opens it.' }
  'phairplay' { $null = Cmd-Install 'mazer666/PhairPlay'; Say 'Pick the googletv APK. Then open PhairPlay once and set its device name to this room (e.g. Round2 Kai).' }
  'launcher' { Cmd-Launcher $a1 }
  'launcher-force' { Cmd-LauncherForce }
  'homeguard' { Cmd-HomeGuard }
  'rescue' { Cmd-Rescue }
  'wallpaper' { Cmd-Wallpaper $a1 }
  'room' { Cmd-Room $a1 }
  'alerts' { Cmd-Alerts $a1 }
  'perf' { Cmd-Perf }
  'themes' { Cmd-Themes }
  'diag' { Cmd-Diag }
  'youtube' { Cmd-YouTube ($Rest -join ' ') }
  'status' { Cmd-Status }
  'undo' { Cmd-Undo }
  'monitor' { Cmd-Monitor }
  'lockdown' { Cmd-Lockdown }
  default { Show-Help }
}
