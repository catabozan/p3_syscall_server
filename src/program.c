/*
 * Comprehensive Test Program for Syscall Interception
 *
 * Tests open(), write(), close(), read() syscalls through RPC interception
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
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

cleanup:
    /* Print final result */
    printf("=== Test Result: %s ===\n\n",
           test_passed ? "ALL TESTS PASSED" : "SOME TESTS FAILED");

    /* Clean up test file */
    unlink(TEST_FILE);

    return test_passed ? 0 : 1;
}
