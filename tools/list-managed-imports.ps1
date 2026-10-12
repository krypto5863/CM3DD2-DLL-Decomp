# Lists every P/Invoke into cm3d2.dll declared by the game's managed assemblies (read-only reflection, nothing executed):
# type, method, entry point, calling convention, return type and parameters (with ref/out and MarshalAs). Used to check the
# reconstructed export signatures against the callers. Usage: powershell -File tools/list-managed-imports.ps1 > imports.txt
param([string]$ManagedRoot='E:\COM3D2Test 2.5\COM3D2_5\COM3D2x64_Data\Managed')
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
    foreach($type in ($types | Where-Object {$null -ne $_})){
        foreach($method in $type.GetMethods($flags)){
            foreach($attr in $method.GetCustomAttributesData()){
                if($attr.Constructor.DeclaringType.Name -ne 'DllImportAttribute'){continue}
                $dll=$attr.ConstructorArguments[0].Value
                $entry=$method.Name; $conv=''; $charset=''
                foreach($n in $attr.NamedArguments){ if($n.MemberName -eq 'EntryPoint'){$entry=$n.TypedValue.Value}; if($n.MemberName -eq 'CallingConvention'){$conv=$n.TypedValue.Value}; if($n.MemberName -eq 'CharSet'){$charset=$n.TypedValue.Value} }
                $params=@()
                foreach($p in $method.GetParameters()){
                    $marshal=''
                    foreach($pa in $p.GetCustomAttributesData()){ if($pa.Constructor.DeclaringType.Name -eq 'MarshalAsAttribute'){ $marshal=' [MarshalAs '+$pa.ConstructorArguments[0].Value+']' } }
                    $params += "$($p.ParameterType.FullName) $($p.Name)$marshal"
                }
                $retMarshal=''
                "$dll|$entry|$conv|$charset|$($type.FullName)::$($method.Name)|ret $($method.ReturnType.FullName)|" + ($params -join '; ')
            }
        }
    }
}

# Layouts of the value types (and delegates) the imports pass by reference
"----- STRUCTS -----"
$seen=@{}
foreach($name in 'Assembly-CSharp.dll','Assembly-CSharp-firstpass.dll'){
    $asm=[Reflection.Assembly]::ReflectionOnlyLoadFrom((Join-Path $ManagedRoot $name))
    try {$types=$asm.GetTypes()} catch [Reflection.ReflectionTypeLoadException] {$types=$_.Exception.Types}
    foreach($type in ($types | Where-Object {$null -ne $_})){
        foreach($method in $type.GetMethods($flags)){
            $isImport=$false
            foreach($attr in $method.GetCustomAttributesData()){ if($attr.Constructor.DeclaringType.Name -eq 'DllImportAttribute' -and $attr.ConstructorArguments[0].Value -eq 'cm3d2'){$isImport=$true} }
            if(-not $isImport){continue}
            foreach($p in $method.GetParameters()){
                $t=$p.ParameterType; if($t.IsByRef){$t=$t.GetElementType()}
                if($t.IsValueType -and -not $t.IsPrimitive -and -not $t.IsEnum -and -not $seen.ContainsKey($t.FullName)){
                    $seen[$t.FullName]=1
                    $layout=''; foreach($ta in $t.GetCustomAttributesData()){ if($ta.Constructor.DeclaringType.Name -eq 'StructLayoutAttribute'){ $layout=' ' + $ta.ToString() } }
                    "STRUCT $($t.FullName)$layout"
                    foreach($f in $t.GetFields([Reflection.BindingFlags]'Public,NonPublic,Instance')){
                        $fa=''; foreach($a in $f.GetCustomAttributesData()){ $fa += ' ' + $a.ToString() }
                        "  FIELD $($f.FieldType.FullName) $($f.Name)$fa"
                    }
                }
            }
        }
    }
}

# Signatures of the delegate types passed to the imports (callbacks the native side invokes)
"----- DELEGATES -----"
$seenD=@{}
foreach($name in 'Assembly-CSharp.dll','Assembly-CSharp-firstpass.dll'){
    $asm=[Reflection.Assembly]::ReflectionOnlyLoadFrom((Join-Path $ManagedRoot $name))
    try {$types=$asm.GetTypes()} catch [Reflection.ReflectionTypeLoadException] {$types=$_.Exception.Types}
    foreach($type in ($types | Where-Object {$null -ne $_})){
        foreach($method in $type.GetMethods($flags)){
            $isImport=$false
            foreach($attr in $method.GetCustomAttributesData()){ if($attr.Constructor.DeclaringType.Name -eq 'DllImportAttribute' -and $attr.ConstructorArguments[0].Value -eq 'cm3d2'){$isImport=$true} }
            if(-not $isImport){continue}
            foreach($p in $method.GetParameters()){
                $t=$p.ParameterType
                if($t.BaseType -ne $null -and $t.BaseType.FullName -eq 'System.MulticastDelegate' -and -not $seenD.ContainsKey($t.FullName)){
                    $seenD[$t.FullName]=1
                    $inv=$t.GetMethod('Invoke'); $ps=@(); foreach($q in $inv.GetParameters()){ $mm=''; foreach($pa in $q.GetCustomAttributesData()){ if($pa.Constructor.DeclaringType.Name -eq 'MarshalAsAttribute'){ $mm=' [MarshalAs '+$pa.ConstructorArguments[0].Value+']' } }; $ps += "$($q.ParameterType.FullName) $($q.Name)$mm" }
                    $ufp=''; foreach($ta in $t.GetCustomAttributesData()){ $ufp += ' ' + $ta.ToString() }
                    "DELEGATE $($t.FullName) ret $($inv.ReturnType.FullName) ($($ps -join '; '))$ufp"
                }
            }
        }
    }
}
