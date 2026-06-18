#Requires -Version 5.1
<#
.SYNOPSIS
  One-click install Clawd Mochi hooks for Cursor + Claude Code.

.PARAMETER DeviceIP
  Mochi device LAN IP or hostname, e.g. 192.168.150.21 or clawd.local.
  Omit to auto-discover clawd.local on the LAN.

.PARAMETER NodePath
  Optional path to node.exe

.PARAMETER SkipCursor
  Skip Cursor ~/.cursor/hooks.json

.PARAMETER SkipClaude
  Skip Claude Code ~/.claude/settings.json hooks

.EXAMPLE
  .\install-global.ps1 -DeviceIP 192.168.150.21

.EXAMPLE
  .\install-global.ps1 -DeviceIP 192.168.150.21 -SkipCursor
#>

param(
    [string]$DeviceIP = "",
    [string]$NodePath = "",
    [switch]$SkipCursor,
    [switch]$SkipClaude
)

$ErrorActionPreference = "Stop"

$ScriptDir      = Split-Path -Parent $MyInvocation.MyCommand.Path
$SourceHook     = Join-Path $ScriptDir "clawd-hook.js"
$GlobalDir      = Join-Path $env:USERPROFILE ".clawd-mood\hook"
$GlobalHook     = Join-Path $GlobalDir "clawd-hook.js"
$GlobalDev      = Join-Path $GlobalDir "device.json"
$CursorDir      = Join-Path $env:USERPROFILE ".cursor"
$CursorHooks    = Join-Path $CursorDir "hooks.json"
$ClaudeDir      = Join-Path $env:USERPROFILE ".claude"
$ClaudeSettings = Join-Path $ClaudeDir "settings.json"

function Find-NodePath {
    param([string]$Preferred)
    if ($Preferred -and (Test-Path $Preferred)) {
        return (Resolve-Path $Preferred).Path
    }
    $candidates = @(
        "D:\nodejs\node.exe",
        "$env:ProgramFiles\nodejs\node.exe",
        "${env:ProgramFiles(x86)}\nodejs\node.exe",
        "$env:LOCALAPPDATA\Programs\node\node.exe"
    )
    foreach ($p in $candidates) {
        if (Test-Path $p) { return (Resolve-Path $p).Path }
    }
    $fromPath = Get-Command node -ErrorAction SilentlyContinue
    if ($fromPath) { return $fromPath.Source }
    throw "node.exe not found. Install Node.js or pass -NodePath."
}

function Read-ExistingDeviceIP {
    $repoDev = Join-Path $ScriptDir "device.json"
    foreach ($path in @($GlobalDev, $repoDev)) {
        if (Test-Path $path) {
            try {
                $cfg = Get-Content $path -Raw -Encoding UTF8 | ConvertFrom-Json
                if ($cfg.device_ip) { return [string]$cfg.device_ip.Trim() }
            } catch { }
        }
    }
    return ""
}

