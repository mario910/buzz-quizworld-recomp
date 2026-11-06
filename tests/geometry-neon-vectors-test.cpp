#include "../port/geometry_neon_vectors.h"
#include <cassert>
#include <cstdio>
int main(){
 unsigned rng=0x19387462;auto next=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
 for(unsigned test=0;test<100000;++test){
  uint32_t a[8],k[8],out[8];for(unsigned i=0;i<8;++i){a[i]=next();k[i]=next();}
  auto av=_mm256_loadu_si256((const __m256i*)a),kv=_mm256_loadu_si256((const __m256i*)k);
  unsigned mask=0;for(unsigned i=0;i<8;++i)mask|=(a[i]>>31)<<i;
  assert(unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(av)))==mask);
  unsigned byte_mask=0;auto* bytes=(const unsigned char*)a;
  for(unsigned i=0;i<32;++i)byte_mask|=unsigned(bytes[i]>>7)<<i;
  assert(unsigned(_mm256_movemask_epi8(av))==byte_mask);
  _mm256_storeu_si256((__m256i*)out,_mm256_permutevar8x32_epi32(av,kv));for(unsigned i=0;i<8;++i)assert(out[i]==a[k[i]&7]);
  _mm256_storeu_si256((__m256i*)out,_mm256_sllv_epi32(av,kv));for(unsigned i=0;i<8;++i)assert(out[i]==(k[i]<32?a[i]<<k[i]:0));
  for(unsigned i=0;i<8;++i)k[i]%=65;kv=_mm256_loadu_si256((const __m256i*)k);
  _mm256_storeu_si256((__m256i*)out,_mm256_srlv_epi32(av,kv));for(unsigned i=0;i<8;++i)assert(out[i]==(k[i]<32?a[i]>>k[i]:0));
  _mm256_storeu_si256((__m256i*)out,_mm256_shuffle_epi8(av,kv));
  auto* x=(unsigned char*)a;auto* idx=(unsigned char*)k;auto* y=(unsigned char*)out;
  for(unsigned i=0;i<32;++i)assert(y[i]==(idx[i]&128?0:x[(i&16)+(idx[i]&15)]));
 }
 puts("PASS NEON vectors: 100000 randomized masks, cross-half permutations, byte shuffles and variable shifts");
}
