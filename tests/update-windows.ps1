# Run with Windows PowerShell: powershell -NoProfile -File tests/update-windows.ps1
# Only locally compiled fixtures are executed. All mutations stay under build/.
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../build/updater-tests'))
$root = Join-Path $root ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $root | Out-Null
$helperScript = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../packaging/update-windows.ps1'))
$fixtureCode = @'
using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
public static class Fixture {
 public static void Main(string[] args) {
  File.WriteAllText(Path.Combine(Path.GetDirectoryName(Process.GetCurrentProcess().MainModule.FileName), "started.txt"), string.Join("\n", args));
  Thread.Sleep(6000);
 }
}
'@
Add-Type -TypeDefinition $fixtureCode -OutputAssembly (Join-Path $root 'fixture.exe') -OutputType ConsoleApplication
Add-Type -TypeDefinition 'public static class FailedUpdate { public static void Main() {} }' -OutputAssembly (Join-Path $root 'failed.exe') -OutputType ConsoleApplication

foreach ($scenario in @('success', 'restart-failure', 'invalid-path')) {
    $case = Join-Path $root $scenario
    $stage = Join-Path $case '.iv-update-test'
    New-Item -ItemType Directory -Force $stage | Out-Null
    $target = Join-Path $case 'viewer.exe'
    $source = Join-Path $stage 'download'
    Copy-Item -LiteralPath (Join-Path $root 'fixture.exe') -Destination $target
    $replacement = if ($scenario -eq 'restart-failure') { 'failed.exe' } else { 'fixture.exe' }
    Copy-Item -LiteralPath (Join-Path $root $replacement) -Destination $source
    $stream = [IO.File]::Open($target, [IO.FileMode]::Append)
    $stream.WriteByte(42)
    $stream.Dispose()
    $originalHash = (Get-FileHash -LiteralPath $target).Hash
    $expectedHash = (Get-FileHash -LiteralPath $source).Hash
    $old = Start-Process -FilePath $target -WindowStyle Hidden -PassThru
    $image = 'C:\test images\photo''s $1.png'
    $plan = @{target=$target; source=$source;
        ready=(Join-Path $stage 'ready'); commit=(Join-Path $stage 'commit');
        log=(Join-Path $stage 'update.log'); image=$image; pid=$old.Id}
    if ($scenario -eq 'invalid-path') { $plan.source = Join-Path $root 'fixture.exe' }
    $planPath = Join-Path $stage 'plan.json'
    $plan | ConvertTo-Json | Set-Content -LiteralPath $planPath -Encoding UTF8
    $arguments = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
        '-File', ('"' + $helperScript + '"'), '-PlanPath', ('"' + $planPath + '"'))
    $helper = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $stage 'helper-output.log') -RedirectStandardError (Join-Path $stage 'helper-errors.log')
    # Retain the process handle so Windows PowerShell can read ExitCode after
    # its redirected-output handler observes process termination.
    $null = $helper.Handle
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (!(Test-Path -LiteralPath $plan.ready) -and !$helper.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    if ($scenario -eq 'invalid-path') {
        if (!$helper.WaitForExit(3000) -or $helper.ExitCode -eq 0) { throw 'Invalid path was accepted' }
        if ((Get-FileHash -LiteralPath $target).Hash -ne $originalHash) { throw 'Invalid plan modified target' }
        Write-Output 'PASS: invalid staging path rejected without changing target'
        continue
    }
    if (!(Test-Path -LiteralPath $plan.ready)) { throw "Helper not ready: $case" }
    if ((Get-FileHash -LiteralPath $target).Hash -ne $originalHash) { throw 'Target changed before handoff' }
    [IO.File]::WriteAllText($plan.commit, 'commit')
    if ($scenario -eq 'restart-failure') {
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        while ([DateTime]::UtcNow -lt $deadline) {
            if ((Test-Path -LiteralPath $plan.log) -and
                [IO.File]::ReadAllText($plan.log).Contains('exited immediately')) { break }
            Start-Sleep -Milliseconds 100
        }
        if (!(Test-Path -LiteralPath $plan.log) -or
            ![IO.File]::ReadAllText($plan.log).Contains('exited immediately')) { throw 'Restart failure not reported' }
        # Dismiss only this fixture helper's error dialog.
        $deadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $helper.Refresh()
            if ($helper.MainWindowHandle -ne 0) { $null = $helper.CloseMainWindow(); break }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        if (!$helper.WaitForExit(3000) -or $helper.ExitCode -eq 0) { throw 'Expected failed update result' }
        if ((Get-FileHash -LiteralPath $target).Hash -ne $expectedHash) { throw 'Unexpected rollback' }
        if (Test-Path -LiteralPath (Join-Path $stage 'previous.exe')) { throw 'Unexpected backup' }
        Write-Output 'PASS: immediate exit is reported without rollback'
    } else {
        if (!$helper.WaitForExit(15000) -or $helper.ExitCode -ne 0) { throw "Update failed: $case" }
        if ((Get-FileHash -LiteralPath $target).Hash -ne $expectedHash) { throw 'Replacement mismatch' }
        if (Test-Path -LiteralPath (Join-Path $stage 'previous.exe')) { throw 'Unexpected backup' }
        if ([IO.File]::ReadAllText((Join-Path $case 'started.txt')) -cne $image) { throw 'Image path changed' }
        Write-Output 'PASS: replacement without backup, restart, and exact image argument preservation'
    }
}
