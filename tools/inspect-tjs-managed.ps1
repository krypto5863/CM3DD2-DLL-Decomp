param([string]$AssemblyName='Assembly-CSharp-firstpass.dll', [string]$TypePattern='^(DLLTJSScript|TJSScript|AFileSystemBase|DLLFileSystem\+Data)$', [string]$MethodPattern='.')
$managedRoot='E:\COM3D2Test 2.5\COM3D2_5\COM3D2x64_Data\Managed'
$handler=[System.ResolveEventHandler]{param($sender,$eventArgs)
    $dllPath=Join-Path $managedRoot (($eventArgs.Name.Split(',')[0])+'.dll')
    if(Test-Path -LiteralPath $dllPath){return [Reflection.Assembly]::ReflectionOnlyLoadFrom($dllPath)}
    return [Reflection.Assembly]::ReflectionOnlyLoad($eventArgs.Name)
}
[AppDomain]::CurrentDomain.add_ReflectionOnlyAssemblyResolve($handler)
Add-Type -TypeDefinition @'
using System;
using System.Reflection;
using System.Reflection.Emit;
using System.Collections.Generic;
public static class ReadOnlyCil {
    static readonly Dictionary<ushort,OpCode> Codes=new Dictionary<ushort,OpCode>();
    static ReadOnlyCil(){foreach(var f in typeof(OpCodes).GetFields(BindingFlags.Public|BindingFlags.Static)){var c=(OpCode)f.GetValue(null);Codes[(ushort)c.Value]=c;}}
    public static string[] Dump(MethodBase m){
        var r=new List<string>(); var b=m.GetMethodBody(); if(b==null)return r.ToArray(); var il=b.GetILAsByteArray(); int p=0;
        while(p<il.Length){int at=p;ushort key=il[p++];if(key==0xfe)key=(ushort)(0xfe00|il[p++]);var op=Codes[key];string arg="";int n=0;
            switch(op.OperandType){
            case OperandType.InlineNone:break;
            case OperandType.ShortInlineI:case OperandType.ShortInlineVar:case OperandType.ShortInlineBrTarget:n=1;arg=il[p].ToString();break;
            case OperandType.InlineVar:n=2;arg=BitConverter.ToUInt16(il,p).ToString();break;
            case OperandType.InlineI:case OperandType.InlineBrTarget:n=4;arg=BitConverter.ToInt32(il,p).ToString();break;
            case OperandType.ShortInlineR:n=4;arg=BitConverter.ToSingle(il,p).ToString();break;
            case OperandType.InlineI8:n=8;arg=BitConverter.ToInt64(il,p).ToString();break;
            case OperandType.InlineR:n=8;arg=BitConverter.ToDouble(il,p).ToString();break;
            case OperandType.InlineSwitch:n=4+4*BitConverter.ToInt32(il,p);arg="switch";break;
            default:n=4;int token=BitConverter.ToInt32(il,p);arg="0x"+token.ToString("x8");try{if(op.OperandType==OperandType.InlineString)arg="\""+m.Module.ResolveString(token)+"\"";else {var member=m.Module.ResolveMember(token,m.DeclaringType.GetGenericArguments(),m.IsGenericMethod?m.GetGenericArguments():null);arg=member.DeclaringType.FullName+"::"+member.ToString();}}catch{}break;
            }
            p+=n;r.Add("IL_"+at.ToString("x4")+": "+op.Name+" "+arg);
        }return r.ToArray();
    }
}
'@
$asm=[Reflection.Assembly]::ReflectionOnlyLoadFrom((Join-Path $managedRoot $AssemblyName))
try {$types=$asm.GetTypes()} catch [Reflection.ReflectionTypeLoadException] {$types=$_.Exception.Types}
$flags=[Reflection.BindingFlags]'Public,NonPublic,Instance,Static,DeclaredOnly'
foreach($type in ($types | Where-Object {$null -ne $_ -and $_.FullName -match $TypePattern})) {
    "TYPE $($type.FullName) TOKEN 0x$('{0:x8}' -f $type.MetadataToken)"
    foreach($field in $type.GetFields($flags)){"FIELD $($field.Name) TYPE $($field.FieldType.FullName) TOKEN 0x$('{0:x8}' -f $field.MetadataToken)"}
    foreach($method in @($type.GetConstructors($flags)) + @($type.GetMethods($flags))){
        if($method.Name -notmatch $MethodPattern){continue}
        "METHOD $($method.Name) TOKEN 0x$('{0:x8}' -f $method.MetadataToken)"
        foreach($attr in $method.GetCustomAttributesData()) {if($attr.Constructor.DeclaringType.Name -eq 'DllImportAttribute'){"IMPORT $attr"}}
        [ReadOnlyCil]::Dump($method)
    }
}
