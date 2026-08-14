/**
 * @file inflate.c
 * @brief Implements RFC 1951 raw DEFLATE. No tables beyond the two the format
 *        itself defines. See inflate.h for why this exists.
 *
 * 한국어: RFC 1951 raw DEFLATE를 구현합니다. 형식 자체가 정의하는 두 개 외에 테이블이
 * 없습니다. 존재 이유는 inflate.h를 참조하십시오.
 */

#include "inflate.h"

/** @brief Longest Huffman code DEFLATE allows. / DEFLATE가 허용하는 최대 허프만 부호 길이. */
#define MAX_BITS   15
/** @brief Literal/length alphabet size. / 리터럴·길이 알파벳 크기. */
#define MAX_LIT    288
/** @brief Distance alphabet size. / 거리 알파벳 크기. */
#define MAX_DIST   30

/**
 * @struct State
 * @brief The two cursors and the bit accumulator one call carries.
 * / 한 번의 호출이 지니는 두 커서와 비트 누산기.
 */
typedef struct {
    const unsigned char *in;   /**< Compressed input. / 압축된 입력. */
    int in_len, in_pos;        /**< Its length, and how far it has been read. / 길이와 읽은 위치. */
    unsigned bit_buf;          /**< Bits pulled but not yet consumed. / 가져왔으나 아직 소비하지 않은 비트. */
    int bit_cnt;               /**< How many of them are valid. / 그중 유효한 비트 수. */
    unsigned char *out;        /**< Destination. / 대상 버퍼. */
    int out_cap, out_pos;      /**< Its capacity, and how far it has been written. / 용량과 기록한 위치. */
} State;

/**
 * @struct Huffman
 * @brief A canonical code, as counts per length plus symbols in canonical order.
 *
 * ENGLISH: Two small arrays rather than a decode table. Decoding walks the
 * lengths one bit at a time, which is slower per symbol and costs a few
 * hundred bytes instead of a few thousand -- the trade this project makes
 * everywhere, and it runs once at startup.
 *
 * 한국어: 해독 테이블이 아니라 길이별 개수와 정규 순서의 심볼, 두 개의 작은 배열입니다.
 * 해독은 한 번에 한 비트씩 길이를 훑으므로 심볼당 느리지만 수천 바이트가 아니라 수백
 * 바이트를 씁니다. 이 프로젝트가 어디서나 택하는 교환이며, 시작 시 한 번만 실행됩니다.
 */
typedef struct {
    short count[MAX_BITS + 1];  /**< How many codes are of each length. / 각 길이의 부호 개수. */
    short symbol[MAX_LIT];      /**< Symbols, shortest code first. / 심볼. 짧은 부호부터. */
} Huffman;

/* --------------------------------------------------------------- bit input */

/**
 * @brief Pulls `need` bits, least significant first, as DEFLATE stores them.
 * @return The value, or -1 when the input ran out.
 * / DEFLATE가 저장하는 방식대로 하위 비트부터 `need` 비트를 가져옵니다. 입력이 떨어지면 -1.
 */
static int get_bits(State *s, int need) {
    unsigned val = s->bit_buf;
    while (s->bit_cnt < need) {
        if (s->in_pos >= s->in_len) return -1;
        val |= (unsigned)s->in[s->in_pos++] << s->bit_cnt;
        s->bit_cnt += 8;
    }
    s->bit_buf = val >> need;
    s->bit_cnt -= need;
    return (int)(val & ((1u << need) - 1));
}

/* ------------------------------------------------------------ huffman codes */

/**
 * @brief Builds a canonical code from a list of code lengths.
 * @return 0 for a complete code, >0 for an incomplete one, <0 for an over-
 *         subscribed one, which is malformed.
 *
 * @note An INCOMPLETE code is legal in one case only: a distance tree with a
 *       single symbol, which a stream of literals with one match produces.
 *       Rejecting it outright breaks such streams; accepting an
 *       over-subscribed one lets a corrupt tree decode to a symbol that was
 *       never assigned.
 *
 * 한국어: 부호 길이 목록으로부터 정규 부호를 만듭니다.
 * @return 완전한 부호면 0, 불완전하면 양수, 과다 배정이면 음수(잘못된 것).
 * @note *불완전한* 부호가 적법한 경우는 하나뿐입니다. 심볼이 하나뿐인 거리 트리이며,
 *       일치가 하나인 리터럴 스트림이 이를 만듭니다. 무조건 거부하면 그런 스트림이
 *       깨지고, 과다 배정을 허용하면 손상된 트리가 배정된 적 없는 심볼로 해독됩니다.
 */
