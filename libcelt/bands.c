/* Copyright (c) 2007-2008 CSIRO
   Copyright (c) 2007-2009 Xiph.Org Foundation
   Copyright (c) 2008-2009 Gregory Maxwell 
   Written by Jean-Marc Valin and Gregory Maxwell */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   
   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   
   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
   
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include "bands.h"
#include "modes.h"
#include "vq.h"
#include "cwrs.h"
#include "stack_alloc.h"
#include "os_support.h"
#include "mathops.h"
#include "rate.h"

celt_uint32 lcg_rand(celt_uint32 seed)
{
   return 1664525 * seed + 1013904223;
}

/* This is a cos() approximation designed to be bit-exact on any platform. Bit exactness
   with this approximation is important because it has an impact on the bit allocation */
static celt_int16 bitexact_cos(celt_int16 x)
{
   celt_int32 tmp;
   celt_int16 x2;
   tmp = (4096+((celt_int32)(x)*(x)))>>13;
   if (tmp > 32767)
      tmp = 32767;
   x2 = tmp;
   x2 = (32767-x2) + FRAC_MUL16(x2, (-7651 + FRAC_MUL16(x2, (8277 + FRAC_MUL16(-626, x2)))));
   if (x2 > 32766)
      x2 = 32766;
   return 1+x2;
}

static int bitexact_log2tan(int isin,int icos)
{
   int lc;
   int ls;
   lc=EC_ILOG(icos);
   ls=EC_ILOG(isin);
   icos<<=15-lc;
   isin<<=15-ls;
   return (ls-lc<<11)
         +FRAC_MUL16(isin, FRAC_MUL16(isin, -2597) + 7932)
         -FRAC_MUL16(icos, FRAC_MUL16(icos, -2597) + 7932);
}

#ifdef FIXED_POINT
/* Compute the amplitude (sqrt energy) in each of the bands */
void celtcompute_band_energies(const CELTMode *m, const celt_sig *X, celt_ener *bank, int end, int _C, int M)
{
   int i, c, N;
   const celt_int16 *eBands = m->eBands;
   const int C = CHANNELS(_C);
   N = M*m->shortMdctSize;
   c=0; do {
      for (i=0;i<end;i++)
      {
         int j;
         celt_word32 maxval=0;
         celt_word32 sum = 0;
         
         j=M*eBands[i]; do {
            maxval = MAX32(maxval, X[j+c*N]);
            maxval = MAX32(maxval, -X[j+c*N]);
         } while (++j<M*eBands[i+1]);
         
         if (maxval > 0)
         {
            int shift = celt_ilog2(maxval)-10;
            j=M*eBands[i]; do {
               sum = MAC16_16(sum, EXTRACT16(VSHR32(X[j+c*N],shift)),
                                   EXTRACT16(VSHR32(X[j+c*N],shift)));
            } while (++j<M*eBands[i+1]);
            /* We're adding one here to make damn sure we never end up with a pitch vector that's
               larger than unity norm */
            bank[i+c*m->nbEBands] = EPSILON+VSHR32(EXTEND32(celt_sqrt(sum)),-shift);
         } else {
            bank[i+c*m->nbEBands] = EPSILON;
         }
         /*printf ("%f ", bank[i+c*m->nbEBands]);*/
      }
   } while (++c<C);
   /*printf ("\n");*/
}

/* Normalise each band such that the energy is one. */
void celtnormalise_bands(const CELTMode *m, const celt_sig * restrict freq, celt_norm * restrict X, const celt_ener *bank, int end, int _C, int M)
{
   int i, c, N;
   const celt_int16 *eBands = m->eBands;
   const int C = CHANNELS(_C);
   N = M*m->shortMdctSize;
   c=0; do {
      i=0; do {
         celt_word16 g;
         int j,shift;
         celt_word16 E;
         shift = celt_zlog2(bank[i+c*m->nbEBands])-13;
         E = VSHR32(bank[i+c*m->nbEBands], shift);
         g = EXTRACT16(celt_rcp(SHL32(E,3)));
         j=M*eBands[i]; do {
            X[j+c*N] = MULT16_16_Q15(VSHR32(freq[j+c*N],shift-1),g);
         } while (++j<M*eBands[i+1]);
      } while (++i<end);
   } while (++c<C);
}

#else /* FIXED_POINT */
/* Compute the amplitude (sqrt energy) in each of the bands */
void celtcompute_band_energies(const CELTMode *m, const celt_sig *X, celt_ener *bank, int end, int _C, int M)
{
   int i, c, N;
   const celt_int16 *eBands = m->eBands;
   const int C = CHANNELS(_C);
   N = M*m->shortMdctSize;
   c=0; do {
      for (i=0;i<end;i++)
      {
         int j;
         celt_word32 sum = 1e-27f;
         for (j=M*eBands[i];j<M*eBands[i+1];j++)
            sum += X[j+c*N]*X[j+c*N];
         bank[i+c*m->nbEBands] = celt_sqrt(sum);
         /*printf ("%f ", bank[i+c*m->nbEBands]);*/
      }
   } while (++c<C);
   /*printf ("\n");*/
}

/* Normalise each band such that the energy is one. */
void celtnormalise_bands(const CELTMode *m, const celt_sig * restrict freq, celt_norm * restrict X, const celt_ener *bank, int end, int _C, int M)
{
   int i, c, N;
   const celt_int16 *eBands = m->eBands;
   const int C = CHANNELS(_C);
   N = M*m->shortMdctSize;
   c=0; do {
      for (i=0;i<end;i++)
      {
         int j;
         celt_word16 g = 1.f/(1e-27f+bank[i+c*m->nbEBands]);
         for (j=M*eBands[i];j<M*eBands[i+1];j++)
            X[j+c*N] = freq[j+c*N]*g;
      }
   } while (++c<C);
}

