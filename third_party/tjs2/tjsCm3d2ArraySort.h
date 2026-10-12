// Reconstructed native introsort, not upstream code. See
// rea-tjs-large-sort-evidence.json and docs/tjs-array-sort-reconstruction.md.
#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include "tjsVariant.h"

namespace TJS { namespace cm3d2_sort {
inline void copySwap(tTJSVariant& a, tTJSVariant& b) {
    tTJSVariant temporary(a);
    a = b;
    b = temporary;
}
template<class Compare>
void insertion(tTJSVariant* first, tTJSVariant* last, Compare compare) {
    if(first == last) return;
    for(auto* at=first+1; at!=last; ++at) {
        tTJSVariant item(*at);
        auto* target=at;
        if(compare(item,*first)) {
            while(target!=first) { *target=target[-1]; --target; }
        } else {
            while(target!=first && compare(item,target[-1])) {
                *target=target[-1]; --target;
            }
        }
        *target=item;
    }
}
template<class Compare>
void median3(tTJSVariant* first,tTJSVariant* middle,tTJSVariant* last,Compare compare) {
    // FUN_1800aaee0.
    if(compare(*middle,*first)) copySwap(*middle,*first);
    if(compare(*last,*middle)) {
        copySwap(*last,*middle);
        if(compare(*middle,*first)) copySwap(*middle,*first);
    }
}
template<class Compare>
std::pair<tTJSVariant*,tTJSVariant*> partition(tTJSVariant* first,tTJSVariant* last,Compare compare) {
    // FUN_1800a7b00: median sampling and equal-pivot partition/rotation order.
    auto* pivot=first+(last-first)/2;
    auto* tail=last-1;
    if(tail-first>40) {
        const auto step=(last-first)/8;
        median3(first,first+step,first+2*step,compare);
        median3(pivot-step,pivot,pivot+step,compare);
        median3(tail-2*step,tail-step,tail,compare);
        median3(first+step,pivot,tail-step,compare);
    } else median3(first,pivot,tail,compare);
    auto* pivotEnd=pivot+1;
    while(first<pivot && !compare(pivot[-1],*pivot) && !compare(*pivot,pivot[-1])) --pivot;
    while(pivotEnd<last && !compare(*pivotEnd,*pivot) && !compare(*pivot,*pivotEnd)) ++pivotEnd;
    auto* upper=pivotEnd;
    auto* lower=pivot;
    for(;;) {
        for(;upper<last;++upper) {
            if(compare(*pivot,*upper)) continue;
            if(compare(*upper,*pivot)) break;
            if(pivotEnd!=upper) copySwap(*pivotEnd,*upper);
            ++pivotEnd;
        }
        for(;first<lower;--lower) {
            if(compare(lower[-1],*pivot)) continue;
            if(compare(*pivot,lower[-1])) break;
            --pivot;
            if(pivot!=lower-1) copySwap(*pivot,lower[-1]);
        }
        if(lower==first && upper==last) return {pivot,pivotEnd};
        if(lower==first) {
            if(pivotEnd!=upper) copySwap(*pivot,*pivotEnd);
            ++pivotEnd;
            copySwap(*pivot,*upper);
            ++pivot; ++upper;
        } else if(upper==last) {
            --lower; --pivot;
            if(lower!=pivot) copySwap(*lower,*pivot);
            --pivotEnd;
            copySwap(*pivot,*pivotEnd);
        } else {
            --lower;
            copySwap(*upper,*lower);
            ++upper;
        }
    }
}
template<class Compare>
void heapAdjust(tTJSVariant* first,std::ptrdiff_t hole,std::ptrdiff_t count,
    const tTJSVariant& item,Compare compare) {
    // FUN_1800a9580: move larger children down first, then push saved item up.
    const auto top=hole;
    const auto lastParent=(count-1)/2;
    while(hole<lastParent) {
        auto child=2*hole+2;
        if(compare(first[child],first[child-1])) --child;
        first[hole]=first[child];
        hole=child;
    }
    if(hole==lastParent && count%2==0) {
        first[hole]=first[count-1];
        hole=count-1;
    }
    while(top<hole) {
        const auto parent=(hole-1)/2;
        if(!compare(first[parent],item)) break;
        first[hole]=first[parent]; hole=parent;
    }
    first[hole]=item;
}
template<class Compare>
void heap(tTJSVariant* first,tTJSVariant* last,Compare compare) {
    // FUN_1800a7920 and the pop sequence in FUN_1800a3640.
    auto count=last-first;
    for(auto parent=count/2; parent>0;) {
        --parent;
        const tTJSVariant item(first[parent]);
        heapAdjust(first,parent,count,item,compare);
    }
    while(count>1) {
        --count;
        const tTJSVariant item(first[count]);
        first[count]=first[0];
        heapAdjust(first,0,count,item,compare);
    }
}
template<class Compare>
void introsort(tTJSVariant* first,tTJSVariant* last,std::ptrdiff_t budget,Compare compare) {
    // FUN_1800a3640: recurse on shorter side, iterate on longer side.
    for(;;) {
        if(last-first<=32) { insertion(first,last,compare); return; }
        if(budget<=0) { heap(first,last,compare); return; }
        const auto equal=cm3d2_sort::partition(first,last,compare);
        budget=budget/2+budget/4;
        if(equal.first-first<last-equal.second) {
            introsort(first,equal.first,budget,compare);
            first=equal.second;
        } else {
            introsort(equal.second,last,budget,compare);
            last=equal.first;
        }
    }
}
template<class Compare>
void unstable(tTJSVariant* first,tTJSVariant* last,Compare compare) {
    if(first!=last) introsort(first,last,last-first,compare);
}
} }
