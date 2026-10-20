# Read-only summary of ASIO_STOP measurements; never touches ASIO or registry.
param(
    [string]$Path = (Join-Path $env:TEMP 'ModernSharedASIO-debug.log'),
    [ValidateRange(1,1000)][int]$Last = 20,
    [switch]$OnlyAudio
)
$ErrorActionPreference = 'Stop'
if(-not (Test-Path -LiteralPath $Path)) { throw "ASIO log not found: $Path" }
function Get-Number([hashtable]$Fields,[string]$Key) {
    [UInt64]$result = 0
    if($Fields.ContainsKey($Key)) {
        if([UInt64]::TryParse([string]$Fields[$Key],[ref]$result)) { return $result }
    }
    return [UInt64]0
}
$rows = @()
$lines = @(Get-Content -LiteralPath $Path | Where-Object { $_ -match '^ASIO_STOP ' } | Select-Object -Last $Last)
foreach($line in $lines) {
    $fields = @{}
    foreach($token in [regex]::Matches($line,'([A-Za-z][A-Za-z0-9_]*)=([^\s]+)')) {
        $fields[$token.Groups[1].Value] = $token.Groups[2].Value
    }
    $nonzero = Get-Number $fields 'host_nonzero_left'
    if($OnlyAudio -and $nonzero -eq 0) { continue }
    $total = Get-Number $fields 'wasapi_submitted_frames'
    $underrun = Get-Number $fields 'underrun_frames'
    $audioMs = Get-Number $fields 'asiodriver_wall_ms'
    $wake = if($fields.ContainsKey('early_producer_wake')) { $fields['early_producer_wake'] } else { '-' }
    $probe = if($fields.ContainsKey('scheduler_probe')) { $fields['scheduler_probe'] } else { '-' }
    $rows += [pscustomobject]@{
        Duration_s = [math]::Round([double]$audioMs/1000.0,2)
        MusicBlocks = $nonzero
        UnderrunFrames = $underrun
        UnderrunEvents = (Get-Number $fields 'underrun_events')
        MaxHost_us = (Get-Number $fields 'max_host_callback_us')
        HostBudgetOver = (Get-Number $fields 'callback_over_block_budget')
        FifoTarget = (Get-Number $fields 'audio_buffer_target_frames')
        FifoMin = (Get-Number $fields 'render_queue_min_frames')
        WakeGapMax_us = (Get-Number $fields 'producer_wake_gap_max_us')
        RenderGapMax_us = (Get-Number $fields 'render_event_gap_max_us')
        EarlyWake = $wake
        SchedulerProbe = $probe
        OutputFallback = if($fields.ContainsKey('output_device_fallback')) { $fields['output_device_fallback'] } else { '-' }
        DirectPct = if($total -gt 0) { [math]::Round(100.0*[double](Get-Number $fields 'native_direct_frames')/[double]$total,2) } else { 0 }
    }
}
if($rows.Count -eq 0) { Write-Host 'No matching ASIO_STOP records.'; return }
$rows | Format-Table -AutoSize | Out-String -Width 240 | Write-Host
Write-Host 'Important: zero host_nonzero_left means Cubase produced silence; startup-only events are not proof of audible glitches.'
Write-Host 'Scheduler probe counters are optional and represent scheduling interval estimates, not audio latency or proven DPC stalls.'
