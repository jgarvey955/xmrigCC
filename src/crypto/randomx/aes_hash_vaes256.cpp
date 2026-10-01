/*
Copyright (c) 2018-2019, tevador <tevador@gmail.com>
Copyright (c) 2026 XMRig       <support@xmrig.com>
Copyright (c) 2026 SChernykh   <https://github.com/SChernykh>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in the
	  documentation and/or other materials provided with the distribution.
	* Neither the name of the copyright holder nor the
	  names of its contributors may be used to endorse or promote products
	  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <cstddef>
#include <cstdint>
#include <immintrin.h>

#define REVERSE_4(A, B, C, D) D, C, B, A

alignas(64) static const uint32_t AES_HASH_1R_STATE[] = {
	REVERSE_4(0xd7983aad, 0xcc82db47, 0x9fa856de, 0x92b52c0d),
	REVERSE_4(0xace78057, 0xf59e125a, 0x15c7b798, 0x338d996e),
	REVERSE_4(0xe8a07ce4, 0x5079506b, 0xae62c7d0, 0x6a770017),
	REVERSE_4(0x7e994948, 0x79a10005, 0x07ad828d, 0x630a240c)
};

alignas(64) static const uint32_t AES_GEN_1R_KEY[] = {
	REVERSE_4(0xb4f44917, 0xdbb5552b, 0x62716609, 0x6daca553),
	REVERSE_4(0x0da1dc4e, 0x1725d378, 0x846a710d, 0x6d7caf07),
	REVERSE_4(0x3e20e345, 0xf4c0794f, 0x9f947ec6, 0x3f1262f1),
	REVERSE_4(0x49169154, 0x16314c88, 0xb1ba317c, 0x6aef8135)
};

alignas(64) static const uint32_t AES_HASH_1R_XKEY0[] = {
	REVERSE_4(0x06890201, 0x90dc56bf, 0x8b24949f, 0xf6fa8389),
	REVERSE_4(0x06890201, 0x90dc56bf, 0x8b24949f, 0xf6fa8389),
	REVERSE_4(0x06890201, 0x90dc56bf, 0x8b24949f, 0xf6fa8389),
	REVERSE_4(0x06890201, 0x90dc56bf, 0x8b24949f, 0xf6fa8389)
};

alignas(64) static const uint32_t AES_HASH_1R_XKEY1[] = {
	REVERSE_4(0xed18f99b, 0xee1043c6, 0x51f4e03c, 0x61b263d1),
	REVERSE_4(0xed18f99b, 0xee1043c6, 0x51f4e03c, 0x61b263d1),
	REVERSE_4(0xed18f99b, 0xee1043c6, 0x51f4e03c, 0x61b263d1),
	REVERSE_4(0xed18f99b, 0xee1043c6, 0x51f4e03c, 0x61b263d1)
};

void hashAndFillAes1Rx4_VAES256(void *scratchpad, size_t size, void *hash, void *state) {
 auto *p = static_cast<unsigned char *>(scratchpad);
 __m256i enc[2], dec[2], key[2];
 for (int i=0;i<2;++i) {
  const auto h=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(AES_HASH_1R_STATE)+i);
  const auto f=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(state)+i);
  key[i]=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(AES_GEN_1R_KEY)+i);
  enc[i]=_mm256_blend_epi32(h,f,0xf0);
  dec[i]=_mm256_blend_epi32(f,h,0xf0);
 }
 for(size_t n=0;n<size;n+=64) {
  for (int i=0;i<2;++i) {
   const auto d=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p+n)+i);
   enc[i]=_mm256_aesenc_epi128(enc[i],_mm256_blend_epi32(d,key[i],0xf0));
   dec[i]=_mm256_aesdec_epi128(dec[i],_mm256_blend_epi32(key[i],d,0xf0));
   _mm256_storeu_si256(reinterpret_cast<__m256i*>(p+n)+i,_mm256_blend_epi32(dec[i],enc[i],0xf0));
  }
  _mm_prefetch(reinterpret_cast<const char*>(p+(n+7168<size?n+7168:n+7168-size)),_MM_HINT_T0);
 }
 const auto k0=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(AES_HASH_1R_XKEY0));
 const auto k1=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(AES_HASH_1R_XKEY1));
 for (int i=0;i<2;++i) {
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(state)+i,_mm256_blend_epi32(dec[i],enc[i],0xf0));
  enc[i]=_mm256_aesenc_epi128(_mm256_aesenc_epi128(enc[i],k0),k1);
  dec[i]=_mm256_aesdec_epi128(_mm256_aesdec_epi128(dec[i],k0),k1);
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(hash)+i,_mm256_blend_epi32(enc[i],dec[i],0xf0));
 }
 _mm256_zeroupper();
}
