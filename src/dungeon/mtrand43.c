// Mersenne Twister engine with N=4, M=3
// Derived from mt19937int.c in libsys4

/* A C-program for MT19937: Integer version (1998/4/6)            */
/*  genrand() generates one pseudorandom unsigned integer (32bit) */
/* which is uniformly distributed among 0 to 2^32-1  for each     */
/* call. sgenrand(seed) set initial values to the working area    */
/* of 624 words. Before genrand(), sgenrand(seed) must be         */
/* called once. (seed is any 32-bit integer except for 0).        */
/*   Coded by Takuji Nishimura, considering the suggestions by    */
/* Topher Cooper and Marc Rieffel in July-Aug. 1997.              */

/* This library is free software; you can redistribute it and/or   */
/* modify it under the terms of the GNU Library General Public     */
/* License as published by the Free Software Foundation; either    */
/* version 2 of the License, or (at your option) any later         */
/* version.                                                        */
/* This library is distributed in the hope that it will be useful, */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of  */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.            */
/* See the GNU Library General Public License for more details.    */
/* You should have received a copy of the GNU Library General      */
/* Public License along with this library; if not, write to the    */
/* Free Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA   */ 
/* 02111-1307  USA                                                 */

/* Copyright (C) 1997 Makoto Matsumoto and Takuji Nishimura.       */
/* When you use this, send an email to: matumoto@math.keio.ac.jp   */
/* with an appropriate reference to your work.                     */

/* REFERENCE                                                       */
/* M. Matsumoto and T. Nishimura,                                  */
/* "Mersenne Twister: A 623-Dimensionally Equidistributed Uniform  */
/* Pseudo-Random Number Generator",                                */
/* ACM Transactions on Modeling and Computer Simulation,           */
/* Vol. 8, No. 1, January 1998, pp 3--30.                          */

#include "dungeon/mtrand43.h"

/* Period parameters */  
#define N MTRAND43_STATE_SIZE
#define M 3
#define MATRIX_A 0x9908b0df   /* constant vector a */
#define UPPER_MASK 0x80000000 /* most significant w-r bits */
#define LOWER_MASK 0x7fffffff /* least significant r bits */

/* Tempering parameters */   
#define TEMPERING_MASK_B 0x9d2c5680
#define TEMPERING_MASK_C 0xefc60000
#define TEMPERING_SHIFT_U(y)  (y >> 11)
#define TEMPERING_SHIFT_S(y)  (y << 7)
#define TEMPERING_SHIFT_T(y)  (y << 15)
#define TEMPERING_SHIFT_L(y)  (y >> 18)

/* initializing the array with a NONZERO seed */
void mtrand43_init(struct mtrand43 *mt, uint32_t seed)
{
    /* setting initial seeds to mt[N] using         */
    /* the generator Line 25 of Table 1 in          */
    /* [KNUTH 1981, The Art of Computer Programming */
    /*    Vol. 2 (2nd Ed.), pp102]                  */
    mt->st[0]= seed & 0xffffffff;
    for (int i=1; i<N; i++)
        mt->st[i] = (69069 * mt->st[i-1]) & 0xffffffff;
    mt->i = N;
}

uint32_t mtrand43_genrand(struct mtrand43 *mt)
{
    uint32_t y;
    static const uint32_t mag01[2]={0x0, MATRIX_A};
    /* mag01[x] = x * MATRIX_A  for x=0,1 */

    if (mt->i >= N) { /* generate N words at one time */
        int kk;

        for (kk=0;kk<N-M;kk++) {
            y = (mt->st[kk]&UPPER_MASK)|(mt->st[kk+1]&LOWER_MASK);
            mt->st[kk] = mt->st[kk+M] ^ (y >> 1) ^ mag01[y & 0x1];
        }
        for (;kk<N-1;kk++) {
            y = (mt->st[kk]&UPPER_MASK)|(mt->st[kk+1]&LOWER_MASK);
            mt->st[kk] = mt->st[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1];
        }
        y = (mt->st[N-1]&UPPER_MASK)|(mt->st[0]&LOWER_MASK);
        mt->st[N-1] = mt->st[M-1] ^ (y >> 1) ^ mag01[y & 0x1];

        mt->i = 0;
    }
  
    y = mt->st[mt->i++];
    y ^= TEMPERING_SHIFT_U(y);
    y ^= TEMPERING_SHIFT_S(y) & TEMPERING_MASK_B;
    y ^= TEMPERING_SHIFT_T(y) & TEMPERING_MASK_C;
    y ^= TEMPERING_SHIFT_L(y);

    return y; 
}

// Returns a random float between [0.0, 1.0], inclusive.
float mtrand43_genfloat(struct mtrand43 *mt)
{
    return (float)(mtrand43_genrand(mt)) / 4294967296.f;
}
