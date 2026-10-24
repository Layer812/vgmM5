/*
 * puff.c - Decompress zlib-like data
 * Copyright (C) 2002-2013 Mark Adler
 */

#include <setjmp.h>

#ifndef NIL
#define NIL ((unsigned char *)0)
#endif

#define local static

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define MAXCODES (MAXLCODES+MAXDCODES)
#define FIXLCODES 288

struct state {
    unsigned char *out;
    unsigned long outlen;
    unsigned long outcnt;
    const unsigned char *in;
    unsigned long inlen;
    unsigned long incnt;
    int bitbuf;
    int bitcnt;
    jmp_buf env;
    int (*write_cb)(void*, const unsigned char*, unsigned long);
    void *write_ctx;
    unsigned long window_size;
    int (*read_cb)(void*, unsigned char*, unsigned long);
    void *read_ctx;
    unsigned long in_buf_size;
};

local inline int read_byte(struct state *s) {
    if (s->incnt == s->inlen) {
        if (s->read_cb) {
            int n = s->read_cb(s->read_ctx, (unsigned char*)s->in, s->in_buf_size);
            if (n <= 0) longjmp(s->env, 1);
            s->inlen = n;
            s->incnt = 0;
        } else {
            longjmp(s->env, 1);
        }
    }
    return s->in[s->incnt++];
}

struct huffman {
    short *count;
    short *symbol;
};

local int bits(struct state *s, int need)
{
    long val;
    val = s->bitbuf;
    while (s->bitcnt < need) {
        val |= (long)(read_byte(s)) << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = (int)(val >> need);
    s->bitcnt -= need;
    return (int)(val & ((1L << need) - 1));
}

local int stored(struct state *s)
{
    unsigned len, nlen;
    s->bitbuf = 0;
    s->bitcnt = 0;
    len = read_byte(s);
    len |= read_byte(s) << 8;
    nlen = read_byte(s);
    nlen |= read_byte(s) << 8;
    if (nlen != (~len & 0xffff))
        return -2;
    if (s->out != NIL) {
        if (s->outcnt + len > s->outlen)
            return 1;
        while (len--) {
            s->out[s->window_size ? (s->outcnt % s->window_size) : s->outcnt] = read_byte(s);
            s->outcnt++;
            if (s->window_size && (s->outcnt % s->window_size) == 0) {
                if (s->write_cb(s->write_ctx, s->out, s->window_size) < 0) return 1;
            }
        }
    }
    else {
        s->outcnt += len;
        while (len--) read_byte(s);
    }
    return 0;
}

local int decode(struct state *s, const struct huffman *h)
{
    int len, code, first, count, index, bitbuf, left;
    short *next;
    bitbuf = s->bitbuf;
    left = s->bitcnt;
    code = first = index = 0;
    len = 1;
    next = h->count + 1;
    while (1) {
        while (left--) {
            code |= bitbuf & 1;
            bitbuf >>= 1;
            count = *next++;
            if (code - count < first) {
                s->bitbuf = bitbuf;
                s->bitcnt = (s->bitcnt - len) & 7;
                return h->symbol[index + (code - first)];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
            len++;
        }
        left = (MAXBITS+1) - len;
        if (left == 0)
            break;
        bitbuf = read_byte(s);
        if (left > 8)
            left = 8;
    }
    return -10;
}

local int construct(struct huffman *h, const short *length, int n)
{
    int symbol, len, left;
    short offs[MAXBITS+1];
    for (len = 0; len <= MAXBITS; len++)
        h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++)
        (h->count[length[symbol]])++;
    if (h->count[0] == n)
        return 0;
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return left;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++)
        offs[len + 1] = offs[len] + h->count[len];
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0)
            h->symbol[offs[length[symbol]]++] = symbol;
    return left;
}

