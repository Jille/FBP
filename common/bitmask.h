#define BM_DEFINE(m)        int *m
#define BM_SIZE(numbits)    numbits / sizeof(int)
#define BM_INIT(m, numbits) m = (int*)calloc(1, numbits / sizeof(int))
#define BM_SET(m, n)        m[n/sizeof(int)] |= (1 << (n % sizeof(int)))
#define BM_CLR(m, n)        m[n/sizeof(int)] &= ~(1 << (n % sizeof(int)))
#define BM_ISSET(m, n)      ((m[n/sizeof(int)] & (1 << (n % sizeof(int)))) != 0)
#define BM_FREE(m)          free(m)
