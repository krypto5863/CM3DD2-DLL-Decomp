// Bounded public API behavior corroborated by the original-DLL text probe.
// Never supplies a filename to Array file methods or Dictionary.saveStruct.
#include "cm3d2/tjs_api.h"
#include "cm3d2/tjs_variant_api.h"
#include <cstdio>
#include <stdexcept>
#include <string>
namespace {
void require(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
CM3D2_TjsScriptHandle script;
CM3D2_TjsVariantHandle value;
void exec(const char* source) { DLL_TJS_ExecScript(&script,source,nullptr); }
void expect(const char* expression,const char* expected) {
    const std::string source="var answer; try { answer=("+std::string(expression)+
        "); } catch(e) { answer=\"ERROR:\"+e.message; }";
    exec(source.c_str());
    DLL_TJS_EvalScript(&script,"answer",value);
    require(DLL_TJSVariant_GetType(&value)==2,"expected result string");
    char text[2048]={};
    DLL_TJSVariant_AsString(&value,text,sizeof(text));
    if(std::string(text)!=expected) {
        std::fprintf(stderr,"expression=%s\nexpected=%s\nactual=%s\n",expression,expected,text);
        throw std::runtime_error("text contract mismatch");
    }
}
}
int main() {
    try {
        DLL_TJS_CreateTjsScriptClass(nullptr,&script);
        DLL_TJSVariant_Create(&value);
        exec("function shape(a) { var s=\"count=\"+a.count; for(var i=0;i<a.count;i++) s+=\"|\"+typeof a[i]+\":\"+a[i]; return s; } function splitProbe(p,s,purge) { var a=[\"old\",123]; var state=\"OK\"; var r; try { r=a.split(p,s,void,purge); state+=\":\"+(r===a); } catch(e) { state=\"ERROR:\"+e.message; } return state+\"|\"+shape(a); }");
        expect("splitProbe(\",;\",\"a;b,c;;\",false)",
            "OK:1|count=5|String:a|String:b|String:c|String:|String:");
        expect("splitProbe(\",;\",\"a;b,c;;\",true)","OK:1|count=3|String:a|String:b|String:c");
        expect("splitProbe(\",\",\"\",false)","OK:1|count=1|String:");
        expect("splitProbe(\",\",\"\",true)","OK:1|count=0");
        expect("splitProbe(\"\",\"alpha\",false)","OK:1|count=1|String:alpha");
        expect("splitProbe(12,121212,true)","OK:1|count=0");
        expect("splitProbe(\"、。\",\"、猫。犬、、\",false)",
            "OK:1|count=5|String:|String:猫|String:犬|String:|String:");
        expect("splitProbe(\",\",<%00 01%>,false)",
            "ERROR:Cannot convert the variable type ((octet)<% 00 01 %> to string)|count=0");
        expect("splitProbe(<%00 01%>,\"a,b\",false)",
            "ERROR:Cannot convert the variable type ((octet)<% 00 01 %> to string)|count=0");
        exec("var a=[\"old\"]; var caught=\"\"; try { a.split(\",\"); } catch(e) { caught=e.message; }");
        expect("caught+\"|\"+shape(a)","Invalid argument count|count=1|String:old");
        exec("a.split(\",\",\"a,,b\",true);");
        expect("shape(a)","count=3|String:a|String:|String:b");
        exec("a.split(\",\",\"a,,b\",<%00%>,true,99);");
        expect("shape(a)","count=2|String:a|String:b");
        expect("[void,\"a\",void,\"\",void,0,false].join(\"|\")","|a||||0|0");
        expect("[void,\"a\",void,\"\",void,0,false].join(\"|\",void,true)","a||0|0");
        expect("[].join(\"|\")","");
        expect("[1,2.5,-3].join(12)","1122.512-3");
        expect("[].join()","ERROR:Invalid argument count");
        expect("(Array.join incontextof %[])(\"|\")","ERROR:Invalid object context");
        expect("(Array.split incontextof %[])(\",\",\"x,y\")","ERROR:Invalid object context");
        exec("a=[1,2]; var bound=a.join;");
        expect("bound(\"|\")","1|2");
        expect("(bound incontextof [3,4])(\"|\")","3|4");
        exec("invalidate a;");
        // Retained native method bypasses property lookup on invalidated this;
        // Array's NI remains queryable with empty items. Native does this too.
        expect("bound(\"|\")","");
        exec("a=[1,2]; var splitBound=a.split; var joinBound=a.join; invalidate a;");
        expect("\"\"+(splitBound(\",\",\"x,y\")===a)","1");
        expect("joinBound(\"|\")","x|y");
        expect("\"\"+(isvalid a)","0");
        expect("[].load()","ERROR:Invalid argument count");
        expect("[].save()","ERROR:Invalid argument count");
        expect("[].saveStruct()","ERROR:Invalid argument count");
        exec("var d=%[\"sentinel\"=>7];");
        expect("typeof (Dictionary.load incontextof d)()","void");
        expect("typeof (Dictionary.save incontextof d)()","void");
        expect("typeof (Dictionary.load incontextof d)(<%00%>,<%01%>,123)","void");
        expect("typeof (Dictionary.save incontextof d)(<%00%>,<%01%>,123)","void");
        expect("\"\"+d.sentinel","7");
        expect("(Dictionary.saveStruct incontextof d)()","ERROR:Invalid argument count");
        expect("(Dictionary.load incontextof [])()","ERROR:Invalid object context");
        exec("var saved=Dictionary.load incontextof d; invalidate d;");
        expect("saved()","ERROR:The object is already invalidated");
        DLL_TJSVariant_Delete(&value);
        DLL_TJS_DeleteTjsScriptClass(&script);
        std::puts("TJS array text/stub contracts passed"); return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"TJS array text: %s\n",e.what()); return 1;
    }
}
