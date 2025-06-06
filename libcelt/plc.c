/* Copyright (c) 2009-2010 Xiph.Org Foundation
   Written by Jean-Marc Valin */
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

#include "plc.h"
#include "stack_alloc.h"
#include "mathops.h"




void _old_celt_lpc(
      celt_word16       *_lpc, /* out: [0...p-1] LPC coefficients      */
const celt_word32 *ac,  /* in:  [0...p] autocorrelation values  */
int          p
)
{
   int i, j;  
   celt_word32 r;
   celt_word32 error = ac[0];
#ifdef FIXED_POINT
   celt_word32 lpc[LPC_ORDER];
#else
   float *lpc = _lpc;
#endif

   for (i = 0; i < p; i++)
      lpc[i] = 0;
   if (ac[0] != 0)
   {
      for (i = 0; i < p; i++) {
         /* Sum up this iteration's reflection coefficient */
         celt_word32 rr = 0;
         for (j = 0; j < i; j++)
            rr += MULT32_32_Q31(lpc[j],ac[i - j]);
         rr += SHR32(ac[i + 1],3);
         r = -frac_div32(SHL32(rr,3), error);
         /*  Update LPC coefficients and total error */
         lpc[i] = SHR32(r,3);
         for (j = 0; j < (i+1)>>1; j++)
         {
            celt_word32 tmp1, tmp2;
            tmp1 = lpc[j];
            tmp2 = lpc[i-1-j];
            lpc[j]     = tmp1 + MULT32_32_Q31(r,tmp2);
            lpc[i-1-j] = tmp2 + MULT32_32_Q31(r,tmp1);
         }

         error = error - MULT32_32_Q31(MULT32_32_Q31(r,r),error);
         /* Bail out once we get 30 dB gain */
#ifdef FIXED_POINT
         if (error<SHR32(ac[0],10))
            break;
#else
         if (error<.001f*ac[0])
            break;
#endif
      }
   }
#ifdef FIXED_POINT
   for (i=0;i<p;i++)
      _lpc[i] = ROUND16(lpc[i],16);
#endif
}

void fir(const celt_word16 *x,
         const celt_word16 *num,
         celt_word16 *y,
         int N,
         int ord,
         celt_word16 *mem)
{
   int i,j;

   for (i=0;i<N;i++)
   {
      celt_word32 sum = SHL32(EXTEND32(x[i]), SIG_SHIFT);
      for (j=0;j<ord;j++)
      {
         sum += MULT16_16(num[j],mem[j]);
      }
      for (j=ord-1;j>=1;j--)
      {
         mem[j]=mem[j-1];
      }
      mem[0] = x[i];
      y[i] = ROUND16(sum, SIG_SHIFT);
   }
}

void iir(const celt_word32 *x,
         const celt_word16 *den,
         celt_word32 *y,
         int N,
         int ord,
         celt_word16 *mem)
{
   int i,j;
   for (i=0;i<N;i++)
   {
      celt_word32 sum = x[i];
      for (j=0;j<ord;j++)
      {
         sum -= MULT16_16(den[j],mem[j]);
      }
      for (j=ord-1;j>=1;j--)
      {
         mem[j]=mem[j-1];
      }
      mem[0] = ROUND16(sum,SIG_SHIFT);
      y[i] = sum;
   }
}

void _old_celt_autocorr(
                   const celt_word16 *x,   /*  in: [0...n-1] samples x   */
                   celt_word32       *ac,  /* out: [0...lag-1] ac values */
                   const celt_word16       *window,
                   int          overlap,
                   int          lag, 
                   int          n
                  )
{
   celt_word32 d;
   int i;
   VARDECL(celt_word16, xx);
   SAVE_STACK;
   ALLOC(xx, n, celt_word16);
   for (i=0;i<n;i++)
      xx[i] = x[i];
   for (i=0;i<overlap;i++)
   {
      xx[i] = MULT16_16_Q15(x[i],window[i]);
      xx[n-i-1] = MULT16_16_Q15(x[n-i-1],window[i]);
   }
#ifdef FIXED_POINT
   {
      celt_word32 ac0=0;
      int shift;
      for(i=0;i<n;i++)
         ac0 += SHR32(MULT16_16(xx[i],xx[i]),8);
      ac0 += 1+n;

      shift = celt_ilog2(ac0)-30+9;
      shift = (shift+1)/2;
      for(i=0;i<n;i++)
         xx[i] = VSHR32(xx[i], shift);
   }
#endif
   while (lag>=0)
   {
      for (i = lag, d = 0; i < n; i++) 
         d += xx[i] * xx[i-lag];
      ac[lag] = d;
      /*printf ("%f ", ac[lag]);*/
      lag--;
   }
   /*printf ("\n");*/
   ac[0] += 10;

   RESTORE_STACK;
}
