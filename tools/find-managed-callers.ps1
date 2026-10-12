# Finds the managed methods that call a given method (read-only reflection + IL scan). Usage:
#   powershell -File tools/find-managed-callers.ps1 -Type DLLSoundReaderOGG -Method Read
param([string]$Type,[string]$Method,[string]$ManagedRoot='E:\COM3D2Test 2.5\COM3D2_5\COM3D2x64_Data\Managed')
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
    foreach($t in ($types | Where-Object {$null -ne $_})){
        foreach($m in (@($t.GetMethods($flags))+@($t.GetConstructors($flags)))){
            $body=$null; try{$body=$m.GetMethodBody()}catch{}
            if($null -eq $body){continue}
            $il=$body.GetILAsByteArray(); if($null -eq $il){continue}
            for($i=0;$i -lt $il.Length-4;$i++){
                if($il[$i] -eq 0x28 -or $il[$i] -eq 0x6f){
                    $tok=[BitConverter]::ToInt32($il,$i+1); $hi=($tok -shr 24) -band 0xff
                    if($hi -eq 0x06 -or $hi -eq 0x0a){
                        try{ $r=$m.Module.ResolveMethod($tok) }catch{ continue }
                        if($r.DeclaringType.Name -eq $Type -and $r.Name -eq $Method){ "$name $($t.FullName)::$($m.Name) IL_$($i.ToString('x4'))"; break }
                    }
                }
            }
        }
    }
}
