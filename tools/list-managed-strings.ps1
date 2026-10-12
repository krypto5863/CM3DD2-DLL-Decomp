# Lists the string literals (ldstr) in the methods of managed types whose full name matches a pattern (read-only reflection).
# Usage: powershell -File tools/list-managed-strings.ps1 -TypePattern 'TJS|ScriptManager' > strings.txt
param([string]$TypePattern='TJS',[string]$ManagedRoot='E:\COM3D2Test 2.5\COM3D2_5\COM3D2x64_Data\Managed')
$handler=[System.ResolveEventHandler]{param($sender,$eventArgs)
    $dllPath=Join-Path $ManagedRoot (($eventArgs.Name.Split(',')[0])+'.dll')
    if(Test-Path -LiteralPath $dllPath){return [Reflection.Assembly]::ReflectionOnlyLoadFrom($dllPath)}
    return [Reflection.Assembly]::ReflectionOnlyLoad($eventArgs.Name)
}
[AppDomain]::CurrentDomain.add_ReflectionOnlyAssemblyResolve($handler)
$flags=[Reflection.BindingFlags]'Public,NonPublic,Instance,Static,DeclaredOnly'
foreach($name in 'Assembly-CSharp.dll','Assembly-CSharp-firstpass.dll'){
    $asm=[Reflection.Assembly]::ReflectionOnlyLoadFrom((Join-Path $ManagedRoot $name))
    try {$types=$asm.GetTypes()} catch [Reflection.ReflectionTypeLoadException] {$types=$_.Exception.Types}
    foreach($t in ($types | Where-Object {$null -ne $_ -and $_.FullName -match $TypePattern})){
        foreach($m in (@($t.GetMethods($flags))+@($t.GetConstructors($flags)))){
            $body=$null; try{$body=$m.GetMethodBody()}catch{}
            if($null -eq $body){continue}
            $il=$body.GetILAsByteArray(); if($null -eq $il){continue}
            for($i=0;$i -lt $il.Length-4;$i++){
                if($il[$i] -eq 0x72){
                    $tok=[BitConverter]::ToInt32($il,$i+1)
                    if((($tok -shr 24) -band 0xff) -eq 0x70){
                        try{ $s=$m.Module.ResolveString($tok); "$($t.FullName)::$($m.Name)`t$($s -replace "`r?`n",'\n')" }catch{}
                    }
                }
            }
        }
    }
}
