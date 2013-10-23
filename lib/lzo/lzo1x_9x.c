/* lzo1x_9x.c -- implementation of the LZO1X-999 compression algorithm

   This file is part of the LZO real-time data compression library.

   Copyright (C) 1996-2002 Markus Franz Xaver Johannes Oberhumer
   All Rights Reserved.

   The LZO library is free software; you can redistribute it and/or
   modify it under the terms of version 2 of the GNU General Public
   License as published by the Free Software Foundation.

   The LZO library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the LZO library; see the file COPYING.
   If not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

   Markus F.X.J. Oberhumer
   <markus@oberhumer.com>
 */

#include <linux/module.h>
#include <linux/string.h>

typedef unsigned int lzo_uint32;
typedef int lzo_int32;
typedef unsigned int lzo_uint;
typedef int lzo_int;
typedef int lzo_bool;

typedef void ( *lzo_progress_callback_t) (unsigned, unsigned);

typedef struct
{
 int init;

 lzo_uint look;

 lzo_uint m_len;
 lzo_uint m_off;

 lzo_uint last_m_len;
 lzo_uint last_m_off;

 const unsigned char *bp;
 const unsigned char *ip;
 const unsigned char *in;
 const unsigned char *in_end;
 unsigned char *out;

 lzo_progress_callback_t cb;

 lzo_uint textsize;
 lzo_uint codesize;
 lzo_uint printcount;


 unsigned long lit_bytes;
 unsigned long match_bytes;
 unsigned long rep_bytes;
 unsigned long lazy;

 lzo_uint r1_lit;
 lzo_uint r1_m_len;

 unsigned long m1a_m, m1b_m, m2_m, m3_m, m4_m;
 unsigned long lit1_r, lit2_r, lit3_r;
}
lzo1x_999_t;

typedef unsigned short swd_uint;

typedef struct
{
 lzo_uint n;
 lzo_uint f;
 lzo_uint threshold;

 lzo_uint max_chain;
 lzo_uint nice_length;
 lzo_bool use_best_off;
 lzo_uint lazy_insert;

 lzo_uint m_len;
 lzo_uint m_off;
 lzo_uint look;
 int b_char;

 lzo_uint best_off[ (((8) >= (33) ? ((8) >= (9) ? (8) : (9)) : ((33) >= (9) ? (33) : (9))) + 1) ];

 lzo1x_999_t *c;
 lzo_uint m_pos;

 lzo_uint best_pos[ (((8) >= (33) ? ((8) >= (9) ? (8) : (9)) : ((33) >= (9) ? (33) : (9))) + 1) ];

 const unsigned char *dict;
 const unsigned char *dict_end;
 lzo_uint dict_len;

 lzo_uint ip;
 lzo_uint bp;
 lzo_uint rp;
 lzo_uint b_size;

 unsigned char *b_wrap;

 lzo_uint node_count;
 lzo_uint first_rp;

 unsigned char b [ 0xbfff + 2048 + 2048 ];
 swd_uint head3 [ 16384 ];
 swd_uint succ3 [ 0xbfff + 2048 ];
 swd_uint best3 [ 0xbfff + 2048 ];
 swd_uint llen3 [ 16384 ];

 swd_uint head2 [ 65536L ];
}
lzo1x_999_swd_t;

static
void swd_initdict(lzo1x_999_swd_t *s, const unsigned char *dict, lzo_uint dict_len)
{
 s->dict = s->dict_end = ((void *)0);
 s->dict_len = 0;

 if (!dict || dict_len <= 0)
  return;
 if (dict_len > s->n)
 {
  dict += dict_len - s->n;
  dict_len = s->n;
 }

 s->dict = dict;
 s->dict_len = dict_len;
 s->dict_end = dict + dict_len;
 memcpy(s->b,dict,dict_len);
 s->ip = dict_len;
}


static
void swd_insertdict(lzo1x_999_swd_t *s, lzo_uint node, lzo_uint len)
{
 lzo_uint key;

 s->node_count = s->n - len;
 s->first_rp = node;

 while (len-- > 0)
 {
  key = (((0x9f5f*(((((lzo_uint32)s->b[node]<<5)^s->b[node+1])<<5)^s->b[node+2]))>>5) & (16384 -1));
  s->succ3[node] = s->head3[key];
  s->head3[key] = ((swd_uint)(node));
  s->best3[node] = ((swd_uint)(s->f + 1));
  s->llen3[key]++;
  ((void) (0));


  key = (* (unsigned short *) &(s->b[node]));
  s->head2[key] = ((swd_uint)(node));


  node++;
 }
}






