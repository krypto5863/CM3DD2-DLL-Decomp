// Structural checks of the reconstructed facade and vendored core, NOT a
// native-DLL oracle. Native parity is covered separately by tjs_payload_probe.
#include "../src/tjs_variant_internal.h"
#include "tjs.h"
#include "tjsObject.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
void require(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
struct Counts { unsigned adds=0,releases=0,destroyed=0; };
class CountingDispatch final : public TJS::tTJSDispatch {
public:
    explicit CountingDispatch(Counts& counts) : counts_(counts) {}
    ~CountingDispatch() override { ++counts_.destroyed; }
    TJS::tjs_uint TJS_INTF_METHOD AddRef() override {
        ++counts_.adds; return tTJSDispatch::AddRef();
    }
    TJS::tjs_uint TJS_INTF_METHOD Release() override {
        ++counts_.releases; return tTJSDispatch::Release();
    }
    TJS::tjs_uint refs() { return GetRefCount(); }
private:
    Counts& counts_;
};
using cm3d2::internal::TjsVariant;
void distinctClosure() {
    Counts oc,tc;
    auto* object=new CountingDispatch(oc);
    auto* receiver=new CountingDispatch(tc);
    {
        TjsVariant original;
        original.native()=TJS::tTJSVariant(object,receiver);
        require(object->refs()==2 && receiver->refs()==2,"retain both closure pointers");
        {
            TjsVariant copy(original);
            require(object->refs()==3 && receiver->refs()==3,"owned copy retains both pointers");
            copy.native()=copy.native();
            require(object->refs()==3 && receiver->refs()==3,"self-assignment preserves closure");
            original.native().Clear();
            require(object->refs()==2 && receiver->refs()==2,"Clear releases original closure");
            copy.native()=TJS::tTVInteger(7);
            require(object->refs()==1 && receiver->refs()==1,"overwrite releases copied closure");
        }
    }
    object->Release(); receiver->Release();
    require(oc.destroyed==1 && tc.destroyed==1,"distinct dispatch lifetime balanced");
    require(oc.releases==oc.adds+1 && tc.releases==tc.adds+1,"distinct dispatch reference counts balanced");
}
void duplicateClosure() {
    Counts counts;
    auto* object=new CountingDispatch(counts);
    {
        TjsVariant original;
        original.native()=TJS::tTJSVariant(object,object);
        require(object->refs()==3,"same pointer retained twice");
        TjsVariant copy;
        copy.native()=original.native();
        require(object->refs()==5,"same pointer copied twice");
        original.native()=original.native();
        require(object->refs()==5,"same pointer self-assignment");
        original.native().Clear();
        require(object->refs()==3,"same pointer Clear releases twice");
        copy.native().Clear();
        require(object->refs()==1,"same pointer copy Clear releases twice");
    }
    object->Release();
    require(counts.destroyed==1 && counts.releases==counts.adds+1,"duplicate closure references balanced");
}
void borrowedViews() {
    Counts counts;
    auto* object=new CountingDispatch(counts);
    TJS::tTJSVariant slot(object,object);
    const auto adds=counts.adds,releases=counts.releases;
    {
        TjsVariant view(&slot);
        TjsVariant alias(&slot);
        require(&view.native()==&slot && &alias.native()==&slot,"views point to live VM slot");
    }
    require(slot.Type()==TJS::tvtObject,"borrowed destructor does not clear VM slot");
    require(counts.adds==adds && counts.releases==releases,"borrowed construction/destruction does not retain or release");
    {
        TjsVariant view(&slot),alias(&slot);
        view.native().Clear();
        require(alias.native().Type()==TJS::tvtVoid,"Clear visible through second borrowed view");
        require(object->refs()==1,"borrowed Clear releases slot closure exactly once");
        view.native()=TJS::tTVInteger(123);
        require(alias.native().AsInteger()==123,"mutation visible through borrowed view");
    }
    require(slot.AsInteger()==123,"mutated borrowed slot survives facade destruction");
    object->Release();
    require(counts.destroyed==1,"borrowed slot dispatch balanced");
}
void octets() {
    const TJS::tjs_uint8 bytes[]={0,127,128,255};
    TjsVariant survivor;
    const TJS::tTJSVariantOctet* payload=nullptr;
    {
        TjsVariant original;
        original.native()=TJS::tTJSVariant(bytes,4);
        payload=original.native().AsOctetNoAddRef();
        survivor.native()=original.native();
        require(survivor.native().AsOctetNoAddRef()==payload,"octet copy shares retained payload");
        survivor.native()=survivor.native();
        original.native().Clear();
        require(payload->GetLength()==4 && std::memcmp(payload->GetData(),bytes,4)==0,"copied octet survives source Clear");
        {
            TjsVariant borrowed(&survivor.native());
            require(borrowed.native().AsOctetNoAddRef()==payload,"borrowed octet views same payload");
        }
        require(survivor.native().AsOctetNoAddRef()==payload,"borrowed octet destructor preserves value");
    }
    require(payload->GetLength()==4 && std::memcmp(payload->GetData(),bytes,4)==0,"octet survives source scope");
    survivor.native()=TJS::tTVInteger(9);
    require(survivor.native().Type()==TJS::tvtInteger,"octet overwrite releases old payload");
}
}
int main() {
    try {
        distinctClosure(); duplicateClosure(); borrowedViews(); octets();
        std::puts("TJS payload lifetime structural contracts passed"); return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"TJS payload lifetime: %s\n",e.what()); return 1;
    }
}
