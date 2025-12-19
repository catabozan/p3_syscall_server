/*
 * Comprehensive Test Program for Syscall Interception
 *
 * Refactored:
 *  - Grouped by syscall
 *  - Each syscall family tested in its own function
 *  - main() only orchestrates test execution
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define TEST_FILE "/tmp/p3_tb_test.txt"
#define TEST_DATA "Hello from intercepted syscalls! This is a test message."

/* -------------------------------------------------- */
/* Utility helpers                                    */
/* -------------------------------------------------- */

static int fail(const char *msg)
{
    fprintf(stderr, "ERROR: %s: %s\n", msg, strerror(errno));
    return -1;
}

/* -------------------------------------------------- */
/* open / openat tests                                */
/* -------------------------------------------------- */

static int test_open_and_openat(void)
{
    printf("[open/openat] Testing open() and openat()\n");

    int fd = open(TEST_FILE, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return fail("open for write failed");

    printf("  open(): fd=%d\n", fd);
    close(fd);

    int fd_openat = openat(AT_FDCWD, TEST_FILE, O_CREAT | O_RDWR, 0644);
    if (fd_openat < 0)
        return fail("openat failed");

    const char *msg = "Testing openat syscall";
    if (write(fd_openat, msg, strlen(msg)) < 0)
        return fail("write after openat failed");

    close(fd_openat);
    printf("  openat(): success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* write / pwrite tests                               */
/* -------------------------------------------------- */

static int test_write_and_pwrite(void)
{
    printf("[write/pwrite] Testing write() and pwrite()\n");

    int fd = open(TEST_FILE, O_WRONLY | O_TRUNC);
    if (fd < 0)
        return fail("open for write failed");

    size_t len = strlen(TEST_DATA);

    ssize_t w = write(fd, TEST_DATA, len);
    if (w != (ssize_t)len)
        return fail("write incomplete");

    ssize_t pw = pwrite(fd, TEST_DATA, len, 0);
    if (pw != (ssize_t)len)
        return fail("pwrite incomplete");

    close(fd);
    printf("  write/pwrite: success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* read / pread tests                                 */
/* -------------------------------------------------- */

static int test_read_and_pread(void)
{
    printf("[read/pread] Testing read() and pread()\n");

    int fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0)
        return fail("open for read failed");

    char buf[256] = {0};
    ssize_t r = read(fd, buf, sizeof(buf) - 1);
    if (r < 0)
        return fail("read failed");

    if (strcmp(buf, TEST_DATA) != 0)
    {
        fprintf(stderr, "ERROR: read data mismatch\n");
        close(fd);
        return -1;
    }

    char pbuf[256] = {0};
    ssize_t pr = pread(fd, pbuf, sizeof(pbuf) - 1, 0);
    if (pr < 0)
        return fail("pread failed");

    if (strncmp(pbuf, TEST_DATA, strlen(TEST_DATA)) != 0)
    {
        fprintf(stderr, "ERROR: pread data mismatch\n");
        close(fd);
        return -1;
    }

    close(fd);
    printf("  read/pread: success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* stat-family tests                                  */
/* -------------------------------------------------- */

static int test_stat_family(void)
{
    printf("[stat] Testing stat(), fstat(), fstatat()\n");

    struct stat st;
    if (stat(TEST_FILE, &st) < 0)
        return fail("stat failed");

    if (!S_ISREG(st.st_mode))
    {
        fprintf(stderr, "ERROR: not a regular file\n");
        return -1;
    }

    int fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0)
        return fail("open for fstat failed");

    struct stat fst;
    if (fstat(fd, &fst) < 0)
    {
        close(fd);
        return fail("fstat failed");
    }

    struct stat atst;
    if (fstatat(AT_FDCWD, TEST_FILE, &atst, 0) < 0)
    {
        close(fd);
        return fail("fstatat failed");
    }

    close(fd);
    printf("  stat-family: success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* fcntl tests                                        */
/* -------------------------------------------------- */

static int test_fcntl_operations(void)
{
    printf("[fcntl] Testing fcntl operations\n");

    int fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0)
        return fail("open failed");

    /* F_DUPFD */
    int dupfd = fcntl(fd, F_DUPFD, 10);
    if (dupfd < 10)
        return fail("F_DUPFD failed");

    close(dupfd);

    /* F_GETFD / F_SETFD */
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0)
        return fail("F_GETFD failed");

    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        return fail("F_SETFD failed");

    /* F_GETFL / F_SETFL */
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0)
        return fail("F_GETFL failed");

    if (fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0)
        return fail("F_SETFL failed");

    close(fd);

    /* File locking */
    fd = open(TEST_FILE, O_RDWR);
    if (fd < 0)
        return fail("open for locking failed");

    struct flock lk = {
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = 0
    };

    if (fcntl(fd, F_SETLK, &lk) < 0)
        return fail("F_SETLK failed");

    lk.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLK, &lk) < 0)
        return fail("F_UNLCK failed");

    close(fd);
    printf("  fcntl: success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* error-path tests                                   */
/* -------------------------------------------------- */

static int test_error_cases(void)
{
    printf("[errors] Testing expected failure paths\n");

    struct stat st;
    if (stat("/tmp/nonexistent_abcdef", &st) == 0 || errno != ENOENT)
    {
        fprintf(stderr, "ERROR: stat on nonexistent file did not fail correctly\n");
        return -1;
    }

    if (fcntl(999, F_GETFD) != -1 || errno != EBADF)
    {
        fprintf(stderr, "ERROR: fcntl invalid FD did not fail correctly\n");
        return -1;
    }

    printf("  error cases: success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* fdatasync tests                                        */
/* -------------------------------------------------- */

static int test_fdatasync(void)
{
    printf("[fdatasync] Testing fdatasync()\n");

    int fd = open(TEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "ERROR: open failed: %s\n", strerror(errno));
        return -1;
    }

    ssize_t w = write(fd, TEST_DATA, strlen(TEST_DATA));
    if (w < 0 || (size_t)w != strlen(TEST_DATA)) {
        fprintf(stderr, "ERROR: write failed or incomplete: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    if (fdatasync(fd) < 0) {
        fprintf(stderr, "ERROR: fdatasync failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);

    printf("  fdatasync: success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* main                                               */
/* -------------------------------------------------- */

int main(void)
{
    int failed = 0;

    printf("\n=== Syscall Interception Test Program ===\n\n");

    failed |= test_open_and_openat();
    failed |= test_write_and_pwrite();
    failed |= test_read_and_pread();
    failed |= test_stat_family();
    failed |= test_fcntl_operations();
    failed |= test_fdatasync();

    failed |= test_error_cases();

    unlink(TEST_FILE);

    printf("=== Test Result: %s ===\n\n",
           failed ? "SOME TESTS FAILED" : "ALL TESTS PASSED");

    return failed ? 1 : 0;
}