static
int swd_init(lzo1x_999_swd_t *s, const unsigned char *dict, lzo_uint dict_len)
{
 lzo_uint i = 0;
 int c = 0;

 s->n = 0xbfff;
 s->f = 2048;
 s->threshold = 1;


 s->max_chain = 2048;
 s->nice_length = 2048;
 s->use_best_off = 0;
 s->lazy_insert = 0;

 s->b_size = s->n + s->f;
 if (2 * s->f >= s->n || s->b_size + s->f >= (32767 * 2 + 1))
  return (-1);
 s->b_wrap = s->b + s->b_size;
 s->node_count = s->n;

 memset(s->llen3, 0, sizeof(s->llen3[0]) * 16384);


 memset(s->head2, 0xff, sizeof(s->head2[0]) * 65536L);
 ((void) (0));






 s->ip = 0;
 swd_initdict(s,dict,dict_len);
 s->bp = s->ip;
 s->first_rp = s->ip;

 ((void) (0));

 s->look = (lzo_uint) (s->c->in_end - s->c->ip);
 if (s->look > 0)
 {
  if (s->look > s->f)
   s->look = s->f;
  memcpy(&s->b[s->ip],s->c->ip,s->look);
  s->c->ip += s->look;
  s->ip += s->look;
 }

 if (s->ip == s->b_size)
  s->ip = 0;

 if (s->look >= 2 && s->dict_len > 0)
  swd_insertdict(s,0,s->dict_len);

 s->rp = s->first_rp;
 if (s->rp >= s->node_count)
  s->rp -= s->node_count;
 else
  s->rp += s->b_size - s->node_count;

 ((void)&i);
 ((void)&c);
 return 0;
}


static
void swd_exit(lzo1x_999_swd_t *s)
{

 ((void)&s);

}

static __inline__
void swd_getbyte(lzo1x_999_swd_t *s)
{
 int c;

 if ((c = ((*(s->c)).ip < (*(s->c)).in_end ? *((*(s->c)).ip)++ : (-1))) < 0)
 {
  if (s->look > 0)
   --s->look;






 }
 else
 {
  s->b[s->ip] = ((unsigned char) ((c) & 0xff));
  if (s->ip < s->f)
   s->b_wrap[s->ip] = ((unsigned char) ((c) & 0xff));
 }
 if (++s->ip == s->b_size)
  s->ip = 0;
 if (++s->bp == s->b_size)
  s->bp = 0;
 if (++s->rp == s->b_size)
  s->rp = 0;
}






static __inline__
void swd_remove_node(lzo1x_999_swd_t *s, lzo_uint node)
{
 if (s->node_count == 0)
 {
  lzo_uint key;

  key = (((0x9f5f*(((((lzo_uint32)s->b[node]<<5)^s->b[node+1])<<5)^s->b[node+2]))>>5) & (16384 -1));
  ((void) (0));
  --s->llen3[key];


  key = (* (unsigned short *) &(s->b[node]));
  ((void) (0));
  if ((lzo_uint) s->head2[key] == node)
   s->head2[key] = (32767 * 2 + 1);

 }
 else
  --s->node_count;
}






static
void swd_accept(lzo1x_999_swd_t *s, lzo_uint n)
{
 ((void) (0));

 while (n--)
 {
  lzo_uint key;

  swd_remove_node(s,s->rp);


  key = (((0x9f5f*(((((lzo_uint32)s->b[s->bp]<<5)^s->b[s->bp+1])<<5)^s->b[s->bp+2]))>>5) & (16384 -1));
  s->succ3[s->bp] = s->head3[key];
  s->head3[key] = ((swd_uint)(s->bp));
  s->best3[s->bp] = ((swd_uint)(s->f + 1));
  s->llen3[key]++;
  ((void) (0));



  key = (* (unsigned short *) &(s->b[s->bp]));
  s->head2[key] = ((swd_uint)(s->bp));


  swd_getbyte(s);
 }
}






