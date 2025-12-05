/*
 * Comprehensive Test Program for Syscall Interception
 *
 * Tests open(), write(), close(), read(), stat() syscalls through RPC interception
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

int main() {
    int fd;
    ssize_t bytes;
    char read_buffer[256];
    int test_passed = 1;

    printf("\n=== Syscall Interception Test Program ===\n\n");

    /* Test 1: Open file for writing */
    printf("[Test 1] Opening file for writing: %s\n", TEST_FILE);
    fd = open(TEST_FILE, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Failed to open file for writing: %s\n", strerror(errno));
        test_passed = 0;
        goto cleanup;
    }
    printf("SUCCESS: Opened file with fd=%d\n\n", fd);

    /* Test 2: Write data to file */
    printf("[Test 2] Writing data to file...\n");
    size_t data_len = strlen(TEST_DATA);
    bytes = write(fd, TEST_DATA, data_len);
    if (bytes < 0) {
        fprintf(stderr, "ERROR: Failed to write to file: %s\n", strerror(errno));
        test_passed = 0;
        close(fd);
        goto cleanup;
    }
    if ((size_t)bytes != data_len) {
        fprintf(stderr, "ERROR: Partial write: wrote %zd bytes, expected %zu\n",
                bytes, data_len);
        test_passed = 0;
        close(fd);
        goto cleanup;
    }
    printf("SUCCESS: Wrote %zd bytes\n\n", bytes);

    /* Test 3: Close file */
    printf("[Test 3] Closing file...\n");
    if (close(fd) < 0) {
        fprintf(stderr, "ERROR: Failed to close file: %s\n", strerror(errno));
        test_passed = 0;
        goto cleanup;
    }
    printf("SUCCESS: File closed\n\n");

    /* Test 4: Open file for reading */
    printf("[Test 4] Opening file for reading: %s\n", TEST_FILE);
    fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "ERROR: Failed to open file for reading: %s\n", strerror(errno));
        test_passed = 0;
        goto cleanup;
    }
    printf("SUCCESS: Opened file with fd=%d\n\n", fd);

    /* Test 5: Read data from file */
    printf("[Test 5] Reading data from file...\n");
    memset(read_buffer, 0, sizeof(read_buffer));
    bytes = read(fd, read_buffer, sizeof(read_buffer) - 1);
    if (bytes < 0) {
        fprintf(stderr, "ERROR: Failed to read from file: %s\n", strerror(errno));
        test_passed = 0;
        close(fd);
        goto cleanup;
    }
    printf("SUCCESS: Read %zd bytes\n", bytes);
    printf("Data read: \"%s\"\n\n", read_buffer);

    /* Test 6: Verify data integrity */
    printf("[Test 6] Verifying data integrity...\n");
    if (strcmp(read_buffer, TEST_DATA) != 0) {
        fprintf(stderr, "ERROR: Data mismatch!\n");
        fprintf(stderr, "Expected: \"%s\"\n", TEST_DATA);
        fprintf(stderr, "Got:      \"%s\"\n", read_buffer);
        test_passed = 0;
        close(fd);
        goto cleanup;
    }
    printf("SUCCESS: Data matches!\n\n");

    /* Test 7: Close file again */
    printf("[Test 7] Closing file...\n");
    if (close(fd) < 0) {
        fprintf(stderr, "ERROR: Failed to close file: %s\n", strerror(errno));
        test_passed = 0;
        goto cleanup;
    }
    printf("SUCCESS: File closed\n\n");

    /* Test 8: stat() on existing file */
    printf("[Test 8] Getting file statistics: %s\n", TEST_FILE);
    struct stat statbuf;
    if (stat(TEST_FILE, &statbuf) < 0) {
        fprintf(stderr, "ERROR: Failed to stat file: %s\n", strerror(errno));
        test_passed = 0;
        goto cleanup;
    }
    printf("SUCCESS: stat() returned:\n");
    printf("  File mode: %o\n", statbuf.st_mode);
    printf("  File size: %ld bytes\n", (long)statbuf.st_size);
    printf("  Last access time: %ld\n", (long)statbuf.st_atime);
    printf("  Last modification time: %ld\n", (long)statbuf.st_mtime);
    printf("  Last status change time: %ld\n\n", (long)statbuf.st_ctime);

    /* Test 9: Verify stat results */
    printf("[Test 9] Verifying stat results...\n");
    if ((size_t)statbuf.st_size != data_len) {
        fprintf(stderr, "ERROR: File size mismatch! Expected %zu, got %ld\n",
                data_len, (long)statbuf.st_size);
        test_passed = 0;
        goto cleanup;
    }
    if (!S_ISREG(statbuf.st_mode)) {
        fprintf(stderr, "ERROR: File is not a regular file!\n");
        test_passed = 0;
        goto cleanup;
    }
    printf("SUCCESS: File size and type verified\n\n");

    /* Test 10: stat() on non-existent file */
    printf("[Test 10] Testing stat() on non-existent file...\n");
    const char *nonexistent_file = "/tmp/p3_tb_nonexistent_file_xyz123.txt";
    if (stat(nonexistent_file, &statbuf) == 0) {
        fprintf(stderr, "ERROR: stat() succeeded on non-existent file!\n");
        test_passed = 0;
        goto cleanup;
    }
    if (errno != ENOENT) {
        fprintf(stderr, "ERROR: Wrong errno for non-existent file: expected ENOENT (%d), got %d\n",
                ENOENT, errno);
        test_passed = 0;
        goto cleanup;
    }
    printf("SUCCESS: stat() correctly failed with ENOENT\n\n");

cleanup:
    /* Print final result */
    printf("=== Test Result: %s ===\n\n",
           test_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED");

    /* Clean up test file */
    unlink(TEST_FILE);

    return test_passed ? 0 : 1;
}