function Build-CursorHookCommand {
    param([string]$Node, [string]$Hook)
    $nodeEsc = $Node.Replace('\', '\\')
    $hookFwd = $Hook.Replace('\', '/')
    return ('cmd /d /s /c ""' + $nodeEsc + '" "' + $hookFwd + '""')
}

function Build-ClaudeHookCommand {
    param([string]$Node, [string]$Hook, [string]$Event)
    $nodeEsc = $Node.Replace('\', '\\')
    $hookFwd = $Hook.Replace('\', '/')
    return ('& "' + $nodeEsc + '" "' + $hookFwd + '" ' + $Event)
}

function New-CursorHooksJson {
    param([string]$Command)
    $events = @(
        "sessionStart", "sessionEnd", "beforeSubmitPrompt",
        "preToolUse", "postToolUse", "postToolUseFailure",
        "subagentStart", "subagentStop", "preCompact", "stop"
    )
    $entry = @{ command = $Command; timeout = 5 }
    $hooks = [ordered]@{}
    foreach ($e in $events) { $hooks[$e] = @($entry) }
    return (@{ version = 1; hooks = $hooks } | ConvertTo-Json -Depth 5)
}

function New-ClaudeMochiHooks {
    param([string]$Node, [string]$Hook)
    $events = @(
        "SessionStart", "SessionEnd", "UserPromptSubmit",
        "PreToolUse", "PostToolUse", "PostToolUseFailure",
        "SubagentStart", "SubagentStop", "PreCompact", "PostCompact",
        "Stop", "Notification"
    )
    $hooks = [ordered]@{}
    foreach ($e in $events) {
        $cmd = Build-ClaudeHookCommand -Node $Node -Hook $Hook -Event $e
        $hooks[$e] = @(@{
            matcher = ""
            hooks   = @(@{
                type    = "command"
                shell   = "powershell"
                command = $cmd
                async   = $true
                timeout = 5
            })
        })
    }
    return $hooks
}

function Merge-ClaudeSettings {
    param([hashtable]$MochiHooks)
    $settings = @{}
    if (Test-Path $ClaudeSettings) {
        $parsed = Get-Content $ClaudeSettings -Raw -Encoding UTF8 | ConvertFrom-Json
        $parsed.PSObject.Properties | ForEach-Object { $settings[$_.Name] = $_.Value }
    }
    $mergedHooks = [ordered]@{}
    if ($settings.ContainsKey("hooks") -and $settings["hooks"]) {
        $settings["hooks"].PSObject.Properties | ForEach-Object { $mergedHooks[$_.Name] = $_.Value }
    }
    foreach ($key in $MochiHooks.Keys) {
        $mergedHooks[$key] = $MochiHooks[$key]
    }
    $settings["hooks"] = $mergedHooks
    return ($settings | ConvertTo-Json -Depth 12)
}

if (-not (Test-Path $SourceHook)) {
    throw "clawd-hook.js not found: $SourceHook"
}

if (-not $DeviceIP) {
    $existing = Read-ExistingDeviceIP
    if ($existing) {
        $prompt = "Mochi device IP/host [detected: $existing]"
        $DeviceIP = Read-Host $prompt
        if (-not $DeviceIP) { $DeviceIP = $existing }
    } else {
        # 自动发现:试 clawd.local 根路径(配网页);设备无 /state(已随 Web 手动控制移除)
        try {
            $probe = Invoke-WebRequest -Uri "http://clawd.local/" -UseBasicParsing -TimeoutSec 3
            if ($probe.StatusCode -eq 200) {
                Write-Host "Auto-discovered device at clawd.local" -ForegroundColor Green
                $DeviceIP = "clawd.local"
            }
        } catch { }
        if (-not $DeviceIP) {
            $DeviceIP = Read-Host "Mochi device IP/host (e.g. 192.168.150.21 or clawd.local; see device screen)"
        }
    }
}

$DeviceIP = $DeviceIP.Trim()
$ipPattern   = '^\d{1,3}(\.\d{1,3}){3}$'
$hostPattern = '^[A-Za-z0-9][A-Za-z0-9.\-]*$'
if ($DeviceIP -notmatch $ipPattern -and $DeviceIP -notmatch $hostPattern) {
    throw "Invalid IP/host: $DeviceIP"
}

$NodePath = Find-NodePath -Preferred $NodePath
Write-Host "[1/5] Node.js: $NodePath" -ForegroundColor Gray

New-Item -ItemType Directory -Force -Path $GlobalDir | Out-Null
Copy-Item -Path $SourceHook -Destination $GlobalHook -Force
Write-Host "[2/5] Hook script: $GlobalHook" -ForegroundColor Gray

$deviceJson = @{ device_ip = $DeviceIP } | ConvertTo-Json
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText($GlobalDev, $deviceJson + [Environment]::NewLine, $utf8NoBom)
Write-Host "[2/5] device.json: $GlobalDev  (IP: $DeviceIP)" -ForegroundColor Gray

$RepoDev = Join-Path $ScriptDir "device.json"
[System.IO.File]::WriteAllText($RepoDev, $deviceJson + [Environment]::NewLine, $utf8NoBom)

$step = 3

if (-not $SkipCursor) {
    New-Item -ItemType Directory -Force -Path $CursorDir | Out-Null
    if (Test-Path $CursorHooks) {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $bak = "$CursorHooks.bak.$stamp"
        Copy-Item $CursorHooks $bak -Force
        Write-Host "[$step/5] Cursor backup -> $bak" -ForegroundColor Yellow
    }
    $hookCmd = Build-CursorHookCommand -Node $NodePath -Hook $GlobalHook
    $hooksContent = New-CursorHooksJson -Command $hookCmd
    [System.IO.File]::WriteAllText($CursorHooks, $hooksContent + [Environment]::NewLine, $utf8NoBom)
    Write-Host "[$step/5] Cursor hooks: $CursorHooks" -ForegroundColor Gray
    $step++
}

if (-not $SkipClaude) {
    New-Item -ItemType Directory -Force -Path $ClaudeDir | Out-Null
    if (Test-Path $ClaudeSettings) {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        $bak = "$ClaudeSettings.bak.$stamp"
        Copy-Item $ClaudeSettings $bak -Force
        Write-Host "[$step/5] Claude backup -> $bak" -ForegroundColor Yellow
    }
    $claudeHooks = New-ClaudeMochiHooks -Node $NodePath -Hook $GlobalHook
    $claudeJson = Merge-ClaudeSettings -MochiHooks $claudeHooks
    [System.IO.File]::WriteAllText($ClaudeSettings, $claudeJson + [Environment]::NewLine, $utf8NoBom)
    Write-Host "[$step/5] Claude Code hooks merged into: $ClaudeSettings" -ForegroundColor Gray
    $step++
}

Write-Host "[5/5] Testing device..." -ForegroundColor Gray
try {
    $uri = "http://$DeviceIP/"
    $r = Invoke-WebRequest -Uri $uri -UseBasicParsing -TimeoutSec 3
    Write-Host "      Device OK  $($r.Content)" -ForegroundColor Green
} catch {
    Write-Host "      Device unreachable (hooks installed; check IP/WiFi)" -ForegroundColor Yellow
}

& $NodePath $GlobalHook SessionStart | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "      Hook self-test exit=$LASTEXITCODE" -ForegroundColor Yellow
} else {
    Write-Host "      Hook self-test OK (SessionStart -> idle)" -ForegroundColor Green
}

Write-Host ""
Write-Host "Done!" -ForegroundColor Green
Write-Host "  Global hook dir : $GlobalDir"
Write-Host "  Device IP       : $DeviceIP"
Write-Host "  Update IP later : edit $GlobalDev"
if (-not $SkipCursor) { Write-Host "  Cursor config   : $CursorHooks" }
if (-not $SkipClaude) { Write-Host "  Claude config   : $ClaudeSettings" }
Write-Host ""
if (-not $SkipCursor) {
    Write-Host "Restart Cursor, use Agent mode in any project." -ForegroundColor Cyan
}
if (-not $SkipClaude) {
    Write-Host "Restart Claude Code, start a new session." -ForegroundColor Cyan
}
Write-Host "Re-run with new IP: .\install-global.ps1 -DeviceIP x.x.x.x" -ForegroundColor Cyan
