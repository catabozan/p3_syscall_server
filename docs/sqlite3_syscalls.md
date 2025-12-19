# sqlite3 syscalls

## Which syscalls should I implement

- [x] 52 fcntl() - used to manipulate file descriptors
- [x] 45 pwrite64() - read from or write to a file descriptor at a given offset
- [x] 14 pread64() - read from or write to a file descriptor at a given offset 
- [x] 26 newfstatat() - get file status (again?)
- [x] 22 fstat() - get file status (again?)
- [x] 20 write()
- [x] 16 read()
- [x] 16 fsync() - synchronize a file's in-core state with storage device ? 
- [x] 16 close()
- [x] 15 openat() - open and possibly create a file
- [x] 4 unlink() - delete a name and possibly the file it refers to
- [x] 2 socket() - create an endpoint for communication -> disable (return -1)
- [x] 2 ioctl() - control device
- [x] 2 connect() - initiate a connection on a socket
- [x] 1 lseek() - reposition read/write file offset
- [x] 1 getcwd() - get current working directory
- [x] 5 access() - check user's permissions for a file (!useful ?)
- [???] 4 geteuid() - get user identity -> to be determined later, check NFS implementation
- [ ] 1 rt_sigaction() - examine and change a signal action
- [ ] 7 getpid()
- [ ] 4 brk() - change data segment size
- [ ] 3 mprotect() - set protection on a region of memory
- [ ] 1 set_tid_address() - set pointer to thread ID
- [ ] 1 set_robust_list() - get/set list of robust futexes (thread related)
- [ ] 1 rseq() - (no entry locally?) 
  glibc provides no wrapper for rseq(), necessitating the use of syscall(2)
  restartable sequences system call
- [ ] 1 prlimit64() - get/set resource limits (memory related)
- [ ] 1 getuid() - get user identity
- [ ] 1 getrandom() - obtain a series of random bytes
- [ ] 1 exit_group() - exit all threads in a process
- [ ] 1 execve() - execute program
- [ ] 1 arch_prctl() - set architecture-specific thread state