[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [string]$BuildDir,
    [string]$LibUsbDll = "D:/n32/.venv/Lib/site-packages/libusb_package/libusb-1.0.dll",
    [string]$MingwBin = "C:/z/app/mingw64/bin",
    [switch]$SkipConfigure,
    [switch]$SkipBuild,
    [switch]$SkipDllCopy,
    [string[]]$Only,
    [string]$LogPath
)

$ErrorActionPreference = 'Stop'

if (-not $ProjectRoot) {
    $scriptRoot = Split-Path -Parent $PSCommandPath
    $ProjectRoot = Split-Path -Parent $scriptRoot
}

if (-not $BuildDir) {
    $BuildDir = Join-Path $ProjectRoot 'build'
}

if (-not $LogPath) {
    $LogPath = Join-Path $BuildDir 'run_examples.log'
}

function Write-Section {
    param([string]$Text)
    Write-Host "`n=== $Text ==="
}

function Copy-RequiredDll {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$DestinationDir
    )

    if (-not (Test-Path -LiteralPath $Source)) {
        throw "Required DLL not found: $Source"
    }
    Copy-Item -Force -LiteralPath $Source -Destination $DestinationDir
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$DisplayName
    )

    Write-Host "Running $DisplayName"
    Add-Content -LiteralPath $LogPath -Value "=== $DisplayName ==="
    Add-Content -LiteralPath $LogPath -Value ((@($FilePath) + $ArgumentList) -join ' ')

    Push-Location $WorkingDirectory
    try {
        $output = & $FilePath @ArgumentList 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
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

    return [pscustomobject]@{
        Name = $DisplayName
        ExitCode = $exitCode
    }
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Set-Content -LiteralPath $LogPath -Value ("gsusb-cpp example run started at {0}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))

if (-not $SkipConfigure) {
    Write-Section 'Configure'
    & cmake -S $ProjectRoot -B $BuildDir
    if ($LASTEXITCODE -ne 0) {
        throw "cmake configure failed with exit code $LASTEXITCODE"
    }
}

if (-not $SkipBuild) {
    Write-Section 'Build'
    & cmake --build $BuildDir -j 4
    if ($LASTEXITCODE -ne 0) {
        throw "cmake build failed with exit code $LASTEXITCODE"
    }
}

if (-not $SkipDllCopy) {
    Write-Section 'Copy DLLs'
    Copy-RequiredDll -Source $LibUsbDll -DestinationDir $BuildDir
    Copy-RequiredDll -Source (Join-Path $MingwBin 'libstdc++-6.dll') -DestinationDir $BuildDir
    Copy-RequiredDll -Source (Join-Path $MingwBin 'libgcc_s_seh-1.dll') -DestinationDir $BuildDir
    Copy-RequiredDll -Source (Join-Path $MingwBin 'libwinpthread-1.dll') -DestinationDir $BuildDir
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
    $exePath = Join-Path $BuildDir ($test.Name + '.exe')
    if (-not (Test-Path -LiteralPath $exePath)) {
        throw "Example executable not found: $exePath"
    }
    $results += Invoke-LoggedCommand -FilePath $exePath -ArgumentList $test.Args -WorkingDirectory $BuildDir -DisplayName $test.Name
}

Write-Section 'Summary'
$results | Format-Table -AutoSize
$results | Format-Table -AutoSize | Out-String | Add-Content -LiteralPath $LogPath

$failed = @($results | Where-Object { $_.ExitCode -ne 0 })
if ($failed.Count -gt 0) {
    throw ("{0} example(s) failed. See log: {1}" -f $failed.Count, $LogPath)
}

Write-Host "All examples passed. Log saved to $LogPath"