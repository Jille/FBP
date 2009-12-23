typedef int bm_datatype;

#define BM_DEFINE(m)        bm_datatype *m
#define BM_SIZE(numbits)    (((numbits) + ((sizeof(bm_datatype)*8) - 1)) / (sizeof(bm_datatype)*8))
#define BM_INIT(m, numbits) m = calloc(BM_SIZE(numbits), sizeof(bm_datatype))
#define BM_SET(m, n)        m[n/sizeof(bm_datatype)] |= (1 << (n % sizeof(bm_datatype)))
#define BM_CLR(m, n)        m[n/sizeof(bm_datatype)] &= ~(1 << (n % sizeof(bm_datatype)))
#define BM_ISSET(m, n)      ((m[n/sizeof(bm_datatype)] & (1 << (n % sizeof(bm_datatype)))) != 0)
#define BM_FREE(m)          free(m)