static
void swd_search(lzo1x_999_swd_t *s, lzo_uint node, lzo_uint cnt)
{





 const unsigned char *p1;
 const unsigned char *p2;
 const unsigned char *px;

 lzo_uint m_len = s->m_len;
 const unsigned char * b = s->b;
 const unsigned char * bp = s->b + s->bp;
 const unsigned char * bx = s->b + s->bp + s->look;
 unsigned char scan_end1;

 ((void) (0));

 scan_end1 = bp[m_len - 1];
 for ( ; cnt-- > 0; node = s->succ3[node])
 {
  p1 = bp;
  p2 = b + node;
  px = bx;

  ((void) (0));

  if (

      p2[m_len - 1] == scan_end1 &&
      p2[m_len] == p1[m_len] &&

      p2[0] == p1[0] &&
      p2[1] == p1[1])
  {
   lzo_uint i;
   ((void) (0));

   p1 += 2; p2 += 2;
   do {} while (++p1 < px && *p1 == *++p2);

   i = p1 - bp;







   ((void) (0));


   if (i < (((8) >= (33) ? ((8) >= (9) ? (8) : (9)) : ((33) >= (9) ? (33) : (9))) + 1))
   {
    if (s->best_pos[i] == 0)
     s->best_pos[i] = node + 1;
   }

   if (i > m_len)
   {
    s->m_len = m_len = i;
    s->m_pos = node;
    if (m_len == s->look)
     return;
    if (m_len >= s->nice_length)
     return;
    if (m_len > (lzo_uint) s->best3[node])
     return;
    scan_end1 = bp[m_len - 1];
   }
  }
 }
}

static
lzo_bool swd_search2(lzo1x_999_swd_t *s)
{
 lzo_uint key;

 ((void) (0));
 ((void) (0));

 key = s->head2[ (* (unsigned short *) &(s->b[s->bp])) ];
 if (key == (32767 * 2 + 1))
  return 0;





 ((void) (0));

 if (s->best_pos[2] == 0)
  s->best_pos[2] = key + 1;


 if (s->m_len < 2)
 {
  s->m_len = 2;
  s->m_pos = key;
 }
 return 1;
}

static
void swd_findbest(lzo1x_999_swd_t *s)
{
 lzo_uint key;
 lzo_uint cnt, node;
 lzo_uint len;

 ((void) (0));


 key = (((0x9f5f*(((((lzo_uint32)s->b[s->bp]<<5)^s->b[s->bp+1])<<5)^s->b[s->bp+2]))>>5) & (16384 -1));
 node = s->succ3[s->bp] = s->head3[key];
 cnt = s->llen3[key]++;
 ((void) (0));
 if (cnt > s->max_chain && s->max_chain > 0)
  cnt = s->max_chain;
 s->head3[key] = ((swd_uint)(s->bp));

 s->b_char = s->b[s->bp];
 len = s->m_len;
 if (s->m_len >= s->look)
 {
  if (s->look == 0)
   s->b_char = -1;
  s->m_off = 0;
  s->best3[s->bp] = ((swd_uint)(s->f + 1));
 }
 else
 {

  if (swd_search2(s))

   if (s->look >= 3)
    swd_search(s,node,cnt);
  if (s->m_len > len)
   s->m_off = (s->bp > (s->m_pos) ? s->bp - (s->m_pos) : s->b_size - ((s->m_pos) - s->bp));
  s->best3[s->bp] = ((swd_uint)(s->m_len));


  if (s->use_best_off)
  {
   int i;
   for (i = 2; i < (((8) >= (33) ? ((8) >= (9) ? (8) : (9)) : ((33) >= (9) ? (33) : (9))) + 1); i++)
    if (s->best_pos[i] > 0)
     s->best_off[i] = (s->bp > (s->best_pos[i]-1) ? s->bp - (s->best_pos[i]-1) : s->b_size - ((s->best_pos[i]-1) - s->bp));
    else
     s->best_off[i] = 0;
  }

 }

 swd_remove_node(s,s->rp);



 key = (* (unsigned short *) &(s->b[s->bp]));
 s->head2[key] = ((swd_uint)(s->bp));

}