#endif /* FIXED_POINT */

/* De-normalise the energy to produce the synthesis from the unit-energy bands */
void celtdenormalise_bands(const CELTMode *m, const celt_norm * restrict X, celt_sig * restrict freq, const celt_ener *bank, int end, int _C, int M)
{
   int i, c, N;
   const celt_int16 *eBands = m->eBands;
   const int C = CHANNELS(_C);
   N = M*m->shortMdctSize;
   celt_assert2(C<=2, "celtdenormalise_bands() not implemented for >2 channels");
   c=0; do {
      celt_sig * restrict f;
      const celt_norm * restrict x;
      f = freq+c*N;
      x = X+c*N;
      for (i=0;i<end;i++)
      {
         int j, band_end;
         celt_word32 g = SHR32(bank[i+c*m->nbEBands],1);
         j=M*eBands[i];
         band_end = M*eBands[i+1];
         do {
            *f++ = SHL32(MULT16_32_Q15(*x, g),2);
            x++;
         } while (++j<band_end);
      }
      for (i=M*eBands[end];i<N;i++)
         *f++ = 0;
   } while (++c<C);
}

/* This prevents energy collapse for transients with multiple short MDCTs */
void celtanti_collapse(const CELTMode *m, celt_norm *_X, unsigned char *collapse_masks, int LM, int C, int CC, int size,
      int start, int end, celt_word16 *logE, celt_word16 *prev1logE,
      celt_word16 *prev2logE, int *pulses, celt_uint32 seed)
{
   int c, i, j, k;
   for (i=start;i<end;i++)
   {
      int N0;
      celt_word16 thresh, sqrt_1;
      int depth;
#ifdef FIXED_POINT
      int shift;
#endif

      N0 = m->eBands[i+1]-m->eBands[i];
      /* depth in 1/8 bits */
      depth = (1+pulses[i])/(m->eBands[i+1]-m->eBands[i]<<LM);

#ifdef FIXED_POINT
      thresh = MULT16_32_Q15(QCONST16(0.5f, 15), MIN32(32767,SHR32(celt_exp2(-SHL16(depth, 10-BITRES)),1) ));
      {
         celt_word32 t;
         t = N0<<LM;
         shift = celt_ilog2(t)>>1;
         t = SHL32(t, (7-shift)<<1);
         sqrt_1 = celt_rsqrt_norm(t);
      }
#else
      thresh = .5f*celt_exp2(-.125f*depth);
      sqrt_1 = celt_rsqrt(N0<<LM);
#endif

      c=0; do
      {
         celt_norm *X;
         celt_word16 prev1;
         celt_word16 prev2;
         celt_word16 Ediff;
         celt_word16 r;
         int renormalize=0;
         prev1 = prev1logE[c*m->nbEBands+i];
         prev2 = prev2logE[c*m->nbEBands+i];
         if (C<CC)
         {
            prev1 = MAX16(prev1,prev1logE[m->nbEBands+i]);
            prev2 = MAX16(prev2,prev2logE[m->nbEBands+i]);
         }
         Ediff = logE[c*m->nbEBands+i]-MIN16(prev1,prev2);
         Ediff = MAX16(0, Ediff);

#ifdef FIXED_POINT
         if (Ediff < 16384)
            r = 2*MIN16(16383,SHR32(celt_exp2(-Ediff),1));
         else
            r = 0;
         if (LM==3)
            r = MULT16_16_Q14(23170, MIN32(23169, r));
         r = SHR16(MIN16(thresh, r),1);
         r = SHR32(MULT16_16_Q15(sqrt_1, r),shift);
#else
         /* r needs to be multiplied by 2 or 2*sqrt(2) depending on LM because
            short blocks don't have the same energy as long */
         r = 2.f*celt_exp2(-Ediff);
         if (LM==3)
            r *= 1.41421356f;
         r = MIN16(thresh, r);
         r = r*sqrt_1;
#endif
         X = _X+c*size+(m->eBands[i]<<LM);
         for (k=0;k<1<<LM;k++)
         {
            /* Detect collapse */
            if (!(collapse_masks[i*C+c]&1<<k))
            {
               /* Fill with noise */
               for (j=0;j<N0;j++)
               {
                  seed = lcg_rand(seed);
                  X[(j<<LM)+k] = (seed&0x8000 ? r : -r);
               }
               renormalize = 1;
            }
         }
         /* We just added some energy, so we need to renormalise */
         if (renormalize)
            renormalise_vector(X, N0<<LM, Q15ONE);
      } while (++c<C);
   }

}


static void intensity_stereo(const CELTMode *m, celt_norm *X, celt_norm *Y, const celt_ener *bank, int bandID, int N)
{
   int i = bandID;
   int j;
   celt_word16 a1, a2;
   celt_word16 left, right;
   celt_word16 norm;
#ifdef FIXED_POINT
   int shift = celt_zlog2(MAX32(bank[i], bank[i+m->nbEBands]))-13;
#endif
   left = VSHR32(bank[i],shift);
   right = VSHR32(bank[i+m->nbEBands],shift);
   norm = EPSILON + celt_sqrt(EPSILON+MULT16_16(left,left)+MULT16_16(right,right));
   a1 = DIV32_16(SHL32(EXTEND32(left),14),norm);
   a2 = DIV32_16(SHL32(EXTEND32(right),14),norm);
   for (j=0;j<N;j++)
   {
      celt_norm r, l;
      l = X[j];
      r = Y[j];
      X[j] = MULT16_16_Q14(a1,l) + MULT16_16_Q14(a2,r);
      /* Side is not encoded, no need to calculate */
   }
}

