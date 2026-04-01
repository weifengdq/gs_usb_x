[CmdletBinding()]
param(
    [string]$ProjectRoot,
    [string]$Configuration = 'Debug',
    [string]$TargetFramework = 'net9.0',
    [string]$LibUsbDll = 'D:/n32/.venv/Lib/site-packages/libusb_package/libusb-1.0.dll',
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

$solutionPath = Join-Path $ProjectRoot 'gsusb-csharp.sln'
$examplesProject = Join-Path $ProjectRoot 'examples/GsUsb.Examples/GsUsb.Examples.csproj'
$outputDir = Join-Path $ProjectRoot ("examples/GsUsb.Examples/bin/{0}/{1}" -f $Configuration, $TargetFramework)

if (-not $LogPath) {
    $LogPath = Join-Path $outputDir 'run_examples.log'
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

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
Set-Content -LiteralPath $LogPath -Value ("gsusb-csharp example run started at {0}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))

if (-not $SkipBuild) {
    Write-Section 'Build'
    & dotnet build $solutionPath -c $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "dotnet build failed with exit code $LASTEXITCODE"
    }
}

if (-not $SkipDllCopy) {
    Write-Section 'Copy DLLs'
    if (-not (Test-Path -LiteralPath $LibUsbDll)) {
        throw "libusb DLL not found: $LibUsbDll"
    }
    Copy-Item -Force -LiteralPath $LibUsbDll -Destination (Join-Path $outputDir 'libusb-1.0.dll')
}

$tests = @(
    @{ Name = '01_simple_std'; Args = @('01_simple_std', '--classic', '--bitrate', '1000000', '--tx-channel', '0', '--rx-channel', '1') },
    @{ Name = '02_periodic'; Args = @('02_periodic', '--classic', '--bitrate', '1000000', '--tx-channel', '0', '--rx-channel', '1', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '03_simple_fd'; Args = @('03_simple_fd', '--bitrate', '1000000', '--data-bitrate', '5000000', '--tx-channel', '0', '--rx-channel', '1') },
    @{ Name = '04_custom_timing'; Args = @('04_custom_timing', '--tx-channel', '0', '--rx-channel', '1') },
    @{ Name = '04_multi_std_asyncio'; Args = @('04_multi_std_asyncio', '--classic', '--bitrate', '1000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '01_pair_4ch_threading'; Args = @('01_pair_4ch_threading', '--bitrate', '1000000', '--data-bitrate', '5000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '02_pair_8ch_threading'; Args = @('02_pair_8ch_threading', '--bitrate', '1000000', '--data-bitrate', '5000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '05_pair_4ch_threading'; Args = @('05_pair_4ch_threading', '--bitrate', '1000000', '--data-bitrate', '5000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '06_pair_8ch_threading'; Args = @('06_pair_8ch_threading', '--bitrate', '1000000', '--data-bitrate', '5000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '07_pair_4ch_asyncio'; Args = @('07_pair_4ch_asyncio', '--bitrate', '1000000', '--data-bitrate', '5000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '08_pair_8ch_asyncio'; Args = @('08_pair_8ch_asyncio', '--bitrate', '1000000', '--data-bitrate', '5000000', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '09_multi_custom_timing_threading'; Args = @('09_multi_custom_timing_threading', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '10_multi_custom_timing_asyncio'; Args = @('10_multi_custom_timing_asyncio', '--duration', '1', '--period', '0.05', '--rx-timeout', '0.05') },
    @{ Name = '11_long_run_stress'; Args = @('11_long_run_stress', '--bitrate', '1000000', '--data-bitrate', '5000000', '--channel-count', '8', '--duration', '2', '--period', '0.05', '--rx-timeout', '0.05', '--payload-len', '16') }
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

$exePath = Join-Path $outputDir 'GsUsb.Examples.exe'
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Example executable not found: $exePath"
}

Write-Section 'Run Examples'
$results = @()
foreach ($test in $tests) {
    $results += Invoke-LoggedCommand -DisplayName $test.Name -FilePath $exePath -Arguments $test.Args -WorkingDirectory $outputDir
}

Write-Section 'Summary'
$results | Format-Table -AutoSize
$results | Format-Table -AutoSize | Out-String | Add-Content -LiteralPath $LogPath

$failed = @($results | Where-Object { $_.ExitCode -ne 0 })
if ($failed.Count -gt 0) {
    throw ("{0} example(s) failed. See log: {1}" -f $failed.Count, $LogPath)
}

Write-Host "All examples passed. Log saved to $LogPath"