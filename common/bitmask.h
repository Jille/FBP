typedef int bm_datatype;
typedef int bm_bitid;

#define BM_DEFINE(m)        bm_datatype *m
#define BM_BITS_PER_UNIT    (sizeof(bm_datatype)*8)
#define BM_SIZE(numbits)    (((numbits) + (BM_BITS_PER_UNIT - 1)) / BM_BITS_PER_UNIT)
// Cast is necessary to make the C++ compiler happy
#define BM_INIT(m, numbits) m = (bm_datatype*)calloc(BM_SIZE(numbits), sizeof(bm_datatype))
#define BM_SET(m, n)        m[n/BM_BITS_PER_UNIT] |= (1 << (n % BM_BITS_PER_UNIT))
#define BM_CLR(m, n)        m[n/BM_BITS_PER_UNIT] &= ~(1 << (n % BM_BITS_PER_UNIT))
#define BM_ISSET(m, n)      ((m[n/BM_BITS_PER_UNIT] & (1 << (n % BM_BITS_PER_UNIT))) != 0)
#define BM_FREE(m)          free(m)

static inline bm_bitid
bm_find_setbit(bm_datatype *m, bm_bitid numbits, bm_bitid offset) {
	bm_bitid n = offset;
	while(!BM_ISSET(m, n)) {
		n = (n+1) % numbits;
		if(n == offset) {
			return -1;
		}
	}

	return n;
}

static inline bm_bitid
bm_find_clrbit(bm_datatype *m, bm_bitid numbits, bm_bitid offset) {
	bm_bitid n = offset;
	while(BM_ISSET(m, n)) {
		n = (n+1) % numbits;
		if(n == offset) {
			return -1;
		}
	}

	return n;
}
