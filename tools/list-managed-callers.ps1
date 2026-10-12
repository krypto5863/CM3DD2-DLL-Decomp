# For every P/Invoke into cm3d2.dll, counts the call sites in the game's managed code (read-only reflection + IL scan, nothing
# is executed) and names the calling methods. Usage: powershell -File tools/list-managed-callers.ps1 > callers.txt
param([string]$ManagedRoot='E:\COM3D2Test 2.5\COM3D2_5\COM3D2x64_Data\Managed')
$handler=[System.ResolveEventHandler]{param($sender,$eventArgs)
    $dllPath=Join-Path $ManagedRoot (($eventArgs.Name.Split(',')[0])+'.dll')
    if(Test-Path -LiteralPath $dllPath){return [Reflection.Assembly]::ReflectionOnlyLoadFrom($dllPath)}
    return [Reflection.Assembly]::ReflectionOnlyLoad($eventArgs.Name)
}
[AppDomain]::CurrentDomain.add_ReflectionOnlyAssemblyResolve($handler)
$flags=[Reflection.BindingFlags]'Public,NonPublic,Instance,Static,DeclaredOnly'
$imports=@{}   # method metadata token + module -> entry name
$all=@()
foreach($name in 'Assembly-CSharp.dll','Assembly-CSharp-firstpass.dll'){
    $asm=[Reflection.Assembly]::ReflectionOnlyLoadFrom((Join-Path $ManagedRoot $name))
    try {$types=$asm.GetTypes()} catch [Reflection.ReflectionTypeLoadException] {$types=$_.Exception.Types}
    foreach($type in ($types | Where-Object {$null -ne $_})){ $all += ,@($asm,$type) }
}
foreach($pair in $all){
    $type=$pair[1]
    foreach($method in $type.GetMethods($flags)){
        foreach($attr in $method.GetCustomAttributesData()){
            if($attr.Constructor.DeclaringType.Name -eq 'DllImportAttribute' -and $attr.ConstructorArguments[0].Value -eq 'cm3d2'){
                $entry=$method.Name; foreach($n in $attr.NamedArguments){ if($n.MemberName -eq 'EntryPoint'){$entry=$n.TypedValue.Value} }
                $imports["$($method.Module.Name):$($method.MetadataToken)"]=$entry
            }
        }
    }
}
$counts=@{}; $callers=@{}
foreach($pair in $all){
    $type=$pair[1]
    foreach($method in (@($type.GetMethods($flags))+@($type.GetConstructors($flags)))){
        $body=$null; try{$body=$method.GetMethodBody()}catch{}
        if($null -eq $body){continue}
        $il=$body.GetILAsByteArray(); if($null -eq $il){continue}
        for($i=0;$i -lt $il.Length-4;$i++){
            if($il[$i] -eq 0x28){   # call
                $tok=[BitConverter]::ToInt32($il,$i+1)
                if((($tok -shr 24) -band 0xff) -eq 0x06){
                    $key="$($method.Module.Name):$tok"
                    if($imports.ContainsKey($key)){
                        $e=$imports[$key]; $counts[$e]=1+[int]$counts[$e]
                        if(-not $callers.ContainsKey($e)){$callers[$e]=@()}
                        $c="$($type.FullName)::$($method.Name)"; if($callers[$e] -notcontains $c){$callers[$e]+=$c}
                    }
                }
            }
        }
    }
}
foreach($e in ($imports.Values | Sort-Object)){
    "$e|$([int]$counts[$e])|$(($callers[$e] | Select-Object -First 4) -join ', ')"
}