static int build(Huffman *h, const short *length, int n) {
    for (int len = 0; len <= MAX_BITS; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;          /* no codes at all */

    /* Kraft: each length doubles the code space and each code spends one. */
    int left = 1;
    for (int len = 1; len <= MAX_BITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;           /* over-subscribed */
    }

    short offs[MAX_BITS + 1];
    offs[1] = 0;
    for (int len = 1; len < MAX_BITS; len++)
        offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym]) h->symbol[offs[length[sym]]++] = (short)sym;

    return left;
}

/**
 * @brief Reads one symbol, one bit at a time, against a canonical code.
 * @return The symbol, or -1 on a code no length accounts for.
 * / 정규 부호에 대해 한 번에 한 비트씩 심볼 하나를 읽습니다. 어느 길이에도 없으면 -1.
 */
static int decode(State *s, const Huffman *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAX_BITS; len++) {
        int b = get_bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first  = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

/* ------------------------------------------------------------------ blocks */

/** @brief Base length for each literal/length symbol 257..285. / 257~285 심볼의 기본 길이. */
static const short LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
/** @brief Extra bits that follow each of those. / 그 뒤에 오는 추가 비트 수. */
static const short LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
/** @brief Base distance for each distance symbol. / 각 거리 심볼의 기본 거리. */
static const short DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
/** @brief Extra bits that follow each of those. / 그 뒤에 오는 추가 비트 수. */
static const short DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/**
 * @brief Runs one compressed block once its two trees are known.
 *
 * @note A match may reach back into bytes this same block just wrote, and may
 *       overlap itself -- `dist` of 1 with a length of 100 is a run of one
 *       byte. Copying one byte at a time is what makes that work; a memcpy
 *       would read ahead of what has been written.
 *
 * 한국어: 두 트리가 정해진 뒤 압축 블록 하나를 실행합니다.
 * @note 일치는 이 블록이 방금 기록한 바이트까지 거슬러 갈 수 있고 자기 자신과 겹칠 수도
 *       있습니다. 거리 1에 길이 100은 한 바이트의 연속입니다. 한 번에 한 바이트씩
 *       복사하는 것이 그것을 성립시킵니다. memcpy는 아직 기록되지 않은 앞을 읽습니다.
 */
static int block(State *s, const Huffman *lit, const Huffman *dist) {
    for (;;) {
        int sym = decode(s, lit);
        if (sym < 0) return -1;

        if (sym < 256) {                       /* a literal byte */
            if (s->out_pos >= s->out_cap) return -1;
            s->out[s->out_pos++] = (unsigned char)sym;
            continue;
        }
        if (sym == 256) return 0;              /* end of block */

        sym -= 257;
        if (sym >= 29) return -1;
        int extra = get_bits(s, LEN_EXTRA[sym]);
        if (extra < 0) return -1;
        int len = LEN_BASE[sym] + extra;

        int dsym = decode(s, dist);
        if (dsym < 0 || dsym >= 30) return -1;
        extra = get_bits(s, DIST_EXTRA[dsym]);
        if (extra < 0) return -1;
        int back = DIST_BASE[dsym] + extra;

        if (back > s->out_pos) return -1;      /* before the start of output */
        if (len > s->out_cap - s->out_pos) return -1;

        for (int i = 0; i < len; i++, s->out_pos++)
            s->out[s->out_pos] = s->out[s->out_pos - back];
    }
}

/** @brief Builds the pair of trees DEFLATE defines for a fixed block. / 고정 블록에 대해 DEFLATE가 정의하는 두 트리를 만듭니다. */
static int fixed_trees(Huffman *lit, Huffman *dist) {
    short len[MAX_LIT];
    int i = 0;
    for (; i < 144; i++) len[i] = 8;
    for (; i < 256; i++) len[i] = 9;
    for (; i < 280; i++) len[i] = 7;
    for (; i < 288; i++) len[i] = 8;
    if (build(lit, len, 288) < 0) return -1;
    for (i = 0; i < MAX_DIST; i++) len[i] = 5;
    /* 30 rather than 32: the two unused codes would make this over-subscribed
       under a strict Kraft test, and every real encoder emits only 30.
       32가 아니라 30입니다. 쓰이지 않는 두 부호는 엄격한 Kraft 검사에서 과다 배정이
       되며, 실제 인코더는 모두 30개만 냅니다. */
    if (build(dist, len, MAX_DIST) < 0) return -1;
    return 0;
}

/** @brief Order the code-length alphabet's lengths arrive in. / 부호 길이 알파벳의 길이가 도착하는 순서. */
static const short CLEN_ORDER[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

/** @brief Reads the two trees a dynamic block carries in front of its data. / 동적 블록이 데이터 앞에 싣고 오는 두 트리를 읽습니다. */
static int dynamic_trees(State *s, Huffman *lit, Huffman *dist) {
    int nlen  = get_bits(s, 5); if (nlen  < 0) return -1; nlen  += 257;
    int ndist = get_bits(s, 5); if (ndist < 0) return -1; ndist += 1;
    int ncode = get_bits(s, 4); if (ncode < 0) return -1; ncode += 4;
    if (nlen > 286 || ndist > 30) return -1;

    short len[MAX_LIT + MAX_DIST];
    for (int i = 0; i < 19; i++) len[i] = 0;
    for (int i = 0; i < ncode; i++) {
        int v = get_bits(s, 3);
        if (v < 0) return -1;
        len[CLEN_ORDER[i]] = (short)v;
    }

    Huffman clen;
    if (build(&clen, len, 19) != 0) return -1;   /* must be complete */

    int i = 0;
    while (i < nlen + ndist) {
        int sym = decode(s, &clen);
        if (sym < 0) return -1;

        if (sym < 16) { len[i++] = (short)sym; continue; }

        int rep, val = 0;
        if (sym == 16) {
            if (i == 0) return -1;               /* nothing to repeat */
            val = len[i - 1];
            rep = get_bits(s, 2); if (rep < 0) return -1; rep += 3;
        } else if (sym == 17) {
            rep = get_bits(s, 3); if (rep < 0) return -1; rep += 3;
        } else {
            rep = get_bits(s, 7); if (rep < 0) return -1; rep += 11;
        }
        if (i + rep > nlen + ndist) return -1;
        while (rep--) len[i++] = (short)val;
    }

    if (len[256] == 0) return -1;                /* no end-of-block code */
    if (build(lit, len, nlen) < 0) return -1;
    if (build(dist, len + nlen, ndist) < 0) return -1;
    return 0;
}

/* -------------------------------------------------------------------- api */

int inflate_raw(unsigned char *dst, int dst_cap,
                const unsigned char *src, int src_len) {
    if (!dst || !src || dst_cap < 0 || src_len < 0) return -1;

    State s;
    s.in = src; s.in_len = src_len; s.in_pos = 0;
    s.bit_buf = 0; s.bit_cnt = 0;
    s.out = dst; s.out_cap = dst_cap; s.out_pos = 0;

    int last;
    do {
        last = get_bits(&s, 1);
        if (last < 0) return -1;
        int type = get_bits(&s, 2);
        if (type < 0) return -1;

        if (type == 0) {
            /* Stored: byte-aligned, with a length and its complement. */
            s.bit_buf = 0; s.bit_cnt = 0;
            if (s.in_pos + 4 > s.in_len) return -1;
            int n  = s.in[s.in_pos] | (s.in[s.in_pos + 1] << 8);
            int nc = s.in[s.in_pos + 2] | (s.in[s.in_pos + 3] << 8);
            if ((n ^ 0xffff) != nc) return -1;
            s.in_pos += 4;
            if (s.in_pos + n > s.in_len)    return -1;
            if (n > s.out_cap - s.out_pos)  return -1;
            for (int i = 0; i < n; i++) s.out[s.out_pos++] = s.in[s.in_pos++];
        } else if (type == 1) {
            Huffman lit, dist;
            if (fixed_trees(&lit, &dist) < 0) return -1;
            if (block(&s, &lit, &dist) < 0)   return -1;
        } else if (type == 2) {
            Huffman lit, dist;
            if (dynamic_trees(&s, &lit, &dist) < 0) return -1;
            if (block(&s, &lit, &dist) < 0)         return -1;
        } else {
            return -1;                        /* reserved */
        }
    } while (!last);

    return s.out_pos;
}
