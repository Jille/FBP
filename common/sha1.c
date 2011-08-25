#include <assert.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <openssl/sha.h>
#include <sys/types.h>
#ifdef HAS_MMAP
#include <sys/stat.h>
#include <sys/mman.h>
#endif
#include <sys/uio.h>

void
sha1_file(char *out, int fd) {
	SHA_CTX c;
	unsigned char buf[BUFSIZ];
	ssize_t len;
	static const char hex[]="0123456789abcdef";
	int i;
#ifdef HAS_MMAP
	struct stat st;
#endif

	if(SHA1_Init(&c) == 0) {
		errno = 0;
		err(1, "SHA1_Init() failed; possible cause");
	}

#ifdef HAS_MMAP
	if(fstat(fd, &st) == -1) {
		err(1, "fstat()");
	}

	char *mdata = mmap(NULL, st.st_size, PROT_READ, MAP_NOCORE, fd, 0);
	if(mdata == MAP_FAILED) {
		goto mmap_failed;
	}
	if(SHA1_Update(&c, mdata, st.st_size) == 0) {
		errno = 0;
		err(1, "SHA1_Update() failed; possible cause");
	}
	if(munmap(mdata, st.st_size) == -1) {
		warn("munmap");
	}
	goto done;

mmap_failed:
#endif
	if(lseek(fd, 0, SEEK_SET) == -1) {
		err(1, "lseek");
	}
	while((len = read(fd, buf, sizeof(buf))) > 0) {
		if(SHA1_Update(&c, buf, len) == 0) {
			errno = 0;
			err(1, "SHA1_Update() failed; possible cause");
		}
	}
	if(len == -1) {
		err(1, "read");
	}

#ifdef HAS_MMAP
done:
#endif
	assert(sizeof(buf) > 20);
	if(SHA1_Final(buf, &c) == 0) {
		errno = 0;
		err(1, "SHA1_Final() failed; possible cause");
	}
	for(i = 0; 20 > i; i++) {
		out[i*2] = hex[buf[i] >> 4];
		out[i*2+1] = hex[buf[i] & 0x0f];
	}
}