static void stereo_split(celt_norm *X, celt_norm *Y, int N)
{
   int j;
   for (j=0;j<N;j++)
   {
      celt_norm r, l;
      l = MULT16_16_Q15(QCONST16(.70710678f,15), X[j]);
      r = MULT16_16_Q15(QCONST16(.70710678f,15), Y[j]);
      X[j] = l+r;
      Y[j] = r-l;
   }
}

static void stereo_merge(celt_norm *X, celt_norm *Y, celt_word16 mid, int N)
{
   int j;
   celt_word32 xp=0, side=0;
   celt_word32 El, Er;
   celt_word16 mid2;
#ifdef FIXED_POINT
   int kl, kr;
#endif
   celt_word32 t, lgain, rgain;

   /* Compute the norm of X+Y and X-Y as |X|^2 + |Y|^2 +/- sum(xy) */
   for (j=0;j<N;j++)
   {
      xp = MAC16_16(xp, X[j], Y[j]);
      side = MAC16_16(side, Y[j], Y[j]);
   }
   /* Compensating for the mid normalization */
   xp = MULT16_32_Q15(mid, xp);
   /* mid and side are in Q15, not Q14 like X and Y */
   mid2 = SHR32(mid, 1);
   El = MULT16_16(mid2, mid2) + side - 2*xp;
   Er = MULT16_16(mid2, mid2) + side + 2*xp;
   if (Er < QCONST32(6e-4f, 28) || El < QCONST32(6e-4f, 28))
   {
      for (j=0;j<N;j++)
         Y[j] = X[j];
      return;
   }

#ifdef FIXED_POINT
   kl = celt_ilog2(El)>>1;
   kr = celt_ilog2(Er)>>1;
#endif
   t = VSHR32(El, (kl-7)<<1);
   lgain = celt_rsqrt_norm(t);
   t = VSHR32(Er, (kr-7)<<1);
   rgain = celt_rsqrt_norm(t);

#ifdef FIXED_POINT
   if (kl < 7)
      kl = 7;
   if (kr < 7)
      kr = 7;
#endif

   for (j=0;j<N;j++)
   {
      celt_norm r, l;
      /* Apply mid scaling (side is already scaled) */
      l = MULT16_16_Q15(mid, X[j]);
      r = Y[j];
      X[j] = EXTRACT16(PSHR32(MULT16_16(lgain, SUB16(l,r)), kl+1));
      Y[j] = EXTRACT16(PSHR32(MULT16_16(rgain, ADD16(l,r)), kr+1));
   }
}

/* Decide whether we should spread the pulses in the current frame */
int celtspreading_decision(const CELTMode *m, celt_norm *X, int *average,
      int last_decision, int *hf_average, int *tapset_decision, int update_hf,
      int end, int _C, int M)
{
   int i, c, N0;
   int sum = 0, nbBands=0;
   const int C = CHANNELS(_C);
   const celt_int16 * restrict eBands = m->eBands;
   int decision;
   int hf_sum=0;
   
   N0 = M*m->shortMdctSize;

   if (M*(eBands[end]-eBands[end-1]) <= 8)
      return SPREAD_NONE;
   c=0; do {
      for (i=0;i<end;i++)
      {
         int j, N, tmp=0;
         int tcount[3] = {0};
         celt_norm * restrict x = X+M*eBands[i]+c*N0;
         N = M*(eBands[i+1]-eBands[i]);
         if (N<=8)
            continue;
         /* Compute rough CDF of |x[j]| */
         for (j=0;j<N;j++)
         {
            celt_word32 x2N; /* Q13 */

            x2N = MULT16_16(MULT16_16_Q15(x[j], x[j]), N);
            if (x2N < QCONST16(0.25f,13))
               tcount[0]++;
            if (x2N < QCONST16(0.0625f,13))
               tcount[1]++;
            if (x2N < QCONST16(0.015625f,13))
               tcount[2]++;
         }

         /* Only include four last bands (8 kHz and up) */
         if (i>m->nbEBands-4)
            hf_sum += 32*(tcount[1]+tcount[0])/N;
         tmp = (2*tcount[2] >= N) + (2*tcount[1] >= N) + (2*tcount[0] >= N);
         sum += tmp*256;
         nbBands++;
      }
   } while (++c<C);

   if (update_hf)
   {
      if (hf_sum)
         hf_sum /= C*(4-m->nbEBands+end);
      *hf_average = (*hf_average+hf_sum)>>1;
      hf_sum = *hf_average;
      if (*tapset_decision==2)
         hf_sum += 4;
      else if (*tapset_decision==0)
         hf_sum -= 4;
      if (hf_sum > 22)
         *tapset_decision=2;
      else if (hf_sum > 18)
         *tapset_decision=1;
      else
         *tapset_decision=0;
   }
   /*printf("%d %d %d\n", hf_sum, *hf_average, *tapset_decision);*/
   sum /= nbBands;
   /* Recursive averaging */
   sum = (sum+*average)>>1;
   *average = sum;
   /* Hysteresis */
   sum = (3*sum + (((3-last_decision)<<7) + 64) + 2)>>2;
   if (sum < 80)
   {
      decision = SPREAD_AGGRESSIVE;
   } else if (sum < 256)
   {
      decision = SPREAD_NORMAL;
   } else if (sum < 384)
   {
      decision = SPREAD_LIGHT;
   } else {
      decision = SPREAD_NONE;
   }
   return decision;
}

#ifdef MEASURE_NORM_MSE

float MSE[30] = {0};
int nbMSEBands = 0;
int MSECount[30] = {0};