static int
init_match ( lzo1x_999_t *c, lzo1x_999_swd_t *s,
    const unsigned char *dict, lzo_uint dict_len,
    lzo_uint32 flags )
{
 int r;

 ((void) (0));
 c->init = 1;

 s->c = c;

 c->last_m_len = c->last_m_off = 0;

 c->textsize = c->codesize = c->printcount = 0;
 c->lit_bytes = c->match_bytes = c->rep_bytes = 0;
 c->lazy = 0;

 r = swd_init(s,dict,dict_len);
 if (r != 0)
  return r;

 s->use_best_off = (flags & 1) ? 1 : 0;
 return r;
}






static int
find_match ( lzo1x_999_t *c, lzo1x_999_swd_t *s,
    lzo_uint this_len, lzo_uint skip )
{
 ((void) (0));

 if (skip > 0)
 {
  ((void) (0));
  swd_accept(s, this_len - skip);
  c->textsize += this_len - skip + 1;
 }
 else
 {
  ((void) (0));
  c->textsize += this_len - skip;
 }

 s->m_len = 1;
 s->m_len = 1;

 if (s->use_best_off)
  memset(s->best_pos,0,sizeof(s->best_pos));

 swd_findbest(s);
 c->m_len = s->m_len;
 c->m_off = s->m_off;

 swd_getbyte(s);

 if (s->b_char < 0)
 {
  c->look = 0;
  c->m_len = 0;
  swd_exit(s);
 }
 else
 {
  c->look = s->look + 1;
 }
 c->bp = c->ip - c->look;

 if (c->cb && c->textsize > c->printcount)
 {
  (*c->cb)(c->textsize,c->codesize);
  c->printcount += 1024;
 }

 return 0;
}




static int
lzo1x_999_compress_internal ( const unsigned char *in , lzo_uint in_len,
                                    unsigned char *out, lzo_uint * out_len,
                                    void * wrkmem,
                              const unsigned char *dict, lzo_uint dict_len,
                                    lzo_progress_callback_t cb,
                                    int try_lazy,
                                    lzo_uint good_length,
                                    lzo_uint max_lazy,
                                    lzo_uint nice_length,
                                    lzo_uint max_chain,
                                    lzo_uint32 flags );






static unsigned char *
code_match ( lzo1x_999_t *c, unsigned char *op, lzo_uint m_len, lzo_uint m_off )
{
 lzo_uint x_len = m_len;
 lzo_uint x_off = m_off;

 c->match_bytes += m_len;

 ((void) (0));
 if (m_len == 2)
 {
  ((void) (0));
  ((void) (0)); ((void) (0));
  m_off -= 1;




  *op++ = ((unsigned char) ((0 | ((m_off & 3) << 2)) & 0xff));
  *op++ = ((unsigned char) ((m_off >> 2) & 0xff));

  c->m1a_m++;
 }



 else if (m_len <= 8 && m_off <= 0x0800)

 {
  ((void) (0));

  m_off -= 1;
  *op++ = ((unsigned char) ((((m_len - 1) << 5) | ((m_off & 7) << 2)) & 0xff));
  *op++ = ((unsigned char) ((m_off >> 3) & 0xff));
  ((void) (0));

  c->m2_m++;
 }
 else if (m_len == 3 && m_off <= (0x0400 + 0x0800) && c->r1_lit >= 4)
 {
  ((void) (0));
  ((void) (0));
  m_off -= 1 + 0x0800;




  *op++ = ((unsigned char) ((0 | ((m_off & 3) << 2)) & 0xff));
  *op++ = ((unsigned char) ((m_off >> 2) & 0xff));

  c->m1b_m++;
 }
 else if (m_off <= 0x4000)
 {
  ((void) (0));
  m_off -= 1;
  if (m_len <= 33)
   *op++ = ((unsigned char) ((32 | (m_len - 2)) & 0xff));
  else
  {
   m_len -= 33;
   *op++ = 32 | 0;
   while (m_len > 255)
   {
    m_len -= 255;
    *op++ = 0;
   }
   ((void) (0));
   *op++ = ((unsigned char) ((m_len) & 0xff));
  }




  *op++ = ((unsigned char) ((m_off << 2) & 0xff));
  *op++ = ((unsigned char) ((m_off >> 6) & 0xff));

  c->m3_m++;
 }
 else
 {
  lzo_uint k;

  ((void) (0));
  ((void) (0)); ((void) (0));
  m_off -= 0x4000;
  k = (m_off & 0x4000) >> 11;
  if (m_len <= 9)
   *op++ = ((unsigned char) ((16 | k | (m_len - 2)) & 0xff));
  else
  {
   m_len -= 9;
   *op++ = ((unsigned char) ((16 | k | 0) & 0xff));
   while (m_len > 255)
   {
    m_len -= 255;
    *op++ = 0;
   }
   ((void) (0));
   *op++ = ((unsigned char) ((m_len) & 0xff));
  }




  *op++ = ((unsigned char) ((m_off << 2) & 0xff));
  *op++ = ((unsigned char) ((m_off >> 6) & 0xff));

  c->m4_m++;
 }

 c->last_m_len = x_len;
 c->last_m_off = x_off;
 return op;
}


