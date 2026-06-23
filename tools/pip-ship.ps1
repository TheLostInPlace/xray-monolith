#requires -Version 5
<#
  pip-ship.ps1 -- build / pack / deploy / clear-caches pipeline for THIS engine, with safety checks.

  Adapted (learned/copied) from F:\Reverse Engineering\Project 4\xray-monolith-mt\tools\pip-ship.ps1
  for the xray-monolith-mt-stock all-in-one MT build, deploying to the 1.5.3 test install.
  WHY THIS EXISTS: the manual pipeline kept biting sessions -
    - MSBuild /flags get mangled if run from Git-bash (run from PowerShell only).
    - a copy that silently failed left a STALE db0 in db\mods (game loaded the old shader).
    - shader cache not cleared -> edited shaders never recompiled.
  Every step fails LOUD and copies are SHA256-verified, so a bad build/copy aborts instead of
  shipping a broken state.

  THIS-REPO SPECIFICS (verified; read before editing):
    - Engine root is derived from this script's location (<repo>\tools), so the script is portable.
    - Install is F:\Anomaly-1.5.3-Testing. Its db\mods starts EMPTY, so the first -Deploy just
      creates db\mods\00_modded_exes.db0 (no foreign db0 to worry about yet).
    - Build config DX11-AVX|x64 -> exe AnomalyDX11AVX.exe, output flat at
      _build\_game\bin_dbg\AnomalyDX11AVX.exe (Common.props sets OutDir=...\_build\_game\bin_dbg\).
    - DX11-AVX disables debug info (Common.props), so there is normally NO .pdb -- the pdb copy is
      guarded and simply skipped.
    - Pack uses this repo's own command (compressor\xrPackage.bat): xrCompress mod -ltx xrCompress.ltx
      -pack -1024 -db. xrCompress.ltx has entry_point=$fs_root$\gamedata\ and include_folders .\=true,
      so gamedata's CONTENTS are staged into compressor\mod\ (shaders\, configs\, ...; levels\ excluded).
    - MSBuild is found via vswhere (not a hardcoded VS path), so it survives VS updates.
    - The exe is backed up ONCE to AnomalyDX11AVX.exe.stock-bak (revert path to the stock exe).
    - db\mods is NOT scrubbed: we only WARN about a foreign .db0; we never delete anything there.

  USAGE (run from PowerShell, NOT Git-bash; GAME CLOSED for -Deploy):
    .\pip-ship.ps1 -All            # build + pack + deploy + clear caches (the full loop)
    .\pip-ship.ps1 -Build          # engine only
    .\pip-ship.ps1 -Pack           # gamedata -> mod.db0 only
    .\pip-ship.ps1 -Deploy         # deploy current build + db0 to the test install
    .\pip-ship.ps1 -Build -Deploy  # engine-only change (no gamedata repack)
    .\pip-ship.ps1 -Pack -Deploy   # gamedata change only (no engine rebuild)
    .\pip-ship.ps1 -Build -Deploy -KeepShaderCache   # engine-only iter, keep compiled shaders
    .\pip-ship.ps1 -Status         # just print the current build/deploy state, change nothing

  Exit code 0 = success; non-zero = a step failed (nothing half-shipped silently).