void dump_norm_mse(void)
{
   int i;
   for (i=0;i<nbMSEBands;i++)
   {
      printf ("%g ", MSE[i]/MSECount[i]);
   }
   printf ("\n");
}

void measure_norm_mse(const CELTMode *m, float *X, float *X0, float *bandE, float *bandE0, int M, int N, int C)
{
   static int init = 0;
   int i;
   if (!init)
   {
      atexit(dump_norm_mse);
      init = 1;
   }
   for (i=0;i<m->nbEBands;i++)
   {
      int j;
      int c;
      float g;
      if (bandE0[i]<10 || (C==2 && bandE0[i+m->nbEBands]<1))
         continue;
      c=0; do {
         g = bandE[i+c*m->nbEBands]/(1e-15+bandE0[i+c*m->nbEBands]);
         for (j=M*m->eBands[i];j<M*m->eBands[i+1];j++)
            MSE[i] += (g*X[j+c*N]-X0[j+c*N])*(g*X[j+c*N]-X0[j+c*N]);
      } while (++c<C);
      MSECount[i]+=C;
   }
   nbMSEBands = m->nbEBands;
}

#endif

/* Indexing table for converting from natural Hadamard to ordery Hadamard
   This is essentially a bit-reversed Gray, on top of which we've added
   an inversion of the order because we want the DC at the end rather than
   the beginning. The lines are for N=2, 4, 8, 16 */
static const int ordery_table[] = {
       1,  0,
       3,  0,  2,  1,
       7,  0,  4,  3,  6,  1,  5,  2,
      15,  0,  8,  7, 12,  3, 11,  4, 14,  1,  9,  6, 13,  2, 10,  5,
};

static void deinterleave_hadamard(celt_norm *X, int N0, int stride, int hadamard)
{
   int i,j;
   VARDECL(celt_norm, tmp);
   int N;
   SAVE_STACK;
   N = N0*stride;
   ALLOC(tmp, N, celt_norm);
   if (hadamard)
   {
      const int *ordery = ordery_table+stride-2;
      for (i=0;i<stride;i++)
      {
         for (j=0;j<N0;j++)
            tmp[ordery[i]*N0+j] = X[j*stride+i];
      }
   } else {
      for (i=0;i<stride;i++)
         for (j=0;j<N0;j++)
            tmp[i*N0+j] = X[j*stride+i];
   }
   for (j=0;j<N;j++)
      X[j] = tmp[j];
   RESTORE_STACK;
}

static void interleave_hadamard(celt_norm *X, int N0, int stride, int hadamard)
{
   int i,j;
   VARDECL(celt_norm, tmp);
   int N;
   SAVE_STACK;
   N = N0*stride;
   ALLOC(tmp, N, celt_norm);
   if (hadamard)
   {
      const int *ordery = ordery_table+stride-2;
      for (i=0;i<stride;i++)
         for (j=0;j<N0;j++)
            tmp[j*stride+i] = X[ordery[i]*N0+j];
   } else {
      for (i=0;i<stride;i++)
         for (j=0;j<N0;j++)
            tmp[j*stride+i] = X[i*N0+j];
   }
   for (j=0;j<N;j++)
      X[j] = tmp[j];
   RESTORE_STACK;
}

void celthaar1(celt_norm *X, int N0, int stride)
{
   int i, j;
   N0 >>= 1;
   for (i=0;i<stride;i++)
      for (j=0;j<N0;j++)
      {
         celt_norm tmp1, tmp2;
         tmp1 = MULT16_16_Q15(QCONST16(.70710678f,15), X[stride*2*j+i]);
         tmp2 = MULT16_16_Q15(QCONST16(.70710678f,15), X[stride*(2*j+1)+i]);
         X[stride*2*j+i] = tmp1 + tmp2;
         X[stride*(2*j+1)+i] = tmp1 - tmp2;
      }
}

static int compute_qn(int N, int b, int offset, int pulse_cap, int stereo)
{
   static const celt_int16 exp2_table8[8] =
      {16384, 17866, 19483, 21247, 23170, 25267, 27554, 30048};
   int qn, qb;
   int N2 = 2*N-1;
   if (stereo && N==2)
      N2--;
   /* The upper limit ensures that in a stereo split with itheta==16384, we'll
       always have enough bits left over to code at least one pulse in the
       side; otherwise it would collapse, since it doesn't get folded. */
   qb = IMIN(b-pulse_cap-(4<<BITRES), (b+N2*offset)/N2);

   qb = IMIN(8<<BITRES, qb);

   if (qb<(1<<BITRES>>1)) {
      qn = 1;
   } else {
      qn = exp2_table8[qb&0x7]>>(14-(qb>>BITRES));
      qn = (qn+1)>>1<<1;
   }
   celt_assert(qn <= 256);
   return qn;
}

/* This function is responsible for encoding and decoding a band for both
   the mono and stereo case. Even in the mono case, it can split the band
   in two and transmit the energy difference with the two half-bands. It
   can be called recursively so bands can end up being split in 8 parts. */