static unsigned char *
STORE_RUN ( lzo1x_999_t *c, unsigned char *op, const unsigned char *ii, lzo_uint t )
{
 c->lit_bytes += t;

 if (op == c->out && t <= 238)
 {
  *op++ = ((unsigned char) ((17 + t) & 0xff));
 }
 else if (t <= 3)
 {



  op[-2] |= ((unsigned char) ((t) & 0xff));

  c->lit1_r++;
 }
 else if (t <= 18)
 {
  *op++ = ((unsigned char) ((t - 3) & 0xff));
  c->lit2_r++;
 }
 else
 {
  lzo_uint tt = t - 18;

  *op++ = 0;
  while (tt > 255)
  {
   tt -= 255;
   *op++ = 0;
  }
  ((void) (0));
  *op++ = ((unsigned char) ((tt) & 0xff));
  c->lit3_r++;
 }
 do *op++ = *ii++; while (--t > 0);

 return op;
}


static unsigned char *
code_run ( lzo1x_999_t *c, unsigned char *op, const unsigned char *ii,
           lzo_uint lit, lzo_uint m_len )
{
 if (lit > 0)
 {
  ((void) (0));
  op = STORE_RUN(c,op,ii,lit);
  c->r1_m_len = m_len;
  c->r1_lit = lit;
 }
 else
 {
  ((void) (0));
  c->r1_m_len = 0;
  c->r1_lit = 0;
 }

 return op;
}






static int
len_of_coded_match ( lzo_uint m_len, lzo_uint m_off, lzo_uint lit )
{
 int n = 4;

 if (m_len < 2)
  return -1;
 if (m_len == 2)
  return (m_off <= 0x0400 && lit > 0 && lit < 4) ? 2 : -1;
 if (m_len <= 8 && m_off <= 0x0800)
  return 2;
 if (m_len == 3 && m_off <= (0x0400 + 0x0800) && lit >= 4)
  return 2;
 if (m_off <= 0x4000)
 {
  if (m_len <= 33)
   return 3;
  m_len -= 33;
  while (m_len > 255)
  {
   m_len -= 255;
   n++;
  }
  return n;
 }
 if (m_off <= 0xbfff)
 {
  if (m_len <= 9)
   return 3;
  m_len -= 9;
  while (m_len > 255)
  {
   m_len -= 255;
   n++;
  }
  return n;
 }
 return -1;
}


static lzo_int
min_gain(lzo_uint ahead, lzo_uint lit1, lzo_uint lit2, int l1, int l2, int l3)
{
 lzo_int lazy_match_min_gain = 0;

 ((void) (0));
 lazy_match_min_gain += ahead;






 if (lit1 <= 3)
  lazy_match_min_gain += (lit2 <= 3) ? 0 : 2;
 else if (lit1 <= 18)
  lazy_match_min_gain += (lit2 <= 18) ? 0 : 1;

 lazy_match_min_gain += (l2 - l1) * 2;
 if (l3 > 0)
  lazy_match_min_gain -= (ahead - l3) * 2;

 if (lazy_match_min_gain < 0)
  lazy_match_min_gain = 0;







 return lazy_match_min_gain;
}