local int codes(struct state *s,
                const struct huffman *lencode,
                const struct huffman *distcode)
{
    int symbol, len;
    unsigned dist;
    static const short lens[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dists[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577};
    static const short dext[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
        12, 12, 13, 13};

    do {
        symbol = decode(s, lencode);
        if (symbol < 0)
            return symbol;
        if (symbol < 256) {
            if (s->out != NIL) {
                if (s->outcnt == s->outlen)
                    return 1;
                s->out[s->window_size ? (s->outcnt % s->window_size) : s->outcnt] = symbol;
            }
            s->outcnt++;
            if (s->window_size && s->out != NIL && (s->outcnt % s->window_size) == 0) {
                if (s->write_cb(s->write_ctx, s->out, s->window_size) < 0) return 1;
            }
        }
        else if (symbol > 256) {
            symbol -= 257;
            if (symbol >= 29)
                return -10;
            len = lens[symbol] + bits(s, lext[symbol]);
            symbol = decode(s, distcode);
            if (symbol < 0)
                return symbol;
            dist = dists[symbol] + bits(s, dext[symbol]);
            if (s->out != NIL) {
                if (s->outcnt + len > s->outlen)
                    return 1;
                while (len--) {
                    s->out[s->window_size ? (s->outcnt % s->window_size) : s->outcnt] = 
                        s->out[s->window_size ? ((s->outcnt - dist) % s->window_size) : (s->outcnt - dist)];
                    s->outcnt++;
                    if (s->window_size && (s->outcnt % s->window_size) == 0) {
                        if (s->write_cb(s->write_ctx, s->out, s->window_size) < 0) return 1;
                    }
                }
            }
            else
                s->outcnt += len;
        }
    } while (symbol != 256);

    return 0;
}

local int fixed(struct state *s)
{
    static int virgin = 1;
    static short lencnt[MAXBITS+1], lensym[FIXLCODES];
    static short distcnt[MAXBITS+1], distsym[MAXDCODES];
    static struct huffman lencode, distcode;

    if (virgin) {
        int symbol;
        short lengths[FIXLCODES];
        lencode.count = lencnt;
        lencode.symbol = lensym;
        distcode.count = distcnt;
        distcode.symbol = distsym;
        for (symbol = 0; symbol < 144; symbol++)
            lengths[symbol] = 8;
        for (; symbol < 256; symbol++)
            lengths[symbol] = 9;
        for (; symbol < 280; symbol++)
            lengths[symbol] = 7;
        for (; symbol < FIXLCODES; symbol++)
            lengths[symbol] = 8;
        construct(&lencode, lengths, FIXLCODES);
        for (symbol = 0; symbol < MAXDCODES; symbol++)
            lengths[symbol] = 5;
        construct(&distcode, lengths, MAXDCODES);
        virgin = 0;
    }

    return codes(s, &lencode, &distcode);
}

local int dynamic(struct state *s)
{
    int nlen, ndist, ncode, index, err, symbol, len;
    short lengths[MAXCODES];
    short lencnt[MAXBITS+1], lensym[MAXLCODES];
    short distcnt[MAXBITS+1], distsym[MAXDCODES];
    struct huffman lencode, distcode;
    static const short order[19] =
        {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    lencode.count = lencnt;
    lencode.symbol = lensym;
    distcode.count = distcnt;
    distcode.symbol = distsym;

    nlen = bits(s, 5) + 257;
    ndist = bits(s, 5) + 1;
    ncode = bits(s, 4) + 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES)
        return -3;

    for (index = 0; index < ncode; index++)
        lengths[order[index]] = bits(s, 3);
    for (; index < 19; index++)
        lengths[order[index]] = 0;

    err = construct(&lencode, lengths, 19);
    if (err != 0)
        return -4;

    index = 0;
    while (index < nlen + ndist) {
        symbol = decode(s, &lencode);
        if (symbol < 0)
            return symbol;
        if (symbol < 16)
            lengths[index++] = symbol;
        else {
            len = 0;
            if (symbol == 16) {
                if (index == 0)
                    return -5;
                len = lengths[index - 1];
                symbol = 3 + bits(s, 2);
            }
            else if (symbol == 17)
                symbol = 3 + bits(s, 3);
            else
                symbol = 11 + bits(s, 7);
            if (index + symbol > nlen + ndist)
                return -6;
            while (symbol--)
                lengths[index++] = len;
        }
    }

    if (lengths[256] == 0)
        return -9;

    err = construct(&lencode, lengths, nlen);
    if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1]))
        return -7;

    err = construct(&distcode, lengths + nlen, ndist);
    if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1]))
        return -8;

    return codes(s, &lencode, &distcode);
}

extern "C" int puff_stream(int (*write_cb)(void*, const unsigned char*, unsigned long),
                           void* write_ctx,
                           unsigned long window_size,
                           int (*read_cb)(void*, unsigned char*, unsigned long),
                           void* read_ctx,
                           unsigned long in_buf_size,
                           unsigned char *dest,
                           unsigned long *destlen,
                           const unsigned char *source,
                           unsigned long *sourcelen)
{
    struct state s;
    int last, type, err;

    s.out = dest;
    s.outlen = destlen ? *destlen : 0;
    s.outcnt = 0;
    s.in = source;
    s.inlen = sourcelen ? *sourcelen : 0;
    s.incnt = 0;
    s.bitbuf = 0;
    s.bitcnt = 0;
    s.write_cb = write_cb;
    s.write_ctx = write_ctx;
    s.window_size = window_size;
    s.read_cb = read_cb;
    s.read_ctx = read_ctx;
    s.in_buf_size = in_buf_size;

    if (setjmp(s.env) != 0)
        err = 2;
    else {
        do {
            last = bits(&s, 1);
            type = bits(&s, 2);
            err = type == 0 ?
                    stored(&s) :
                    (type == 1 ?
                        fixed(&s) :
                        (type == 2 ?
                            dynamic(&s) :
                            -1));
            if (err != 0)
                break;
        } while (!last);
    }

    if (err <= 0) {
        if (s.window_size && s.out != NIL && (s.outcnt % s.window_size) != 0) {
            s.write_cb(s.write_ctx, s.out, s.outcnt % s.window_size);
        }
        if (destlen) *destlen = s.outcnt;
        if (sourcelen) *sourcelen = s.incnt;
    }
    return err;
}

extern "C" int puff(unsigned char *dest,
          unsigned long *destlen,
          const unsigned char *source,
          unsigned long *sourcelen)
{
    return puff_stream(0, 0, 0, 0, 0, 0, dest, destlen, source, sourcelen);
}