## task 4
- [ ] check syscalls for sqlite
- [ ] chunk buffers in protocol calls when client asks for buff bigger than protocol limit

## fixes AI code
- [ ] rpc_client.c@88 - svcaddr.len = svcaddr.maxlen = sizeof(server_addr);
- [ ] rpc_server.c@remove_fd_mapping() - check for closed FDs and reuse instead of increasing index
- [ ] look up what are variadic arguments like va_list, va_start, va_arg, va_end

## task 1

- [x] intercept syscalls, maybe replace some libc functions
- [x] only write() for ex.

# task 2

- [x] implement serialization - **COMPLETED with ONC RPC/XDR**
- [x] implement RPC-based client-server architecture
- [x] support UNIX domain sockets (working)
- [x] support TCP sockets (implemented, requires rpcbind)
- [x] implement FD mapping between client and server
- [x] intercept open(), close(), read(), write() syscalls
- [x] comprehensive test program with data integrity verification

# task 3 (future)

- [ ] implement interface similar to PAL
- [ ] maybe automatic interface generation from PAL definitions
  - [ ] generate c code with includes and go through code tree to detect interface functions (abstract syntax tree)
