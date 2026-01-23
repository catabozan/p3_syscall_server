/*
 * Comprehensive Test Program for Syscall Interception
 *
 * Refactored:
 *  - Grouped by syscall
 *  - Each syscall family tested in its own function
 *  - main() only orchestrates test execution
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/resource.h>   // For prlimit, struct rlimit, RLIMIT_*
#include <sys/ioctl.h>       // For ioctl, FIONREAD, FIONBIO
#include <termios.h>         // For TIOCGWINSZ, struct winsize
#include <linux/limits.h>    // For PATH_MAX
#include <sys/syscall.h>

#define TEST_FILE "/tmp/p3_tb_test.txt"
#define TEST_DATA "Hello from intercepted syscalls! This is a test message."
#define TEST_FILE_UNLINK "/tmp/p3_tb_test_unlink.txt"
#define TEST_FILE_UNLINKAT "/tmp/p3_tb_test_unlinkat.txt"

/* -------------------------------------------------- */
/* Utility helpers                                    */
/* -------------------------------------------------- */
/* Helper function for direct syscall output (bypasses LD_PRELOAD) */
static void write_direct(const char *msg) {
    syscall(SYS_write, STDERR_FILENO, msg, strlen(msg));
}

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
/* lseek tests                                        */
/* -------------------------------------------------- */