static unsigned quant_band(int encode, const CELTMode *m, int i, celt_norm *X, celt_norm *Y,
      int N, int b, int spread, int B, int intensity, int tf_change, celt_norm *lowband, int resynth, ec_ctx *ec,
      celt_int32 *remaining_bits, int LM, celt_norm *lowband_out, const celt_ener *bandE, int level,
      celt_uint32 *seed, celt_word16 gain, celt_norm *lowband_scratch, int fill)
{
   const unsigned char *cache;
   int q;
   int curr_bits;
   int stereo, split;
   int imid=0, iside=0;
   int N0=N;
   int N_B=N;
   int N_B0;
   int B0=B;
   int time_divide=0;
   int recombine=0;
   int inv = 0;
   celt_word16 mid=0, side=0;
   int longBlocks;
   unsigned cm=0;

   longBlocks = B0==1;

   N_B /= B;
   N_B0 = N_B;

   split = stereo = Y != NULL;

   /* Special case for one sample */
   if (N==1)
   {
      int c;
      celt_norm *x = X;
      c=0; do {
         int sign=0;
         if (*remaining_bits>=1<<BITRES)
         {
            if (encode)
            {
               sign = x[0]<0;
               ec_enc_bits(ec, sign, 1);
            } else {
               sign = ec_dec_bits(ec, 1);
            }
            *remaining_bits -= 1<<BITRES;
            b-=1<<BITRES;
         }
         if (resynth)
            x[0] = sign ? -NORM_SCALING : NORM_SCALING;
         x = Y;
      } while (++c<1+stereo);
      if (lowband_out)
         lowband_out[0] = SHR16(X[0],4);
      return 1;
   }

   if (!stereo && level == 0)
   {
      int k;
      if (tf_change>0)
         recombine = tf_change;
      /* Band recombining to increase frequency resolution */

      if (lowband && (recombine || ((N_B&1) == 0 && tf_change<0) || B0>1))
      {
         int j;
         for (j=0;j<N;j++)
            lowband_scratch[j] = lowband[j];
         lowband = lowband_scratch;
      }

      for (k=0;k<recombine;k++)
      {
         static const unsigned char bit_interleave_table[16]={
           0,1,1,1,2,3,3,3,2,3,3,3,2,3,3,3
         };
         if (encode)
            celthaar1(X, N>>k, 1<<k);
         if (lowband)
            celthaar1(lowband, N>>k, 1<<k);
         fill = bit_interleave_table[fill&0xF]|bit_interleave_table[fill>>4]<<2;
      }
      B>>=recombine;
      N_B<<=recombine;

      /* Increasing the time resolution */
      while ((N_B&1) == 0 && tf_change<0)
      {
         if (encode)
            celthaar1(X, N_B, B);
         if (lowband)
            celthaar1(lowband, N_B, B);
         fill |= fill<<B;
         B <<= 1;
         N_B >>= 1;
         time_divide++;
         tf_change++;
      }
      B0=B;
      N_B0 = N_B;

      /* Reorganize the samples in time order instead of frequency order */
      if (B0>1)
      {
         if (encode)
            deinterleave_hadamard(X, N_B>>recombine, B0<<recombine, longBlocks);
         if (lowband)
            deinterleave_hadamard(lowband, N_B>>recombine, B0<<recombine, longBlocks);
      }
   }

   /* If we need 1.5 more bit than we can produce, split the band in two. */
   cache = m->cache.bits + m->cache.index[(LM+1)*m->nbEBands+i];
   if (!stereo && LM != -1 && b > cache[cache[0]]+12 && N>2)
   {
      if (LM>0 || (N&1)==0)
      {
         N >>= 1;
         Y = X+N;
         split = 1;
         LM -= 1;
         if (B==1)
            fill = fill&1|fill<<1;
         B = (B+1)>>1;
      }
   }

   if (split)
   {
      int qn;
      int itheta=0;
      int mbits, sbits, delta;
      int qalloc;
      int pulse_cap;
      int offset;
      int orig_fill;
      celt_int32 tell;

      /* Decide on the resolution to give to the split parameter theta */
      pulse_cap = m->logN[i]+(LM<<BITRES);
      offset = (pulse_cap>>1) - (stereo&&N==2 ? QTHETA_OFFSET_TWOPHASE : QTHETA_OFFSET);
      qn = compute_qn(N, b, offset, pulse_cap, stereo);
      if (stereo && i>=intensity)
         qn = 1;
      if (encode)
      {
         /* theta is the atan() of the ratio between the (normalized)
            side and mid. With just that parameter, we can re-scale both
            mid and side because we know that 1) they have unit norm and
            2) they are orthogonal. */
         itheta = stereo_itheta(X, Y, stereo, N);
      }
      tell = ec_tell_frac(ec);
      if (qn!=1)
      {
         if (encode)
            itheta = (itheta*qn+8192)>>14;

         /* Entropy coding of the angle. We use a uniform pdf for the
            time split, a step for stereo, and a triangular one for the rest. */
         if (stereo && N>2)
         {
            int p0 = 3;
            int x = itheta;
            int x0 = qn/2;
            int ft = p0*(x0+1) + x0;
            /* Use a probability of p0 up to itheta=8192 and then use 1 after */
            if (encode)
            {
               ec_encode(ec,x<=x0?p0*x:(x-1-x0)+(x0+1)*p0,x<=x0?p0*(x+1):(x-x0)+(x0+1)*p0,ft);
            } else {
               int fs;
               fs=ec_decode(ec,ft);
               if (fs<(x0+1)*p0)
                  x=fs/p0;
               else
                  x=x0+1+(fs-(x0+1)*p0);
               ec_dec_update(ec,x<=x0?p0*x:(x-1-x0)+(x0+1)*p0,x<=x0?p0*(x+1):(x-x0)+(x0+1)*p0,ft);
               itheta = x;
            }
         } else if (B0>1 || stereo) {
            /* Uniform pdf */
            if (encode)
               ec_enc_uint(ec, itheta, qn+1);
            else
               itheta = ec_dec_uint(ec, qn+1);
         } else {
            int fs=1, ft;
            ft = ((qn>>1)+1)*((qn>>1)+1);
            if (encode)
            {
               int fl;

               fs = itheta <= (qn>>1) ? itheta + 1 : qn + 1 - itheta;
               fl = itheta <= (qn>>1) ? itheta*(itheta + 1)>>1 :
                ft - ((qn + 1 - itheta)*(qn + 2 - itheta)>>1);

               ec_encode(ec, fl, fl+fs, ft);
            } else {
               /* Triangular pdf */
               int fl=0;
               int fm;
               fm = ec_decode(ec, ft);

               if (fm < ((qn>>1)*((qn>>1) + 1)>>1))
               {
                  itheta = (celtisqrt32(8*(celt_uint32)fm + 1) - 1)>>1;
                  fs = itheta + 1;
                  fl = itheta*(itheta + 1)>>1;
               }
               else
               {
                  itheta = (2*(qn + 1)
                   - celtisqrt32(8*(celt_uint32)(ft - fm - 1) + 1))>>1;
                  fs = qn + 1 - itheta;
                  fl = ft - ((qn + 1 - itheta)*(qn + 2 - itheta)>>1);
               }

               ec_dec_update(ec, fl, fl+fs, ft);
            }
         }
         itheta = (celt_int32)itheta*16384/qn;
         if (encode && stereo)
         {
            if (itheta==0)
               intensity_stereo(m, X, Y, bandE, i, N);
            else
               stereo_split(X, Y, N);
         }
         /* TODO: Renormalising X and Y *may* help fixed-point a bit at very high rate.
                  Let's do that at higher complexity */
      } else if (stereo) {
         if (encode)
         {
            inv = itheta > 8192;
            if (inv)
            {
               int j;
               for (j=0;j<N;j++)
                  Y[j] = -Y[j];
            }
            intensity_stereo(m, X, Y, bandE, i, N);
         }
         if (b>2<<BITRES && *remaining_bits > 2<<BITRES)
         {
            if (encode)
               ec_enc_bit_logp(ec, inv, 2);
            else
               inv = ec_dec_bit_logp(ec, 2);
         } else
            inv = 0;
         itheta = 0;
      }
      qalloc = ec_tell_frac(ec) - tell;
      b -= qalloc;

      orig_fill = fill;
      if (itheta == 0)
      {
         imid = 32767;
         iside = 0;
         fill &= (1<<B)-1;
         delta = -16384;
      } else if (itheta == 16384)
      {
         imid = 0;
         iside = 32767;
         fill &= (1<<B)-1<<B;
         delta = 16384;
      } else {
         imid = bitexact_cos(itheta);
         iside = bitexact_cos(16384-itheta);
         /* This is the mid vs side allocation that minimizes squared error
            in that band. */
         delta = FRAC_MUL16(N-1<<7,bitexact_log2tan(iside,imid));
      }

#ifdef FIXED_POINT
      mid = imid;
      side = iside;
#else
      mid = (1.f/32768)*imid;
      side = (1.f/32768)*iside;
#endif

      /* This is a special case for N=2 that only works for stereo and takes
         advantage of the fact that mid and side are orthogonal to encode
         the side with just one bit. */
      if (N==2 && stereo)
      {
         int c;
         int sign=0;
         celt_norm *x2, *y2;
         mbits = b;
         sbits = 0;
         /* Only need one bit for the side */
         if (itheta != 0 && itheta != 16384)
            sbits = 1<<BITRES;
         mbits -= sbits;
         c = itheta > 8192;
         *remaining_bits -= qalloc+sbits;

         x2 = c ? Y : X;
         y2 = c ? X : Y;
         if (sbits)
         {
            if (encode)
            {
               /* Here we only need to encode a sign for the side */
               sign = x2[0]*y2[1] - x2[1]*y2[0] < 0;
               ec_enc_bits(ec, sign, 1);
            } else {
               sign = ec_dec_bits(ec, 1);
            }
         }
         sign = 1-2*sign;
         /* We use orig_fill here because we want to fold the side, but if
             itheta==16384, we'll have cleared the low bits of fill. */
         cm = quant_band(encode, m, i, x2, NULL, N, mbits, spread, B, intensity, tf_change, lowband, resynth, ec, remaining_bits, LM, lowband_out, NULL, level, seed, gain, lowband_scratch, orig_fill);
         /* We don't split N=2 bands, so cm is either 1 or 0 (for a fold-collapse),
             and there's no need to worry about mixing with the other channel. */
         y2[0] = -sign*x2[1];
         y2[1] = sign*x2[0];
         if (resynth)
         {
            celt_norm tmp;
            X[0] = MULT16_16_Q15(mid, X[0]);
            X[1] = MULT16_16_Q15(mid, X[1]);
            Y[0] = MULT16_16_Q15(side, Y[0]);
            Y[1] = MULT16_16_Q15(side, Y[1]);
            tmp = X[0];
            X[0] = SUB16(tmp,Y[0]);
            Y[0] = ADD16(tmp,Y[0]);
            tmp = X[1];
            X[1] = SUB16(tmp,Y[1]);
            Y[1] = ADD16(tmp,Y[1]);
         }
      } else {
         /* "Normal" split code */
         celt_norm *next_lowband2=NULL;
         celt_norm *next_lowband_out1=NULL;
         int next_level=0;
         celt_int32 rebalance;

         /* Give more bits to low-energy MDCTs than they would otherwise deserve */
         if (B0>1 && !stereo && (itheta&0x3fff))
         {
            if (itheta > 8192)
               /* Rough approximation for pre-echo masking */
               delta -= delta>>(4-LM);
            else
               /* Corresponds to a forward-masking slope of 1.5 dB per 10 ms */
               delta = IMIN(0, delta + (N<<BITRES>>(5-LM)));
         }
         mbits = IMAX(0, IMIN(b, (b-delta)/2));
         sbits = b-mbits;
         *remaining_bits -= qalloc;

         if (lowband && !stereo)
            next_lowband2 = lowband+N; /* >32-bit split case */

         /* Only stereo needs to pass on lowband_out. Otherwise, it's
            handled at the end */
         if (stereo)
            next_lowband_out1 = lowband_out;
         else
            next_level = level+1;

         rebalance = *remaining_bits;
         if (mbits >= sbits)
         {
            /* In stereo mode, we do not apply a scaling to the mid because we need the normalized
               mid for folding later */
            cm = quant_band(encode, m, i, X, NULL, N, mbits, spread, B, intensity, tf_change,
                  lowband, resynth, ec, remaining_bits, LM, next_lowband_out1,
                  NULL, next_level, seed, stereo ? Q15ONE : MULT16_16_P15(gain,mid), lowband_scratch, fill);
            rebalance = mbits - (rebalance-*remaining_bits);
            if (rebalance > 3<<BITRES && itheta!=0)
               sbits += rebalance - (3<<BITRES);

            /* For a stereo split, the high bits of fill are always zero, so no
               folding will be done to the side. */
            cm |= quant_band(encode, m, i, Y, NULL, N, sbits, spread, B, intensity, tf_change,
                  next_lowband2, resynth, ec, remaining_bits, LM, NULL,
                  NULL, next_level, seed, MULT16_16_P15(gain,side), NULL, fill>>B)<<(B0>>1&stereo-1);
         } else {
            /* For a stereo split, the high bits of fill are always zero, so no
               folding will be done to the side. */
            cm = quant_band(encode, m, i, Y, NULL, N, sbits, spread, B, intensity, tf_change,
                  next_lowband2, resynth, ec, remaining_bits, LM, NULL,
                  NULL, next_level, seed, MULT16_16_P15(gain,side), NULL, fill>>B)<<(B0>>1&stereo-1);
            rebalance = sbits - (rebalance-*remaining_bits);
            if (rebalance > 3<<BITRES && itheta!=16384)
               mbits += rebalance - (3<<BITRES);
            /* In stereo mode, we do not apply a scaling to the mid because we need the normalized
               mid for folding later */
            cm |= quant_band(encode, m, i, X, NULL, N, mbits, spread, B, intensity, tf_change,
                  lowband, resynth, ec, remaining_bits, LM, next_lowband_out1,
                  NULL, next_level, seed, stereo ? Q15ONE : MULT16_16_P15(gain,mid), lowband_scratch, fill);
         }
      }

   } else {
      /* This is the basic no-split case */
      q = bits2pulses(m, i, LM, b);
      curr_bits = pulses2bits(m, i, LM, q);
      *remaining_bits -= curr_bits;

      /* Ensures we can never bust the budget */
      while (*remaining_bits < 0 && q > 0)
      {
         *remaining_bits += curr_bits;
         q--;
         curr_bits = pulses2bits(m, i, LM, q);
         *remaining_bits -= curr_bits;
      }

      if (q!=0)
      {
         int K = get_pulses(q);

         /* Finally do the actual quantization */
         if (encode)
            cm = alg_quant(X, N, K, spread, B, resynth, ec, gain);
         else
            cm = alg_unquant(X, N, K, spread, B, ec, gain);
      } else {
         /* If there's no pulse, fill the band anyway */
         int j;
         if (resynth)
         {
            unsigned cm_mask;
            /*B can be as large as 16, so this shift might overflow an int on a
               16-bit platform; use a long to get defined behavior.*/
            cm_mask = (unsigned)(1UL<<B)-1;
            fill &= cm_mask;
            if (!fill)
            {
               for (j=0;j<N;j++)
                  X[j] = 0;
            } else {
               if (lowband == NULL)
               {
                  /* Noise */
                  for (j=0;j<N;j++)
                  {
                     *seed = lcg_rand(*seed);
                     X[j] = (celt_int32)(*seed)>>20;
                  }
                  cm = cm_mask;
               } else {
                  /* Folded spectrum */
                  for (j=0;j<N;j++)
                  {
                     celt_word16 tmp;
                     *seed = lcg_rand(*seed);
                     /* About 48 dB below the "normal" folding level */
                     tmp = QCONST16(1.0f/256, 10);
                     tmp = (*seed)&0x8000 ? tmp : -tmp;
                     X[j] = lowband[j]+tmp;
                  }
                  cm = fill;
               }
               renormalise_vector(X, N, gain);
            }
         }
      }
   }

   /* This code is used by the decoder and by the resynthesis-enabled encoder */
   if (resynth)
   {
      if (stereo)
      {
         if (N!=2)
            stereo_merge(X, Y, mid, N);
         if (inv)
         {
            int j;
            for (j=0;j<N;j++)
               Y[j] = -Y[j];
         }
      } else if (level == 0)
      {
         int k;

         /* Undo the sample reorganization going from time order to frequency order */
         if (B0>1)
            interleave_hadamard(X, N_B>>recombine, B0<<recombine, longBlocks);

         /* Undo time-freq changes that we did earlier */
         N_B = N_B0;
         B = B0;
         for (k=0;k<time_divide;k++)
         {
            B >>= 1;
            N_B <<= 1;
            cm |= cm>>B;
            celthaar1(X, N_B, B);
         }

         for (k=0;k<recombine;k++)
         {
            static const unsigned char bit_deinterleave_table[16]={
              0x00,0x03,0x0C,0x0F,0x30,0x33,0x3C,0x3F,
              0xC0,0xC3,0xCC,0xCF,0xF0,0xF3,0xFC,0xFF
            };
            cm = bit_deinterleave_table[cm];
            celthaar1(X, N0>>k, 1<<k);
         }
         B<<=recombine;

         /* Scale output for later folding */
         if (lowband_out)
         {
            int j;
            celt_word16 n;
            n = celt_sqrt(SHL32(EXTEND32(N0),22));
            for (j=0;j<N0;j++)
               lowband_out[j] = MULT16_16_Q15(n,X[j]);
         }
         cm &= (1<<B)-1;
      }
   }
   return cm;
}

