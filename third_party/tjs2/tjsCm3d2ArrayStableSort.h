#ifndef TJS_CM3D2_ARRAY_STABLE_SORT_H
#define TJS_CM3D2_ARRAY_STABLE_SORT_H

#include <cstddef>
#include <string>
#include <vector>
#include "tjsVariant.h"

// Reconstructed from cm3d2.dll's FUN_1800a3530 / a76d0 / a77f0,
// a8d80 / a8f20 / a90d0 / a9240 (rea-tjs-large-sort-evidence.json).
// This is the normal sufficient-buffer path, not the native allocator's
// 204-slot inline/reduced-allocation fallback. RAII deliberately releases
// temporary payloads on comparator exceptions. Native aggregate error runs
// failed, but their root cause is unknown; exception cleanup parity is not
// claimed. The array's writes are not rolled back.
namespace TJS { namespace cm3d2_sort {
namespace stable_detail {

template<class Compare>
void insertion(tTJSVariant *first, tTJSVariant *last, Compare &compare)
{
    if(first == last) return;
    for(tTJSVariant *at = first + 1; at != last; ++at)
    {
        tTJSVariant item(*at);
        tTJSVariant *target = at;
        if(compare(item, *first))
            while(target != first) { *target = *(target - 1); --target; }
        else
            while(target != first && compare(item, *(target - 1)))
            { *target = *(target - 1); --target; }
        *target = item;
    }
}

inline void clear(tTJSVariant *first, tTJSVariant *last)
{
    for(; first != last; ++first) first->Clear();
}

// Merge adjacent sorted runs from one nonoverlapping range into another.
// Native first pass copy-constructs uninitialized buffer cells; subsequent
// passes assign them. Default-void RAII cells give equivalent payload retains.
template<class Compare>
void passes(tTJSVariant *first, tTJSVariant *last, tTJSVariant *output,
            std::ptrdiff_t width, Compare &compare)
{
    while(first != last)
    {
        const std::ptrdiff_t remaining = last - first;
        if(remaining <= width)
        {
            while(first != last) *output++ = *first++;
            return;
        }
        tTJSVariant *middle = first + width;
        tTJSVariant *end = middle + ((last - middle < width) ? last - middle : width);
        tTJSVariant *left = first, *right = middle;
        while(left != middle && right != end)
        {
            if(compare(*right, *left)) *output++ = *right++;
            else *output++ = *left++;
        }
        while(left != middle) *output++ = *left++;
        while(right != end) *output++ = *right++;
        first = end;
    }
}

template<class Compare>
void chunks(tTJSVariant *first, tTJSVariant *last,
            tTJSVariant *buffer, Compare &compare)
{
    const std::ptrdiff_t count = last - first;
    tTJSVariant *at = first;
    while(last - at > 32) { insertion(at, at + 32, compare); at += 32; }
    insertion(at, last, compare);
    if(count <= 32) return;
    passes(first, last, buffer, 32, compare);
    passes(buffer, buffer + count, first, 64, compare);
    // The native alternates buffer/source passes in pairs and always finishes
    // with the sorted values in the original array.
    for(std::ptrdiff_t width = 128; width < count; )
    {
        passes(first, last, buffer, width, compare);
        passes(buffer, buffer + count, first, width * 2, compare);
        if(width > count / 4) break;
        width *= 4;
    }
    clear(buffer, buffer + count);
}

inline void rotate_left_one(tTJSVariant *first, tTJSVariant *middle,
                            tTJSVariant *last)
{
    tTJSVariant item(*first);
    while(middle != last) *first++ = *middle++;
    *first = item;
}

inline void rotate_right_one(tTJSVariant *first, tTJSVariant *middle,
                             tTJSVariant *last)
{
    tTJSVariant item(*middle);
    while(middle != first) *--last = *--middle;
    *first = item;
}

template<class Compare>
void merge(tTJSVariant *first, tTJSVariant *middle, tTJSVariant *last,
           tTJSVariant *buffer, Compare &compare)
{
    if(first == middle || middle == last) return;
    // Skip the already ordered prefix; the first right value then precedes
    // the first left value. No redundant predicate validation is performed.
    while(!compare(*middle, *first))
        if(++first == middle) return;
    // Skip the ordered suffix, preserving the largest left value as sentinel.
    for(;;)
    {
        tTJSVariant *tail = last - 1;
        if(tail == middle) { rotate_right_one(first, middle, last); return; }
        if(compare(*tail, *(middle - 1))) break;
        last = tail;
    }
    const std::ptrdiff_t left_count = middle - first;
    const std::ptrdiff_t right_count = last - middle;
    if(left_count == 1) { rotate_left_one(first, middle, last); return; }
    if(left_count <= right_count)
    {
        tTJSVariant *buffer_end = buffer + left_count;
        for(std::ptrdiff_t i = 0; i != left_count; ++i) buffer[i] = first[i];
        *first = *middle++;
        tTJSVariant *out = first + 1, *left = buffer;
        while(left != buffer_end - 1)
        {
            if(compare(*middle, *left))
            {
                *out++ = *middle++;
                if(middle == last)
                {
                    while(left != buffer_end) *out++ = *left++;
                    clear(buffer, buffer_end);
                    return;
                }
            }
            else *out++ = *left++;
        }
        while(middle != last) *out++ = *middle++;
        *out = *left;
        clear(buffer, buffer_end);
    }
    else
    {
        tTJSVariant *buffer_end = buffer + right_count;
        for(std::ptrdiff_t i = 0; i != right_count; ++i) buffer[i] = middle[i];
        tTJSVariant *left = middle - 1, *right = buffer_end - 1;
        tTJSVariant *out = last - 1;
        *out-- = *left--;
        for(;;)
        {
            if(compare(*right, *left))
            {
                *out-- = *left;
                if(left == first) break;
                --left;
            }
            else
            {
                *out-- = *right--;
                if(right == buffer)
                {
                    for(;;)
                    {
                        *out-- = *left;
                        if(left == first) break;
                        --left;
                    }
                    *first = *buffer;
                    clear(buffer, buffer_end);
                    return;
                }
            }
        }
        while(right != buffer) *out-- = *right--;
        *out = *right;
        clear(buffer, buffer_end);
    }
}

} // namespace stable_detail

template<class Compare>
void stable(tTJSVariant *first, tTJSVariant *last, Compare compare)
{
    const std::ptrdiff_t count = last - first;
    if(count <= 32) { stable_detail::insertion(first, last, compare); return; }
    const std::ptrdiff_t left_count = count - count / 2;
    std::vector<tTJSVariant> buffer(static_cast<std::size_t>(left_count));
    tTJSVariant *middle = first + left_count;
    stable_detail::chunks(first, middle, buffer.data(), compare);
    stable_detail::chunks(middle, last, buffer.data(), compare);
    stable_detail::merge(first, middle, last, buffer.data(), compare);
}

} } // namespace TJS::cm3d2_sort
#endif
