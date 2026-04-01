[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [switch]$SkipBuild,
    [string[]]$Only,
    [string]$LogPath
)

$ErrorActionPreference = 'Stop'

if (-not $ProjectRoot) {
    $scriptRoot = Split-Path -Parent $PSCommandPath
    $ProjectRoot = Split-Path -Parent $scriptRoot
}

$targetDir = Join-Path $ProjectRoot 'target/debug/examples'
if (-not $LogPath) {
    $LogPath = Join-Path $targetDir 'run_examples.log'
}

function Write-Section {
    param([string]$Text)
    Write-Host "`n=== $Text ==="
}

function Invoke-LoggedCommand {
    param(
        [string]$DisplayName,
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory
    )

    Write-Host "Running $DisplayName"
    Add-Content -LiteralPath $LogPath -Value "=== $DisplayName ==="
    Add-Content -LiteralPath $LogPath -Value ((@($FilePath) + $Arguments) -join ' ')

    Push-Location $WorkingDirectory
    try {
        $output = & $FilePath @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    if ($null -ne $output) {
        $text = ($output | Out-String).TrimEnd()
        if ($text.Length -gt 0) {
            Write-Host $text
            Add-Content -LiteralPath $LogPath -Value $text
        }
    }

    Add-Content -LiteralPath $LogPath -Value "EXIT_CODE=$exitCode"
    return [pscustomobject]@{ Name = $DisplayName; ExitCode = $exitCode }
}

New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
Set-Content -LiteralPath $LogPath -Value ("gsusb-rs example run started at {0}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))

if (-not $SkipBuild) {
    Write-Section 'Build'
    Push-Location $ProjectRoot
    try {
        & cargo build --examples
        if ($LASTEXITCODE -ne 0) {
            throw "cargo build failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
}

$tests = @(
    @{ Name = '01_simple_std'; Args = @('--classic', '--bitrate', '1000000', '--tx-channel', '0', '--rx-channel', '1') },
    @{ Name = '02_periodic'; Args = @('--classic', '--bitrate', '1000000', '--tx-channel', '0', '--rx-channel', '1', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '03_simple_fd'; Args = @('--bitrate', '1000000', '--data-bitrate', '5000000', '--tx-channel', '0', '--rx-channel', '1') },
    @{ Name = '04_custom_timing'; Args = @('--tx-channel', '0', '--rx-channel', '1') },
    @{ Name = '04_multi_std_asyncio'; Args = @('--classic', '--bitrate', '1000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '01_pair_4ch_threading'; Args = @('--bitrate', '1000000', '--data-bitrate', '5000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '02_pair_8ch_threading'; Args = @('--bitrate', '1000000', '--data-bitrate', '5000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '05_pair_4ch_threading'; Args = @('--bitrate', '1000000', '--data-bitrate', '5000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '06_pair_8ch_threading'; Args = @('--bitrate', '1000000', '--data-bitrate', '5000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '07_pair_4ch_asyncio'; Args = @('--bitrate', '1000000', '--data-bitrate', '5000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '08_pair_8ch_asyncio'; Args = @('--bitrate', '1000000', '--data-bitrate', '5000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '09_multi_custom_timing_threading'; Args = @('--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '10_multi_custom_timing_asyncio'; Args = @('--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '11_long_run_stress'; Args = @('--bitrate', '1000000', '--data-bitrate', '5000000', '--channel-count', '8', '--duration', '2', '--period', '0.05', '--rx-timeout', '0.05', '--payload-len', '16') }
)

if ($Only -and $Only.Count -gt 0) {
    $selected = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($item in $Only) {
        foreach ($part in ($item -split ',')) {
            $name = $part.Trim()
            if ($name.Length -gt 0) {
                [void]$selected.Add($name)
            }
        }
    }
    $tests = @($tests | Where-Object { $selected.Contains($_.Name) })
    if ($tests.Count -eq 0) {
        throw 'No tests matched -Only'
    }
}

Write-Section 'Run Examples'
$results = @()
foreach ($test in $tests) {
    $exePath = Join-Path $targetDir ($test.Name + '.exe')
    if (-not (Test-Path -LiteralPath $exePath)) {
        throw "Example executable not found: $exePath"
    }
    $results += Invoke-LoggedCommand -DisplayName $test.Name -FilePath $exePath -Arguments $test.Args -WorkingDirectory $targetDir
}

Write-Section 'Summary'
$results | Format-Table -AutoSize
$results | Format-Table -AutoSize | Out-String | Add-Content -LiteralPath $LogPath

$failed = @($results | Where-Object { $_.ExitCode -ne 0 })
if ($failed.Count -gt 0) {
    throw ("{0} example(s) failed. See log: {1}" -f $failed.Count, $LogPath)
}

Write-Host "All examples passed. Log saved to $LogPath"