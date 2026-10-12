// The script shapes the game's managed ScriptManager generates (ScriptBridge CreateTjsScript with G9 numbers, global.tf
// replacement, utility classes from startup.tjs) plus number/string conversions, through the ORIGINAL or the reconstructed DLL.
// Usage: tjs_bridge_probe.exe <dll> <startup folder with startup.tjs, utils.tjs, game_utils.tjs>
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct FsHandle { void* object; int kind; int reserved; };
static HMODULE m;
template<class T> T fn(const char* n) { return reinterpret_cast<T>(GetProcAddress(m, n)); }
static void* g_tjs = nullptr;

static void __cdecl console(const char* message, uint8_t exception) {
  std::string t = message, out;
  for (size_t i = 0; i < t.size();) {
    if (t[i] == '0' && i + 1 < t.size() && t[i + 1] == 'x') {
      size_t j = i + 2; while (j < t.size() && isxdigit(static_cast<unsigned char>(t[j]))) ++j;
      if (j - (i + 2) >= 8) { out += "0xADDR"; i = j; continue; }
    }
    out.push_back(t[i++]);
  }
  for (auto& c : out) if (c == '\n') c = '|';
  std::printf("  console %s %s\n", exception ? "E" : "N", out.c_str());
}

static void show(const char* label, void* result) {
  void* view = nullptr;
  fn<void(__cdecl*)(void**, void*)>("DLL_TJSVariant_Reference")(&view, result);
  const int type = fn<int(__cdecl*)(void**)>("DLL_TJSVariant_GetType")(&view);
  std::vector<char> text(8192, 0);
  fn<void(__cdecl*)(void**, char*, int)>("DLL_TJSVariant_AsString")(&view, text.data(), static_cast<int>(text.size()));
  std::string s = text.data();
  for (auto& c : s) if (c == '\n') c = '|';
  std::printf("%s => type=%d '%s'\n", label, type, s.c_str());
}
static void eval(const char* expr) {
  void* result = nullptr;
  fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Create")(&result);
  std::printf("eval %s\n", expr);
  fn<void(__cdecl*)(void**, const char*, void*)>("DLL_TJS_EvalScript")(&g_tjs, expr, result);
  show("  ", result);
  fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Delete")(&result);
}
static void exec(const char* script) {
  void* result = nullptr;
  fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Create")(&result);
  std::printf("exec %s\n", script);
  fn<void(__cdecl*)(void**, const char*, void*)>("DLL_TJS_ExecScript")(&g_tjs, script, result);
  fn<void(__cdecl*)(void**)>("DLL_TJSVariant_Delete")(&result);
}

int main(int, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  m = LoadLibraryA(argv[1]);
  FsHandle handle{};
  fn<void(__cdecl*)(void*)>("DLL_FileSystem_CreateFileSystemWindows")(&handle);
  fn<uint8_t(__cdecl*)(void*, const char*)>("DLL_FileSystem_AddFolder")(&handle, argv[2]);
  void* fs = fn<void*(__cdecl*)(void*)>("DLL_FileSystem_GetInterfaceFileSystemWidePointer")(&handle);
  fn<void(__cdecl*)(void*, void**)>("DLL_TJS_CreateTjsScriptClass")(fs, &g_tjs);
  fn<void(__cdecl*)(void**, void*)>("DLL_TJS_CreateTjsConsoleClass")(&g_tjs, (void*)console);
  exec("Scripts.execStorage('startup.tjs');");
  eval("typeof tf");
  // ScriptBridge shapes (numbers formatted like .NET "G9")
  exec("tf['k'] = new CameraTargetValue(); tf['k'].center = new Vector3(1.5,2.25,-3.125E-05); tf['k'].radius = 1E+10; tf['k'].aroundAngle = new Vector2(0.1,3.40282347E+38);");
  eval("tf['k'].ToJson()");
  exec("tf['t'] = new TransformValue(); tf['t'].position = new Vector3(1,2,3); tf['t'].localPosition = new Vector3(-0,1.401298E-45,-1.17549435E-38); tf['t'].rotation = new Vector4(0.5,0.5,0.5,0.5); tf['t'].localRotation = new Vector4(0,0,0,1); tf['t'].localScale = new Vector3(1,1,1);");
  eval("tf['t'].ToJson()");
  eval("tf['t'].Clone().ToJson()");
  eval("tf['k'].Clone().ToJson()");
  exec("tf['n'] = new Vector3(NaN,Infinity,-Infinity);");
  eval("tf['n'].ToString()");
  exec("global.tf = new Dictionary();");
  eval("tf['k']");
  eval("typeof tf['k']");
  eval("(Dictionary.getKeyArray incontextof tf)().count");
  // utility functions from game_utils.tjs
  eval("GetRandomScenarioName(['a','b','c'])");
  eval("GetRandomScenarioName('x')");
  // number and string conversions
  const char* exprs[] = {
    "''+0.1", "''+(0.1+0.2)", "''+1e21", "''+1e-7", "''+123456789012", "''+(1/3)", "''+(2/3)", "''+1e15", "''+1e16", "''+100000000000000000000",
    "''+0.000001", "''+0.0000001", "''+(-0.0)", "''+3.4028235E+38", "''+1.5E-5", "''+1.0", "''+5.", "''+.5", "int(3.99)", "int(-3.99)",
    "real('1.5e3')", "real('abc')", "int('0x1f')", "int('12abc')", "(string)255", "0x1f", "1/0", "-1/0", "0/0", "''+(1/0)", "''+(0/0)",
    "'abc'.substr(1,1)", "'abc'.substring(1)", "'a,b,c'.split(',').count", "'abc'.length", "'ABC'.toLowerCase()", "'abc'.indexOf('c')",
    "string(1.5)", "string(100)", "1+'2'", "'3'*'4'", "1<<40", "1>>1", "7%3", "-7%3", "7.5%2", "5\2", "2**10", "Math.floor(2.5)", "Math.round(2.5)",
    "Math.sqrt(2)", "Math.sin(1)", "Math.pow(2,0.5)", "Math.PI", "intrandom(5,5)", "[1,2,3].join('-')", "(function(a,b){return a+b;})(1,2)",
    "typeof 1", "typeof 1.0", "typeof 'a'", "typeof void", "typeof %[]", "typeof []", "void", "null", "true+1", "'a'<'b'", "'10'<'9'", "10<'9'",
    "int(1e10)", "int(2147483648)", "int(-2147483649)", "1e10|0", "0x7fffffff+1", "9223372036854775807", "9223372036854775807+1",
    "%['a'=>1,'b'=>2].a", "(Dictionary.toStructString incontextof %['x'=>[1,2,%['y'=>'z']]])()", "(Array.toStructString == void)",
  };
  for (auto e : exprs) eval(e);
  exec("function bridgeCallback(a) { return a * 2; } var __r = bridgeCallback(21);");
  eval("__r");
  return 0;
}