static void
better_match ( const lzo1x_999_swd_t *swd, lzo_uint *m_len, lzo_uint *m_off )
{




 if (*m_len <= 3)
  return;

 if (*m_off <= 0x0800)
  return;



 if (*m_off > 0x0800 &&
     *m_len >= 3 + 1 && *m_len <= 8 + 1 &&
     swd->best_off[*m_len-1] && swd->best_off[*m_len-1] <= 0x0800)
 {
  *m_len = *m_len - 1;
  *m_off = swd->best_off[*m_len];
  return;
 }




 if (*m_off > 0x4000 &&
     *m_len >= 9 + 1 && *m_len <= 8 + 2 &&
     swd->best_off[*m_len-2] && swd->best_off[*m_len-2] <= 0x0800)
 {
  *m_len = *m_len - 2;
  *m_off = swd->best_off[*m_len];
  return;
 }




 if (*m_off > 0x4000 &&
     *m_len >= 9 + 1 && *m_len <= 33 + 1 &&
     swd->best_off[*m_len-1] && swd->best_off[*m_len-1] <= 0x4000)
 {
  *m_len = *m_len - 1;
  *m_off = swd->best_off[*m_len];
 }

}

 int
lzo1x_999_compress_internal ( const unsigned char *in , lzo_uint in_len,
                                    unsigned char *out, lzo_uint * out_len,
                                    void * wrkmem,
                              const unsigned char *dict, lzo_uint dict_len,
                                    lzo_progress_callback_t cb,
                                    int try_lazy,
                                    lzo_uint good_length,
                                    lzo_uint max_lazy,
                                    lzo_uint nice_length,
                                    lzo_uint max_chain,
                                    lzo_uint32 flags )
{
 unsigned char *op;
 const unsigned char *ii;
 lzo_uint lit;
 lzo_uint m_len, m_off;
 lzo1x_999_t cc;
 lzo1x_999_t * const c = &cc;
 lzo1x_999_swd_t * const swd = (lzo1x_999_swd_t *) wrkmem;
 int r;







 if (!(((lzo_uint32) (14 * 16384L * sizeof(short))) >= ((lzo_uint) (sizeof(lzo1x_999_swd_t)))))
  return (-1);



 if (try_lazy < 0)
  try_lazy = 1;

 if (good_length <= 0)
  good_length = 32;

 if (max_lazy <= 0)
  max_lazy = 32;

 if (nice_length <= 0)
  nice_length = 0;

 if (max_chain <= 0)
  max_chain = 2048;

 c->init = 0;
 c->ip = c->in = in;
 c->in_end = in + in_len;
 c->out = out;
 c->cb = cb;
 c->m1a_m = c->m1b_m = c->m2_m = c->m3_m = c->m4_m = 0;
 c->lit1_r = c->lit2_r = c->lit3_r = 0;

 op = out;
 ii = c->ip;
 lit = 0;
 c->r1_lit = c->r1_m_len = 0;

 r = init_match(c,swd,dict,dict_len,flags);
 if (r != 0)
  return r;
 if (max_chain > 0)
  swd->max_chain = max_chain;
 if (nice_length > 0)
  swd->nice_length = nice_length;

 r = find_match(c,swd,0,0);
 if (r != 0)
  return r;
 while (c->look > 0)
 {
  lzo_uint ahead;
  lzo_uint max_ahead;
  int l1, l2, l3;

  c->codesize = op - out;

  m_len = c->m_len;
  m_off = c->m_off;

  ((void) (0));
  ((void) (0));
  if (lit == 0)
   ii = c->bp;
  ((void) (0));
  ((void) (0));

  if ( m_len < 2 ||
      (m_len == 2 && (m_off > 0x0400 || lit == 0 || lit >= 4)) ||





      (m_len == 2 && op == out) ||

   (op == out && lit == 0))
  {

   m_len = 0;
  }
  else if (m_len == 3)
  {

   if (m_off > (0x0400 + 0x0800) && lit >= 4)
    m_len = 0;
  }

  if (m_len == 0)
  {

   lit++;
   swd->max_chain = max_chain;
   r = find_match(c,swd,1,0);
   ((void) (0));
   continue;
  }



  if (swd->use_best_off)
   better_match(swd,&m_len,&m_off);

  ((void)0);




  ahead = 0;
  if (try_lazy <= 0 || m_len >= max_lazy)
  {

   l1 = 0;
   max_ahead = 0;
  }
  else
  {

   l1 = len_of_coded_match(m_len,m_off,lit);
   ((void) (0));

   max_ahead = ((try_lazy) <= (l1 - 1) ? (try_lazy) : (l1 - 1));



  }


  while (ahead < max_ahead && c->look > m_len)
  {
   lzo_int lazy_match_min_gain;

   if (m_len >= good_length)
    swd->max_chain = max_chain >> 2;
   else
    swd->max_chain = max_chain;
   r = find_match(c,swd,1,0);
   ahead++;

   ((void) (0));
   ((void) (0));
   ((void) (0));






   if (c->m_len < m_len)
    continue;

   if (c->m_len == m_len && c->m_off >= m_off)
    continue;


   if (swd->use_best_off)
    better_match(swd,&c->m_len,&c->m_off);

   l2 = len_of_coded_match(c->m_len,c->m_off,lit+ahead);
   if (l2 < 0)
    continue;

   l3 = (op == out) ? -1 : len_of_coded_match(ahead,m_off,lit);




   lazy_match_min_gain = min_gain(ahead,lit,lit+ahead,l1,l2,l3);
   if (c->m_len >= m_len + lazy_match_min_gain)
   {
    c->lazy++;
    ((void)0);

    if (l3 > 0)
    {

     op = code_run(c,op,ii,lit,ahead);
     lit = 0;

     op = code_match(c,op,ahead,m_off);
    }
    else
    {
     lit += ahead;
     ((void) (0));
    }
    goto lazy_match_done;
   }
  }


  ((void) (0));


  op = code_run(c,op,ii,lit,m_len);
  lit = 0;


  op = code_match(c,op,m_len,m_off);
  swd->max_chain = max_chain;
  r = find_match(c,swd,m_len,1+ahead);
  ((void) (0));

lazy_match_done: ;
 }



 if (lit > 0)
  op = STORE_RUN(c,op,ii,lit);


 *op++ = 16 | 1;
 *op++ = 0;
 *op++ = 0;


 c->codesize = op - out;
 ((void) (0));

 *out_len = op - out;

 if (c->cb)
  (*c->cb)(c->textsize,c->codesize);







 ((void) (0));

 return 0;
}






 int
