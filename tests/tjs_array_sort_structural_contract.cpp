// Reconstructed-core ownership/heap tests, not native DLL observations.
#include <string>
#include "../third_party/tjs2/tjsCm3d2ArraySort.h"
#include "../third_party/tjs2/tjsCm3d2ArrayStableSort.h"
#include "tjs.h"
#include "tjsObject.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
struct Counts { unsigned adds=0,releases=0,destroyed=0; };
class Object final : public TJS::tTJSDispatch {
public:
    Counts& counts;
    int key;
    Object(Counts& tracked,int k):counts(tracked),key(k) {}
    ~Object() override { ++counts.destroyed; }
    TJS::tjs_uint TJS_INTF_METHOD AddRef() override { ++counts.adds; return tTJSDispatch::AddRef(); }
    TJS::tjs_uint TJS_INTF_METHOD Release() override { ++counts.releases; return tTJSDispatch::Release(); }
};
void forcedHeap() {
    std::vector<TJS::tTJSVariant> data;
    for(int i=0;i<256;++i) data.emplace_back(TJS::tTVInteger((i*17+11)%256));
    const auto compare=[](const auto& a,const auto& b) { return a.AsInteger()<b.AsInteger(); };
    // Zero division budget exercises our heap fallback directly. This is a
    // structural check, not proof that the native runtime probe reached it.
    TJS::cm3d2_sort::introsort(data.data(),data.data()+data.size(),0,compare);
    for(int i=0;i<256;++i) require(data[i].AsInteger()==i,"heap fallback sorted result");
}
void ownership(bool stable,unsigned stop) {
    Counts counts;
    std::vector<Object*> owners;
    {
        std::vector<TJS::tTJSVariant> values;
        for(int i=0;i<256;++i) {
            auto* object=new Object(counts,(i*17+11)%256);
            owners.push_back(object); // retain the initial reference outside sorting
            values.emplace_back(object,object);
        }
        unsigned calls=0;
        const auto compare=[&](const auto& a,const auto& b) {
            if(++calls==stop) throw std::runtime_error("controlled comparator stop");
            return static_cast<Object*>(a.AsObjectNoAddRef())->key <
                static_cast<Object*>(b.AsObjectNoAddRef())->key;
        };
        bool threw=false;
        try {
            if(stable) TJS::cm3d2_sort::stable(values.data(),values.data()+values.size(),compare);
            else TJS::cm3d2_sort::unstable(values.data(),values.data()+values.size(),compare);
        } catch(const std::runtime_error&) { threw=true; }
        require(threw==(stop!=0),"controlled sort exception reached");
        require(counts.destroyed==0,"external owners survive sort and exception cleanup");
        if(!stop) for(int i=0;i<256;++i)
            require(static_cast<Object*>(values[i].AsObjectNoAddRef())->key==i,"sorted object payloads");
    }
    for(auto* object:owners) object->Release();
    require(counts.destroyed==256,"all dispatch objects released after sort scope");
    require(counts.releases==counts.adds+256,"dispatch/receiver references balanced");
}
}
int main() {
    try {
        forcedHeap();
        for(bool stable:{false,true}) for(unsigned stop:{0u,1u,4u,100u,600u}) ownership(stable,stop);
        std::puts("TJS sort structural heap/ownership contracts passed");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"TJS sort structural contract failed: %s\n",error.what());
        return 1;
    }
}