#>
[CmdletBinding()]
param(
    [switch]$Build,
    [switch]$Pack,
    [switch]$Deploy,
    [switch]$All,
    [switch]$Status,
    [switch]$SkipCacheClear,
    [switch]$KeepShaderCache,
    [switch]$Rebuild   # /t:Rebuild (clean) instead of incremental
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------- CONFIG (edit paths here) ----------------------------------
# Engine root is derived from this script's location (<repo>\tools), so the script is portable.
$Cfg = @{
    Engine   = (Split-Path -Parent $PSScriptRoot)
    Install  = 'F:\Anomaly-1.5.3-Testing'          # the live 1.5.3 test install
    Config   = 'DX11-AVX'
    Platform = 'x64'
    ExeName  = 'AnomalyDX11AVX.exe'
    DbName   = '00_modded_exes.db0'                # db\mods is empty initially; this is the deploy name
}
# derived
$Cfg.Sln        = "$($Cfg.Engine)\src\engine-vs2022.sln"
$Cfg.Gamedata   = "$($Cfg.Engine)\gamedata"
$Cfg.Compressor = "$($Cfg.Engine)\compressor"
$Cfg.Stage      = "$($Cfg.Compressor)\mod"
$Cfg.Packer     = "$($Cfg.Compressor)\xrCompress.exe"
$Cfg.ModDb0     = "$($Cfg.Compressor)\mod.db0"
$Cfg.BuildExe   = "$($Cfg.Engine)\_build\_game\bin_dbg\$($Cfg.ExeName)"
$Cfg.BuildPdb   = $Cfg.BuildExe -replace '\.exe$', '.pdb'
$Cfg.InstBin    = "$($Cfg.Install)\bin"
$Cfg.InstMods   = "$($Cfg.Install)\db\mods"
$Cfg.InstAppd   = "$($Cfg.Install)\appdata"

# ---------------- console helpers ------------------------------------------
function Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Ok  ($m) { Write-Host "   [OK]   $m" -ForegroundColor Green }
function Warn($m) { Write-Host "   [WARN] $m" -ForegroundColor Yellow }
function Die ($m) { Write-Host "   [FAIL] $m" -ForegroundColor Red; throw $m }
function NeedPath($p, $what) { if (-not (Test-Path -LiteralPath $p)) { Die "$what not found: $p" } }
function KB($bytes) { "{0:N0} KB" -f ($bytes / 1KB) }

# locate MSBuild via vswhere so the tool survives VS updates (no hardcoded path).
function Resolve-MSBuild {
    $vsw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vsw) {
        $vsp = & $vsw -latest -property installationPath
        if ($vsp) {
            foreach ($rel in @('MSBuild\Current\Bin\amd64\MSBuild.exe', 'MSBuild\Current\Bin\MSBuild.exe')) {
                $c = Join-Path $vsp $rel
                if (Test-Path -LiteralPath $c) { return $c }
            }
        }
    }
    return $null
}

function Assert-GameClosed {
    $name = $Cfg.ExeName -replace '\.exe$', ''
    $p = Get-Process -Name $name -ErrorAction SilentlyContinue
    if ($p) { Die "the game is RUNNING (pid $($p.Id)) -- close it first; the exe/db0/cache are locked while it runs." }
    Ok "game not running (no file locks)"
}

