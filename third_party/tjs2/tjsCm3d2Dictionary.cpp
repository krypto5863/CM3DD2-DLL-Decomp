// Reconstructed cm3d2 additions, not upstream TJS source.
// Native evidence: rea-tjs-builtins-evidence.json, see docs/tjs-builtins-reconstruction.md.
#include "tjsCommHead.h"
#include "tjsDictionary.h"
#include "tjsArray.h"
#include "tjsUtils.h"
#include <algorithm>

namespace TJS {
namespace {
ttstr quote(const ttstr& text) {
    std::wstring out = L"\"";
    for(const tjs_char* p=text.c_str(); *p; ++p) {
        if(*p == L'"' || *p == L'\\') out.push_back(L'\\');
        out.push_back(*p);
    }
    out.push_back(L'"');
    return ttstr(out.c_str());
}
ttstr expression(const tTJSVariant& value, std::vector<iTJSDispatch2*>& path);
class StructMembers final : public tTJSDispatch {
public:
    ttstr text = TJS_W("(const)%[");
    bool first = true;
    std::vector<iTJSDispatch2*>& path;
    explicit StructMembers(std::vector<iTJSDispatch2*>& active) : path(active) {}
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*,
        tTJSVariant* result, tjs_int count, tTJSVariant** args, iTJSDispatch2*) override {
        // FUN_1800b3e10: hidden members do not alter first/separators.
        if(count >= 3 && !(args[1]->AsInteger() & TJS_HIDDENMEMBER)) {
            if(!first) text += TJS_W(",");
            first = false;
            text += quote(ttstr(*args[0]));
            text += TJS_W(",");
            text += expression(*args[2], path);
        }
        if(result) *result = tjs_int(1);
        return TJS_S_OK;
    }
};
ttstr expression(const tTJSVariant& value, std::vector<iTJSDispatch2*>& path) {
    if(value.Type() == tvtString) return quote(ttstr(value));
    if(value.Type() == tvtReal) {
        // FUN_1800ea150 calls the hex formatter only, without vendor's comment.
        auto* formatted = TJSRealToHexString(value.AsReal());
        ttstr out;
        try { out = formatted; }
        catch(...) { if(formatted) formatted->Release(); throw; }
        if(formatted) formatted->Release();
        return out;
    }
    if(value.Type() == tvtOctet) {
        ttstr out(TJS_W("<%"));
        const auto* octet = value.AsOctetNoAddRef();
        if(octet) {
            const tjs_char digits[] = TJS_W("0123456789abcdef");
            for(tjs_uint i=0; i<octet->GetLength(); ++i) {
                const auto byte = octet->GetData()[i];
                const tjs_char item[] = {digits[byte >> 4], digits[byte & 15], L' ', 0};
                out += item;
            }
        }
        out += TJS_W("%>");
        return out;
    }
    if(value.Type() != tvtObject) return TJSVariantToExpressionString(value);
    // Native selects the object pointer, not the closure receiver.
    iTJSDispatch2* object = value.AsObjectNoAddRef();
    if(!object) return ttstr(TJS_W("null")); // safe normalization of native unchecked access
    tTJSArrayNI* array = nullptr;
    tTJSDictionaryNI* dictionary = nullptr;
    if(TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
        TJSGetArrayClassID(), reinterpret_cast<iTJSNativeInstance**>(&array)))) array = nullptr;
    if(!array && TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
        TJSGetDictionaryClassID(), reinterpret_cast<iTJSNativeInstance**>(&dictionary)))) dictionary = nullptr;
    if(!array && !dictionary) return ttstr(); // native omits unsupported object text
    // Native recurses unchecked; reject cycles/deep structures rather than crash.
    if(path.size() >= 128 || std::find(path.begin(), path.end(), object) != path.end())
        TJS_eTJSError(TJS_W("Unsupported cyclic or excessively deep struct data"));
    path.push_back(object);
    struct Pop { std::vector<iTJSDispatch2*>& path; ~Pop() { path.pop_back(); } } pop{path};
    if(array) {
        ttstr out(TJS_W("(const)["));
        bool first = true;
        for(const auto& item : array->Items) {
            if(!first) out += TJS_W(",");
            first = false;
            out += expression(item, path);
        }
        out += TJS_W("]");
        return out;
    }
    StructMembers members(path);
    tTJSVariantClosure callback(&members, nullptr);
    object->EnumMembers(TJS_IGNOREPROP, &callback, object);
    members.text += TJS_W("]");
    return members.text;
}
class KeyMembers final : public tTJSDispatch {
public:
    iTJSDispatch2* array;
    tjs_int index = 0;
    explicit KeyMembers(iTJSDispatch2* output) : array(output) {}
    tjs_error TJS_INTF_METHOD FuncCall(tjs_uint32, const tjs_char*, tjs_uint32*,
        tTJSVariant* result, tjs_int count, tTJSVariant** args, iTJSDispatch2*) override {
        if(result) *result = tjs_int(1);
        if(count >= 2) {
            if(!(args[1]->AsInteger() & TJS_HIDDENMEMBER)) {
                tTJSVariant name{ttstr(*args[0])};
                array->PropSetByNum(TJS_MEMBERENSURE, index, &name, array);
            }
            ++index; // FUN_1800b2860 increments even for hidden members.
        }
        return TJS_S_OK;
    }
};
}
ttstr tTJSDictionaryNI::ToStructString() {
    std::vector<iTJSDispatch2*> path;
    return expression(tTJSVariant(Owner, Owner), path);
}
iTJSDispatch2* tTJSDictionaryNI::GetKeyArray() {
    iTJSDispatch2* array = TJSCreateArrayObject();
    try {
        KeyMembers members(array);
        tTJSVariantClosure callback(&members, nullptr);
        Owner->EnumMembers(TJS_IGNOREPROP, &callback, Owner);
    } catch(...) { array->Release(); throw; }
    return array;
}
}
