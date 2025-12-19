/* Test 8b: fstat() on existing file */
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define TEST_FILE "/home/catab/hearc/IL3/P3_TB/meow.txt"

int main()
{
    printf("[Test 8b] Getting file statistics using fstat: %s\n", TEST_FILE);

    /* Open the file for reading (or any mode you need) */
    int fd = open(TEST_FILE, O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "ERROR: Failed to open file: %s\n", strerror(errno));
        return 1;
    }

    /* Get file statistics using the file descriptor */
    struct stat statbuf;
    if (fstat(fd, &statbuf) < 0)
    {
        fprintf(stderr, "ERROR: Failed to fstat file: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("SUCCESS: fstat() returned:\n");
    printf("  File mode: %o\n", statbuf.st_mode);
    printf("  File size: %ld bytes\n", (long)statbuf.st_size);
    printf("  Last access time: %ld\n", (long)statbuf.st_atime);
    printf("  Last modification time: %ld\n", (long)statbuf.st_mtime);
    printf("  Last status change time: %ld\n\n", (long)statbuf.st_ctime);

    /* Close the file when done */
    close(fd);

    return 0;
}
