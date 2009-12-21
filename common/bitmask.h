#define	BM_DEFINE(m)	int *m
#define	BM_INIT(m, numbits)	m = calloc(1, numbits / sizeof(int))
#define BM_SET(m, n) m[n/sizeof(int)] |= (1 << (n % sizeof(int)))
#define BM_CLR(m, n) m[n/sizeof(int)] &= ~(1 << (n % sizeof(int)))
#define BM_ISSET(m, n) ((m[n/sizeof(int)] & (1 << (n % sizeof(int)))) != 0)
