param([Parameter(Mandatory=$true)][string]$PlanPath)
$ErrorActionPreference = 'Stop'
$plan = Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8 | ConvertFrom-Json
$replaced = $false
function Start-Viewer {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $plan.target
    $start.WorkingDirectory = [IO.Path]::GetDirectoryName($plan.target)
    $start.UseShellExecute = $false
    if ($plan.image) {
        # Windows filenames cannot contain quotes; double trailing backslashes.
        $argument = [string]$plan.image
        $argument = [regex]::Replace($argument, '(\\+)$', '$1$1')
        $start.Arguments = '"' + $argument + '"'
    }
    return [Diagnostics.Process]::Start($start)
}
try {
    $stage = [IO.Path]::GetFullPath((Split-Path -LiteralPath $PlanPath))
    $target = [IO.Path]::GetFullPath($plan.target)
    foreach ($entry in @($plan.source, $plan.ready, $plan.commit, $plan.log)) {
        if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($entry)) -ne $stage) {
            throw 'Invalid update staging path.'
        }
    }
    if ([IO.Path]::GetDirectoryName($stage) -ne [IO.Path]::GetDirectoryName($target) -or
        !(Test-Path -LiteralPath $target -PathType Leaf) -or
        !(Test-Path -LiteralPath $plan.source -PathType Leaf)) { throw 'Invalid update target.' }
    $oldProcess = Get-Process -Id $plan.pid -ErrorAction Stop
    [IO.File]::WriteAllText($plan.ready, 'ready')
    $handoffDeadline = [DateTime]::UtcNow.AddSeconds(30)
    while (!(Test-Path -LiteralPath $plan.commit)) {
        if ([DateTime]::UtcNow -ge $handoffDeadline) { throw 'Update handoff was canceled.' }
        Start-Sleep -Milliseconds 100
    }
    if (!$oldProcess.WaitForExit(120000)) { throw 'Timed out waiting for the viewer to close.' }
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        try {
            [IO.File]::Replace($plan.source, $target, [System.Management.Automation.Language.NullString]::Value)
            $replaced = $true
            break
        } catch {
            if ($attempt -eq 19) { throw }
            Start-Sleep -Milliseconds 500
        }
    }
    $newProcess = Start-Viewer
    if ($newProcess.WaitForExit(2000)) { throw 'The updated viewer exited immediately.' }
    [IO.File]::WriteAllText($plan.log, 'Update installed.')
} catch {
    $failure = $_.Exception.Message
    if (!$replaced -and $oldProcess -and $oldProcess.HasExited) {
        try { $null = Start-Viewer } catch {}
    }
    [IO.File]::WriteAllText($plan.log, $failure)
    if (Test-Path -LiteralPath $plan.commit) {
        Add-Type -AssemblyName System.Windows.Forms
        [Windows.Forms.MessageBox]::Show($failure + "`nDetails: " + $plan.log, 'Image Viewer update failed') | Out-Null
    }
    exit 1
}
