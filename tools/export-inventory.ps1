param(
    [string]$EvidencePath = (Join-Path $PSScriptRoot '..\rea-evidence.json'),
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\docs\exports.csv')
)

$bundle = Get-Content -Raw -LiteralPath $EvidencePath | ConvertFrom-Json
$record = $bundle.records |
    Where-Object operation -eq 'list_names' |
    Select-Object -First 1

if ($null -eq $record) {
    throw 'The evidence bundle has no list_names record.'
}

$exports = $record.normalized_result |
    Where-Object {
        -not $_.symbol.external -and
        $_.value -match '^(D?DLL_|entry$)'
    } |
    ForEach-Object {
        $group = if ($_.value -match '^(D?DLL_[^_]+)') { $Matches[1] } else { 'entry' }
        [pscustomobject]@{
            Address = $_.address
            Name = $_.value
            Group = $group
            SymbolType = $_.symbol.type
        }
    } |
    Sort-Object Address, Name

$exports | Export-Csv -NoTypeInformation -Encoding utf8 -LiteralPath $OutputPath
Write-Output "Wrote $($exports.Count) export symbols to $OutputPath"