void celtquant_all_bands(int encode, const CELTMode *m, int start, int end,
      celt_norm *_X, celt_norm *_Y, unsigned char *collapse_masks, const celt_ener *bandE, int *pulses,
      int shortBlocks, int spread, int dual_stereo, int intensity, int *tf_res, int resynth,
      celt_int32 total_bits, celt_int32 balance, ec_ctx *ec, int LM, int codedBands, celt_uint32 *seed)
{
   int i;
   celt_int32 remaining_bits;
   const celt_int16 * restrict eBands = m->eBands;
   celt_norm * restrict norm, * restrict norm2;
   VARDECL(celt_norm, _norm);
   VARDECL(celt_norm, lowband_scratch);
   int B;
   int M;
   int lowband_offset;
   int update_lowband = 1;
   int C = _Y != NULL ? 2 : 1;
   SAVE_STACK;

   M = 1<<LM;
   B = shortBlocks ? M : 1;
   ALLOC(_norm, C*M*eBands[m->nbEBands], celt_norm);
   ALLOC(lowband_scratch, M*(eBands[m->nbEBands]-eBands[m->nbEBands-1]), celt_norm);
   norm = _norm;
   norm2 = norm + M*eBands[m->nbEBands];

   lowband_offset = 0;
   for (i=start;i<end;i++)
   {
      celt_int32 tell;
      int b;
      int N;
      celt_int32 curr_balance;
      int effective_lowband=-1;
      celt_norm * restrict X, * restrict Y;
      int tf_change=0;
      unsigned x_cm;
      unsigned y_cm;

      X = _X+M*eBands[i];
      if (_Y!=NULL)
         Y = _Y+M*eBands[i];
      else
         Y = NULL;
      N = M*eBands[i+1]-M*eBands[i];
      tell = ec_tell_frac(ec);

      /* Compute how many bits we want to allocate to this band */
      if (i != start)
         balance -= tell;
      remaining_bits = total_bits-tell-1;
      if (i <= codedBands-1)
      {
         curr_balance = balance / IMIN(3, codedBands-i);
         b = IMAX(0, IMIN(16383, IMIN(remaining_bits+1,pulses[i]+curr_balance)));
      } else {
         b = 0;
      }

      if (resynth && M*eBands[i]-N >= M*eBands[start] && (update_lowband || lowband_offset==0))
            lowband_offset = i;

      tf_change = tf_res[i];
      if (i>=m->effEBands)
      {
         X=norm;
         if (_Y!=NULL)
            Y = norm;
      }

      /* Get a conservative estimate of the collapse_mask's for the bands we're
          going to be folding from. */
      if (lowband_offset != 0 && (spread!=SPREAD_AGGRESSIVE || B>1 || tf_change<0))
      {
         int fold_start;
         int fold_end;
         int fold_i;
         /* This ensures we never repeat spectral content within one band */
         effective_lowband = IMAX(M*eBands[start], M*eBands[lowband_offset]-N);
         fold_start = lowband_offset;
         while(M*eBands[--fold_start] > effective_lowband);
         fold_end = lowband_offset-1;
         while(M*eBands[++fold_end] < effective_lowband+N);
         x_cm = y_cm = 0;
         fold_i = fold_start; do {
           x_cm |= collapse_masks[fold_i*C+0];
           y_cm |= collapse_masks[fold_i*C+C-1];
         } while (++fold_i<fold_end);
      }
      /* Otherwise, we'll be using the LCG to fold, so all blocks will (almost
          always) be non-zero.*/
      else
         x_cm = y_cm = (1<<B)-1;

      if (dual_stereo && i==intensity)
      {
         int j;

         /* Switch off dual stereo to do intensity */
         dual_stereo = 0;
         for (j=M*eBands[start];j<M*eBands[i];j++)
            norm[j] = HALF32(norm[j]+norm2[j]);
      }
      if (dual_stereo)
      {
         x_cm = quant_band(encode, m, i, X, NULL, N, b/2, spread, B, intensity, tf_change,
               effective_lowband != -1 ? norm+effective_lowband : NULL, resynth, ec, &remaining_bits, LM,
               norm+M*eBands[i], bandE, 0, seed, Q15ONE, lowband_scratch, x_cm);
         y_cm = quant_band(encode, m, i, Y, NULL, N, b/2, spread, B, intensity, tf_change,
               effective_lowband != -1 ? norm2+effective_lowband : NULL, resynth, ec, &remaining_bits, LM,
               norm2+M*eBands[i], bandE, 0, seed, Q15ONE, lowband_scratch, y_cm);
      } else {
         x_cm = quant_band(encode, m, i, X, Y, N, b, spread, B, intensity, tf_change,
               effective_lowband != -1 ? norm+effective_lowband : NULL, resynth, ec, &remaining_bits, LM,
               norm+M*eBands[i], bandE, 0, seed, Q15ONE, lowband_scratch, x_cm|y_cm);
         y_cm = x_cm;
      }
      collapse_masks[i*C+0] = (unsigned char)x_cm;
      collapse_masks[i*C+C-1] = (unsigned char)y_cm;
      balance += pulses[i] + tell;

      /* Update the folding position only as long as we have 1 bit/sample depth */
      update_lowband = b>(N<<BITRES);
   }
   RESTORE_STACK;
}