static int test_lseek(void)
{
    printf("[lseek] Testing lseek() and lseek64()\n");

    /* Create and write test file */
    int fd = open(TEST_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return fail("open for lseek failed");

    size_t len = strlen(TEST_DATA);
    if (write(fd, TEST_DATA, len) != (ssize_t)len)
        return fail("write for lseek failed");

    /* Test SEEK_END - move to end */
    off_t pos = lseek(fd, 0, SEEK_END);
    if (pos != (off_t)len) {
        fprintf(stderr, "ERROR: lseek SEEK_END returned %ld, expected %zu\n", pos, len);
        close(fd);
        return -1;
    }

    /* Test SEEK_SET - move to offset 10 */
    pos = lseek(fd, 10, SEEK_SET);
    if (pos != 10) {
        fprintf(stderr, "ERROR: lseek SEEK_SET returned %ld, expected 10\n", pos);
        close(fd);
        return -1;
    }

    /* Write at this position */
    const char *extra = "XXX";
    if (write(fd, extra, 3) < 0)
        return fail("write after lseek failed");

    /* Test SEEK_CUR - get current position */
    pos = lseek(fd, 0, SEEK_CUR);
    if (pos != 13) {
        fprintf(stderr, "ERROR: lseek SEEK_CUR returned %ld, expected 13\n", pos);
        close(fd);
        return -1;
    }

    /* Test SEEK_END with negative offset */
    pos = lseek(fd, -5, SEEK_END);
    if (pos < 0) {
        close(fd);
        return fail("lseek SEEK_END with negative offset failed");
    }

    /* Test lseek64 (wrapper) */
    pos = lseek64(fd, 0, SEEK_SET);
    if (pos != 0) {
        fprintf(stderr, "ERROR: lseek64 returned %ld, expected 0\n", pos);
        close(fd);
        return -1;
    }

    close(fd);

    /* Test error case: invalid fd */
    if (lseek(999, 0, SEEK_SET) != -1 || errno != EBADF) {
        fprintf(stderr, "ERROR: lseek on invalid FD did not fail correctly\n");
        return -1;
    }

    printf("  lseek/lseek64: success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* access tests                                       */
/* -------------------------------------------------- */

static int test_access(void)
{
    printf("[access] Testing access() and faccessat()\n");

    /* Ensure TEST_FILE exists */
    int fd = open(TEST_FILE, O_CREAT | O_WRONLY, 0644);
    if (fd < 0)
        return fail("open for access test failed");
    close(fd);

    /* Test F_OK - file existence */
    if (access(TEST_FILE, F_OK) != 0)
        return fail("access F_OK failed");

    /* Test R_OK - read permission */
    if (access(TEST_FILE, R_OK) != 0)
        return fail("access R_OK failed");

    /* Test W_OK - write permission */
    if (access(TEST_FILE, W_OK) != 0)
        return fail("access W_OK failed");

    /* Test faccessat wrapper */
    if (faccessat(AT_FDCWD, TEST_FILE, F_OK, 0) != 0)
        return fail("faccessat failed");

    /* Test error case: nonexistent file */
    if (access("/tmp/nonexistent_xyz_access", F_OK) == 0 || errno != ENOENT) {
        fprintf(stderr, "ERROR: access on nonexistent file did not fail correctly\n");
        return -1;
    }

    printf("  access/faccessat: success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* unlink tests                                       */
/* -------------------------------------------------- */

static int test_unlink(void)
{
    printf("[unlink] Testing unlink() and unlinkat()\n");

    /* Create first test file */
    int fd = open(TEST_FILE_UNLINK, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return fail("open for unlink test failed");

    const char *data = "test data for unlink";
    if (write(fd, data, strlen(data)) < 0) {
        close(fd);
        return fail("write for unlink test failed");
    }
    close(fd);

    /* Verify file exists */
    if (access(TEST_FILE_UNLINK, F_OK) != 0)
        return fail("file does not exist before unlink");

    /* Test unlink */
    if (unlink(TEST_FILE_UNLINK) != 0)
        return fail("unlink failed");

    /* Verify file no longer exists */
    if (access(TEST_FILE_UNLINK, F_OK) == 0 || errno != ENOENT) {
        fprintf(stderr, "ERROR: file still exists after unlink\n");
        return -1;
    }

    /* Create second test file for unlinkat */
    fd = open(TEST_FILE_UNLINKAT, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return fail("open for unlinkat test failed");
    close(fd);

    /* Test unlinkat wrapper */
    if (unlinkat(AT_FDCWD, TEST_FILE_UNLINKAT, 0) != 0)
        return fail("unlinkat failed");

    /* Verify deletion */
    if (access(TEST_FILE_UNLINKAT, F_OK) == 0 || errno != ENOENT) {
        fprintf(stderr, "ERROR: file still exists after unlinkat\n");
        return -1;
    }

    /* Test error case: try to unlink already-deleted file */
    if (unlink(TEST_FILE_UNLINK) == 0 || errno != ENOENT) {
        fprintf(stderr, "ERROR: unlink on already-deleted file did not fail correctly\n");
        return -1;
    }

    printf("  unlink/unlinkat: success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* getcwd tests                                       */
/* -------------------------------------------------- */

static int test_getcwd(void)
{
    printf("[getcwd] Testing getcwd()\n");

    /* Test with standard buffer */
    char buf[PATH_MAX];
    char *result = getcwd(buf, PATH_MAX);
    if (result == NULL)
        return fail("getcwd with buffer failed");

    if (result != buf) {
        fprintf(stderr, "ERROR: getcwd returned wrong pointer\n");
        return -1;
    }

    printf("  Current working directory: %s\n", buf);

    /* Verify path is absolute */
    if (buf[0] != '/') {
        fprintf(stderr, "ERROR: getcwd did not return absolute path\n");
        return -1;
    }

    /* Test with NULL buffer (GNU extension - malloc'd buffer) */
    char *cwd = getcwd(NULL, 0);
    if (cwd == NULL)
        return fail("getcwd with NULL buffer failed");

    printf("  CWD from NULL buffer: %s\n", cwd);

    /* Verify both paths match */
    if (strcmp(buf, cwd) != 0) {
        fprintf(stderr, "ERROR: getcwd returned different paths\n");
        free(cwd);
        return -1;
    }

    free(cwd);

    /* Test error case: buffer too small */
    char tiny[1];
    if (getcwd(tiny, 1) != NULL || errno != ERANGE) {
        fprintf(stderr, "ERROR: getcwd with tiny buffer did not fail correctly\n");
        return -1;
    }

    printf("  getcwd: success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* prlimit tests                                      */
/* -------------------------------------------------- */

static int test_prlimit(void)
{
    printf("[prlimit] Testing prlimit()\n");

    struct rlimit old_lim, new_lim, verify_lim;

    /* Test getting current RLIMIT_NOFILE */
    if (prlimit(0, RLIMIT_NOFILE, NULL, &old_lim) != 0)
        return fail("prlimit get RLIMIT_NOFILE failed");

    printf("  Current RLIMIT_NOFILE: soft=%lu, hard=%lu\n",
           (unsigned long)old_lim.rlim_cur, (unsigned long)old_lim.rlim_max);

    /* Test setting same limits (no-op, safe operation) */
    new_lim = old_lim;
    if (prlimit(0, RLIMIT_NOFILE, &new_lim, &verify_lim) != 0)
        return fail("prlimit set RLIMIT_NOFILE failed");

    /* Verify old limits match what we got */
    if (verify_lim.rlim_cur != old_lim.rlim_cur || verify_lim.rlim_max != old_lim.rlim_max) {
        fprintf(stderr, "ERROR: prlimit returned mismatched old limits\n");
        return -1;
    }

    /* Test setting without getting old value (old_limit = NULL) */
    if (prlimit(0, RLIMIT_NOFILE, &old_lim, NULL) != 0)
        return fail("prlimit set without get old failed");

    /* Verify limits were applied by getting them again */
    struct rlimit check_lim;
    if (prlimit(0, RLIMIT_NOFILE, NULL, &check_lim) != 0)
        return fail("prlimit verify after set failed");

    if (check_lim.rlim_cur != old_lim.rlim_cur || check_lim.rlim_max != old_lim.rlim_max) {
        fprintf(stderr, "ERROR: prlimit did not apply limits correctly\n");
        return -1;
    }

    printf("  prlimit: success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* ioctl tests                                        */
/* -------------------------------------------------- */

static int test_ioctl(void)
{
    printf("[ioctl] Testing ioctl() operations\n");

    /* Test FIONREAD - bytes available to read */
    int fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0)
        return fail("open for ioctl FIONREAD failed");

    int bytes_avail = 0;
    if (ioctl(fd, FIONREAD, &bytes_avail) != 0) {
        close(fd);
        return fail("ioctl FIONREAD failed");
    }

    printf("  FIONREAD: %d bytes available\n", bytes_avail);
    close(fd);

    /* Test FIONBIO - set/clear non-blocking mode */
    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0)
        return fail("open for ioctl FIONBIO failed");

    int nb = 1;
    if (ioctl(fd, FIONBIO, &nb) != 0) {
        close(fd);
        return fail("ioctl FIONBIO set failed");
    }

    nb = 0;
    if (ioctl(fd, FIONBIO, &nb) != 0) {
        close(fd);
        return fail("ioctl FIONBIO clear failed");
    }

    close(fd);

    /* Test TIOCGWINSZ - terminal window size (optional, may fail if not a TTY) */
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        printf("  TIOCGWINSZ: %dx%d (rows x cols)\n", ws.ws_row, ws.ws_col);
    } else if (errno == ENOTTY || errno == EBADF) {
        /* ENOTTY = not a terminal, EBADF = stdout not in FD translation table */
        printf("  TIOCGWINSZ: skipped (not a TTY or FD not mapped)\n");
    } else {
        return fail("ioctl TIOCGWINSZ unexpected error");
    }

    /* Test unsupported ioctl */
    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0)
        return fail("open for unsupported ioctl test failed");

    if (ioctl(fd, 0x9999, NULL) == 0 || errno != ENOTTY) {
        fprintf(stderr, "ERROR: unsupported ioctl did not fail correctly\n");
        close(fd);
        return -1;
    }

    close(fd);

    /* Test error case: invalid fd */
    if (ioctl(999, FIONREAD, &bytes_avail) != -1 || errno != EBADF) {
        fprintf(stderr, "ERROR: ioctl on invalid FD did not fail correctly\n");
        return -1;
    }

    printf("  ioctl: success\n\n");
    return 0;
}

/* -------------------------------------------------- */
/* main                                               */
/* -------------------------------------------------- */

int main(void)
{
    write_direct("SYSCALL TEST PROGRAM - CATALIN BOZAN\n");
    write_direct("Haute Ecole Arc - All rights reserved\n\n");

    int failed = 0;
    const int TEST_ITERATIONS = 1;

    printf("\n=== Syscall Interception Test Program ===\n\n");

    for(int i = 0; i < TEST_ITERATIONS; i++) {
        failed |= test_open_and_openat();
        failed |= test_write_and_pwrite();
        failed |= test_read_and_pread();
        failed |= test_stat_family();
        failed |= test_fcntl_operations();
        failed |= test_fdatasync();
        failed |= test_lseek();
        failed |= test_access();
        failed |= test_unlink();
        failed |= test_getcwd();
        failed |= test_prlimit();
        failed |= test_ioctl();
        failed |= test_error_cases();
        unlink(TEST_FILE);
    }


    write_direct("=== Test Result: ===\n");
    if (failed) {
        write_direct("SOME TESTS FAILED\n");
    } else {
        write_direct("ALL TESTS PASSED\n");
    }

    return failed ? 1 : 0;
}
