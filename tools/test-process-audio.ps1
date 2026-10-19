param(
    [Parameter(Mandatory=$true)][string]$Probe,
    [string]$OutputDirectory = (Join-Path $env:TEMP ('sunshine-process-audio-' + [guid]::NewGuid()))
)
$ErrorActionPreference = 'Stop'
if ((Get-Process -Id $PID).SessionId -eq 0) { throw 'Run in an interactive user session with a working audio output.' }
$Probe = (Resolve-Path $Probe).Path
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$tones = @()
try {
    foreach ($frequency in @(440,880,1320)) {
        $tones += Start-Process $Probe -ArgumentList "--tone $frequency" -PassThru -WindowStyle Hidden
    }
    Start-Sleep 2
    for ($i=0; $i -lt 4; $i++) {
        $frequency = @(440,880,1320,0)[$i]
        $target = if ($i -lt 3) {$tones[$i].Id} else {0}
        $log = Join-Path $OutputDirectory "$frequency.log"
        $capture = Start-Process $Probe -ArgumentList "$target 4" -PassThru -WindowStyle Hidden -RedirectStandardOutput $log
        if (!$capture.WaitForExit(10000)) { $capture.Kill(); throw "Capture timed out for $frequency" }
        $result = Get-Content $log -Raw
        $amplitudes = @{}
        foreach ($match in [regex]::Matches($result, '(440|880|1320)Hz=([0-9.]+)')) {
            $amplitudes[[int]$match.Groups[1].Value] = [double]::Parse($match.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
        }
        if ($amplitudes.Count -ne 3) { throw "Missing measurements: $result" }
        if ($frequency) {
            $wanted = $amplitudes[$frequency]
            if ($capture.ExitCode -ne 0 -or $wanted -lt 0.01) { throw "Wanted tone absent: $result" }
            foreach ($other in @(440,880,1320)) {
                if ($other -ne $frequency -and $amplitudes[$other]/$wanted -ge 0.001) { throw "Cross-talk: $result" }
            }
        } else {
            if ($capture.ExitCode -ne 3 -or ($amplitudes.Values | Where-Object {$_ -ne 0})) { throw "Unrelated process captured audio: $result" }
        }
        Write-Output "PASS: $frequency Hz"
    }
} finally {
    $tones | Stop-Process -ErrorAction SilentlyContinue
}
Write-Output "Evidence: $OutputDirectory"