# Copy with SHA256 + size verification -- the safeguard against the stale-db0 bug.
function Copy-Verified($src, $dst) {
    NeedPath $src "copy source"
    $dstDir = Split-Path -Parent $dst
    if (-not (Test-Path -LiteralPath $dstDir)) { Die "destination dir missing: $dstDir" }
    Copy-Item -LiteralPath $src -Destination $dst -Force
    if (-not (Test-Path -LiteralPath $dst)) { Die "copy produced no file: $dst" }
    $sh = (Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash
    $dh = (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash
    $ssz = (Get-Item -LiteralPath $src).Length
    $dsz = (Get-Item -LiteralPath $dst).Length
    if ($sh -ne $dh -or $ssz -ne $dsz) {
        Die "VERIFY FAILED for $dst -- size $dsz/$ssz, sha256 $(if($sh -eq $dh){'match'}else{'MISMATCH'}). The copy did NOT land; nothing trustworthy was shipped."
    }
    Ok "$(Split-Path -Leaf $dst): copied + sha256-verified ($(KB $dsz)) -> $dst"
}

# ---------------- steps ----------------------------------------------------
function Invoke-Build {
    Step "BUILD engine ($($Cfg.Config) | $($Cfg.Platform))$(if ($Rebuild) { ' [REBUILD]' })"
    $msb = Resolve-MSBuild
    if (-not $msb) { Die "MSBuild not found via vswhere (is VS 2022 installed?)" }
    NeedPath $Cfg.Sln "solution"
    $before = if (Test-Path $Cfg.BuildExe) { (Get-Item $Cfg.BuildExe).LastWriteTime } else { [datetime]::MinValue }
    $log = "$env:TEMP\pip_build.log"
    $mArgs = @($Cfg.Sln, "/p:Configuration=$($Cfg.Config)", "/p:Platform=$($Cfg.Platform)", '/m', '/nologo', '/v:minimal')
    if ($Rebuild) { $mArgs += '/t:Rebuild' }
    # NOTE: call operator so MSBuild /flags survive (Git-bash mangles them). $LASTEXITCODE is the truth.
    & $msb @mArgs *> $log
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Write-Host "----- build log tail -----" -ForegroundColor DarkGray
        Get-Content $log -Tail 30
        Die "MSBuild exited $code. Full log: $log"
    }
    NeedPath $Cfg.BuildExe "build output exe"
    $after = (Get-Item $Cfg.BuildExe).LastWriteTime
    if ($after -le $before) { Warn "exe timestamp not newer than before ($after) -- did anything actually rebuild?" }
    else { Ok "exe linked: $after ($(KB (Get-Item $Cfg.BuildExe).Length))" }
    Ok "BUILD succeeded"
}

function Invoke-Pack {
    Step "PACK gamedata -> mod.db0"
    NeedPath $Cfg.Gamedata "gamedata"; NeedPath $Cfg.Packer "xrCompress.exe"
    NeedPath "$($Cfg.Compressor)\xrCompress.ltx" "xrCompress.ltx"
    # 1) mirror gamedata into the stage (additive /E; skip editor junk; exit 0-7 = ok, >7 = real failure)
    & robocopy $Cfg.Gamedata $Cfg.Stage /E /XD .vscode .git /NJH /NJS /NDL /NFL /R:1 /W:1 | Out-Null
    $rc = $LASTEXITCODE
    if ($rc -gt 7) { Die "robocopy gamedata -> stage failed (exit $rc)" }
    Ok "gamedata mirrored to stage (robocopy exit $rc)"
    # 2) delete old db0 (xrCompress refuses to overwrite -- it would pack 0 bytes and keep the old one)
    if (Test-Path $Cfg.ModDb0) { Remove-Item -LiteralPath $Cfg.ModDb0 -Force }
    if (Test-Path $Cfg.ModDb0) { Die "could not delete old mod.db0 (locked?)" }
    # 3) pack (run from the compressor dir so it finds mod\ + xrCompress.ltx)
    $plog = "$env:TEMP\pip_pack.log"
    Push-Location $Cfg.Compressor
    try { & $Cfg.Packer 'mod' -ltx 'xrCompress.ltx' -pack -1024 -db *> $plog; $pc = $LASTEXITCODE }
    finally { Pop-Location }
    if ($pc -ne 0) { Get-Content $plog -Tail 15; Die "xrCompress exited $pc. Log: $plog" }
    NeedPath $Cfg.ModDb0 "packed mod.db0"
    $sz = (Get-Item $Cfg.ModDb0).Length
    if ($sz -lt 10KB) { Die "mod.db0 is only $sz bytes -- pack almost certainly failed (expected hundreds of KB)." }
    Ok "packed mod.db0: $(KB $sz) @ $((Get-Item $Cfg.ModDb0).LastWriteTime)"
}

function Invoke-Deploy {
    Step "DEPLOY -> $($Cfg.Install)"
    NeedPath $Cfg.Install "test install"
    Assert-GameClosed
    # exe (+ pdb if present). One-time stock backup so we can always revert to the stock exe.
    NeedPath $Cfg.BuildExe "build exe (run -Build first?)"
    $dstExe   = "$($Cfg.InstBin)\$($Cfg.ExeName)"
    $stockBak = "$dstExe.stock-bak"
    if ((Test-Path -LiteralPath $dstExe) -and -not (Test-Path -LiteralPath $stockBak)) {
        Copy-Item -LiteralPath $dstExe -Destination $stockBak
        Ok "backed up stock exe -> $(Split-Path -Leaf $stockBak)"
    }
    Copy-Verified $Cfg.BuildExe $dstExe
    if (Test-Path $Cfg.BuildPdb) { Copy-Verified $Cfg.BuildPdb ($dstExe -replace '\.exe$', '.pdb') }
    # db0. Do NOT scrub db\mods. Just warn about a foreign .db0 that could conflict, then verified-copy ours.
    NeedPath $Cfg.ModDb0 "packed mod.db0 (run -Pack first?)"
    if (-not (Test-Path -LiteralPath $Cfg.InstMods)) {
        New-Item -ItemType Directory -Path $Cfg.InstMods -Force | Out-Null
        Ok "created db\mods (was absent)"
    }
    Get-ChildItem -LiteralPath $Cfg.InstMods -File -Filter '*.db0' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne $Cfg.DbName } |
        ForEach-Object { Warn "foreign .db0 present in db\mods (left in place): $($_.Name)" }
    Copy-Verified $Cfg.ModDb0 "$($Cfg.InstMods)\$($Cfg.DbName)"
    if (-not $SkipCacheClear) { Clear-Caches }
    Ok "DEPLOY succeeded"
}