lzo1x_999_compress_level ( const unsigned char *in , unsigned in_len,
                                    unsigned char *out, unsigned * out_len,
                                    void * wrkmem,
                              const unsigned char *dict, unsigned dict_len,
                                    lzo_progress_callback_t cb,
                                    int compression_level )
{
 static const struct
 {
  int try_lazy;
  lzo_uint good_length;
  lzo_uint max_lazy;
  lzo_uint nice_length;
  lzo_uint max_chain;
  lzo_uint32 flags;
 } c[9] = {
  { 0, 0, 0, 8, 4, 0 },
  { 0, 0, 0, 16, 8, 0 },
  { 0, 0, 0, 32, 16, 0 },

  { 1, 4, 4, 16, 16, 0 },
  { 1, 8, 16, 32, 32, 0 },
  { 1, 8, 16, 128, 128, 0 },

  { 2, 8, 32, 128, 256, 0 },
  { 2, 32, 128, 2048, 2048, 1 },
  { 2, 2048, 2048, 2048, 4096, 1 }
 };

 if (compression_level < 1 || compression_level > 9)
  return (-1);

 compression_level -= 1;
 return lzo1x_999_compress_internal(in, in_len, out, out_len, wrkmem,
                                    dict, dict_len, cb,
                                    c[compression_level].try_lazy,
                                    c[compression_level].good_length,
                                    c[compression_level].max_lazy,



                                    0,

                                    c[compression_level].max_chain,
                                    c[compression_level].flags);
}
EXPORT_SYMBOL_GPL(lzo1x_999_compress_level);





 int
lzo1x_999_compress_dict ( const unsigned char *in , unsigned in_len,
                                    unsigned char *out, unsigned * out_len,
                                    void * wrkmem,
                              const unsigned char *dict, unsigned dict_len )
{
 return lzo1x_999_compress_level(in, in_len, out, out_len, wrkmem,
                                 dict, dict_len, 0, 8);
}
EXPORT_SYMBOL_GPL(lzo1x_999_compress_dict);

 int
lzo1x_999_compress ( const unsigned char *in , unsigned in_len,
                            unsigned char *out, unsigned * out_len,
                            void * wrkmem )
{
 return lzo1x_999_compress_level(in, in_len, out, out_len, wrkmem,
                                 ((void *)0), 0, 0, 8);
}
EXPORT_SYMBOL_GPL(lzo1x_999_compress);

