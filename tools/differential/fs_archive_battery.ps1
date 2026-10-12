param(
    [Parameter(Mandatory=$true)][string]$Probe,
    [Parameter(Mandatory=$true)][string]$Original,
    [Parameter(Mandatory=$true)][string]$Rebuilt,
    [string]$OutputDirectory = 'build-review/archive-battery'
)
$ErrorActionPreference = 'Stop'
$probePath = (Resolve-Path -LiteralPath $Probe).Path
$originalPath = (Resolve-Path -LiteralPath $Original).Path
$rebuiltPath = (Resolve-Path -LiteralPath $Rebuilt).Path
$originalHash = (Get-FileHash -LiteralPath $originalPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($originalHash -ne 'cacba71ac2ae6b8676f7628322d609cc704d185665e92a82fe74d4d34d3daba6') {
    throw 'Original DLL identity mismatch; refusing execution'
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$report = [ordered]@{
    original = @{path=$originalPath; sha256=$originalHash}
    rebuilt = @{path=$rebuiltPath; sha256=(Get-FileHash -LiteralPath $rebuiltPath -Algorithm SHA256).Hash.ToLowerInvariant()}
    probe = @{path=$probePath; sha256=(Get-FileHash -LiteralPath $probePath -Algorithm SHA256).Hash.ToLowerInvariant()}
    cases = @()
    limitations = @('Default mode; valid plain uncompressed/unencrypted WARC fixtures only.',
        'Wide opens through KAG/menu consumers, not native-vtable equivalence.',
        'No game, real archives, malformed decoder, or timing-equivalence claims.')
    pass = $true
}
foreach ($scenario in @('ab', 'ba', 'pre')) {
    $runs = @{}
    foreach ($target in @(@{label='original'; dll=$originalPath}, @{label='rebuilt'; dll=$rebuiltPath})) {
        $stdoutPath = Join-Path $OutputDirectory ($scenario + '-' + $target.label + '.txt')
        $stderrPath = Join-Path $OutputDirectory ($scenario + '-' + $target.label + '.err.txt')
        # The probe has its own 20-second watchdog. Use direct invocation because
        # some managed command-runner environments reject Node child_process.
        $savedPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & $probePath $target.dll $scenario > $stdoutPath 2> $stderrPath
        $status = $LASTEXITCODE
        $ErrorActionPreference = $savedPreference
        $runs[$target.label] = @{status=$status; lines=@(Get-Content -LiteralPath $stdoutPath)}
        # Deferred native mount cleanup: only accept exact owned fixture roots
        # immediately below the OS temp directory, after the probe has exited.
        $diagnostics = Get-Content -LiteralPath $stderrPath -Raw
        if ($diagnostics -match '(?m)^([^\r\n]*[\\/]cm3d2-archive-battery-\d+-\d+)\s*$') {
            $candidate = $Matches[1].Trim()
            $candidate = $candidate -replace '^.*cleanup deferred until process exit: ', ''
            if (Test-Path -LiteralPath $candidate -PathType Container) {
                $resolved = (Resolve-Path -LiteralPath $candidate).Path
                $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')
                if ([IO.Path]::GetDirectoryName($resolved).Equals($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
                    [IO.Path]::GetFileName($resolved) -match '^cm3d2-archive-battery-\d+-\d+$') {
                    Remove-Item -LiteralPath $resolved -Recurse -Force
                }
            }
        }
    }
    $a = $runs.original.lines; $b = $runs.rebuilt.lines
    $differences = @()
    for ($i=0; $i -lt [Math]::Max($a.Count, $b.Count); $i++) {
        $left = if ($i -lt $a.Count) {$a[$i]} else {$null}
        $right = if ($i -lt $b.Count) {$b[$i]} else {$null}
        if ($left -cne $right) { $differences += @{line=$i+1; original=$left; rebuilt=$right} }
    }
    $equal = (Get-FileHash -LiteralPath (Join-Path $OutputDirectory ($scenario+'-original.txt'))).Hash -eq
             (Get-FileHash -LiteralPath (Join-Path $OutputDirectory ($scenario+'-rebuilt.txt'))).Hash
    $report.cases += @{scenario=$scenario; equal=$equal; observationLines=$a.Count;
        originalStatus=$runs.original.status; rebuiltStatus=$runs.rebuilt.status; differences=$differences}
    if (!$equal -or $runs.original.status -ne 0 -or $runs.rebuilt.status -ne 0) {$report.pass=$false}
    Write-Output ($scenario + ': equal=' + $equal + ', lines=' + $a.Count + ', exit=' + $runs.original.status + '/' + $runs.rebuilt.status)
    $differences | Select-Object -First 8 | Format-List
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'report.json') -Encoding UTF8
if (!$report.pass) {exit 1}