function Clear-Caches {
    # -KeepShaderCache drops only the shader cache from the clear (engine-only iteration); logs always clear
    $caches = if ($KeepShaderCache) { @('logs') } else { @('shaders_cache', 'logs') }
    Step "CLEAR caches ($($caches -join ' + '))"
    foreach ($d in $caches) {
        $p = "$($Cfg.InstAppd)\$d"
        if (Test-Path $p) {
            try { Remove-Item -LiteralPath $p -Recurse -Force; Ok "cleared $d" }
            catch { Warn "could NOT clear $d -- DELETE IT MANUALLY or edited shaders will not recompile. ($($_.Exception.Message))" }
        }
        else { Ok "$d already absent" }
    }
}

function Show-Status {
    Step "STATUS"
    $items = @(
        @{ n = 'build exe'; p = $Cfg.BuildExe },
        @{ n = 'deployed exe'; p = "$($Cfg.InstBin)\$($Cfg.ExeName)" },
        @{ n = 'packed db0'; p = $Cfg.ModDb0 },
        @{ n = 'deployed db0'; p = "$($Cfg.InstMods)\$($Cfg.DbName)" }
    )
    foreach ($i in $items) {
        if (Test-Path -LiteralPath $i.p) {
            $f = Get-Item -LiteralPath $i.p
            Write-Host ("   {0,-14} {1}  {2}" -f $i.n, $f.LastWriteTime, (KB $f.Length))
        }
        else { Write-Host ("   {0,-14} MISSING ({1})" -f $i.n, $i.p) -ForegroundColor Yellow }
    }
    # cross-check: deployed db0 should byte-match the packed one
    $pd = $Cfg.ModDb0; $dd = "$($Cfg.InstMods)\$($Cfg.DbName)"
    if ((Test-Path $pd) -and (Test-Path $dd)) {
        $same = (Get-FileHash $pd).Hash -eq (Get-FileHash $dd).Hash
        if ($same) { Ok "deployed db0 matches the packed db0 (sha256)" }
        else { Warn "deployed db0 does NOT match the packed db0 -- run -Deploy (this is the stale-db0 trap)" }
    }
    foreach ($d in 'shaders_cache', 'logs') {
        $p = "$($Cfg.InstAppd)\$d"
        Write-Host ("   cache {0,-14} {1}" -f $d, $(if (Test-Path $p) { 'present (clear before launch)' } else { 'absent (ok)' }))
    }
}

# ---------------- main -----------------------------------------------------
if ($All) { $Build = $true; $Pack = $true; $Deploy = $true }
if (-not ($Build -or $Pack -or $Deploy -or $Status)) {
    Write-Host "Nothing to do. Use -All, or any of -Build / -Pack / -Deploy / -Status." -ForegroundColor Yellow
    exit 2
}

$sw = [Diagnostics.Stopwatch]::StartNew()
try {
    if ($Status) { Show-Status }
    if ($Build)  { Invoke-Build }
    if ($Pack)   { Invoke-Pack }
    if ($Deploy) { Invoke-Deploy }
    if ($Build -or $Pack -or $Deploy) {
        Write-Host "`n==> ALL STEPS OK in $([math]::Round($sw.Elapsed.TotalSeconds))s" -ForegroundColor Green
    }
    exit 0
}
catch {
    Write-Host "`n==> ABORTED (nothing half-shipped): $_" -ForegroundColor Red
    exit 1
}
