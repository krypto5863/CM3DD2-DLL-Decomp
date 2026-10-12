#pragma once
#include "synthetic_warc.h"
#include <bcrypt.h>
#include <array>
#include <cstring>

namespace synthetic_warc {
// Independent writer for the statically recovered CBC carrier, not a decoder.
inline std::array<unsigned char,16> aesSeed(std::uint32_t seed) {
    std::uint32_t a=0x075bcd15U,b=0x159a55e5U,c=0x1f123bb5U,d=seed;
    std::array<unsigned char,16> out{};
    for (int i=0;i<4;++i) {
        const auto t=a^(a<<11);
        const auto next=d^t^(((d>>11)^t)>>8);
        a=b;b=c;c=d;d=next;
        for (int j=0;j<4;++j) out[i*4+j]=static_cast<unsigned char>(next>>(j*8));
    }
    return out;
}
inline Bytes aesCarrier(Bytes input, std::uint32_t decoded, std::uint32_t seed) {
    const auto pad=(16-input.size()%16)%16;
    input.resize(input.size()+pad,0);
    Bytes out(input.size());
    if (!input.empty()) {
        auto key=aesSeed(decoded),iv=aesSeed(seed);
        BCRYPT_ALG_HANDLE algorithm=nullptr;
        BCRYPT_KEY_HANDLE handle=nullptr;
        ULONG written=0;
        bool ok=false;
        if (BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_AES_ALGORITHM,nullptr,0)>=0 &&
            BCryptSetProperty(algorithm,BCRYPT_CHAINING_MODE,
                reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                sizeof(BCRYPT_CHAIN_MODE_CBC),0)>=0 &&
            BCryptGenerateSymmetricKey(algorithm,&handle,nullptr,0,key.data(),16,0)>=0)
            ok=BCryptEncrypt(handle,input.data(),static_cast<ULONG>(input.size()),nullptr,
                iv.data(),16,out.data(),static_cast<ULONG>(out.size()),&written,0)>=0 && written==out.size();
        if (handle) BCryptDestroyKey(handle);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
        if (!ok) throw std::runtime_error("fixture AES encryption failed");
    }
    const auto trailer=seed^0xbfbfbfbfU;
    out.push_back(static_cast<unsigned char>(pad)^static_cast<unsigned char>(trailer));
    u32(out,trailer);
    return out;
}
inline std::vector<Member> aesMembers() {
    std::vector<Member> members;
    for (const int size : {0,1,7,15,16,17,31,32,256}) {
        Bytes data(size);
        for (int i=0;i<size;++i) data[i]=static_cast<unsigned char>(i*37+size);
        for (int compressed=0;compressed<2;++compressed) {
            Member m{"aes"+std::to_string(size)+"c"+std::to_string(compressed)+".bin",data,compressed!=0};
            m.encoded_override=aesCarrier(compressed?zlibStored(data):data,size,
                0x12345678U+static_cast<std::uint32_t>(size*2+compressed));
            m.crypt_override=2;
            members.push_back(std::move(m));
        }
    }
    return members;
}
} // namespace synthetic_warc
