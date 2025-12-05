# Developer Documentation Index

Welcome to the P3_TB Syscall Interception System documentation!

## About This Project

This project implements a **syscall interception and remote execution system** using ONC RPC (Open Network Computing Remote Procedure Call) with XDR (External Data Representation) serialization. The system intercepts file I/O syscalls from client programs and forwards them to a remote server for execution, making it possible to transparently redirect file operations to a different process or machine.

## Documentation Structure

This documentation is organized to progressively introduce you to the system, starting with high-level concepts and diving into implementation details.

### 📚 Reading Order

1. **[01_INTRODUCTION.md](./01_INTRODUCTION.md)** - Start here!
   - Project overview
   - Use cases
   - Key concepts
   - Quick start guide

2. **[02_ARCHITECTURE.md](./02_ARCHITECTURE.md)**
   - System architecture
   - Component overview
   - Data flow diagrams
   - Design decisions

3. **[03_ONC_RPC_PRIMER.md](./03_ONC_RPC_PRIMER.md)** - Essential reading!
   - What is ONC RPC?
   - What is XDR?
   - How rpcgen works
   - Protocol definition

4. **[04_CODE_STRUCTURE.md](./04_CODE_STRUCTURE.md)**
   - Directory layout
   - File responsibilities
   - Module dependencies
   - Build system

5. **[05_TRANSPORT_LAYER.md](./05_TRANSPORT_LAYER.md)**
   - UNIX socket implementation
   - TCP socket implementation
   - Transport configuration
   - Connection management

6. **[06_INTERCEPTION_MECHANISM.md](./06_INTERCEPTION_MECHANISM.md)**
   - LD_PRELOAD technique
   - Function interception
   - Reentry guards
   - Thread safety

7. **[07_FD_MAPPING.md](./07_FD_MAPPING.md)**
   - Why FD mapping is needed
   - Implementation details
   - Edge cases
   - Lifecycle management

8. **[08_REQUEST_FLOW.md](./08_REQUEST_FLOW.md)**
   - Complete request lifecycle
   - PlantUML sequence diagrams
   - Error handling
   - Performance considerations

9. **[09_API_REFERENCE.md](./09_API_REFERENCE.md)**
   - RPC procedures
   - Data structures
   - Error codes
   - Usage examples

10. **[10_DEVELOPMENT_GUIDE.md](./10_DEVELOPMENT_GUIDE.md)**
    - Setting up dev environment
    - Building the project
    - Running tests
    - Adding new syscalls
    - Debugging tips

11. **[11_ADDING_NEW_SYSCALLS_TUTORIAL.md](./11_ADDING_NEW_SYSCALLS_TUTORIAL.md)** - Detailed tutorial!
    - Step-by-step guide for new syscalls
    - Complete stat() implementation example
    - Protocol definition
    - Server and client implementation
    - Testing and troubleshooting

## Quick Navigation

### By Topic

- **New to RPC?** → Start with [03_ONC_RPC_PRIMER.md](./03_ONC_RPC_PRIMER.md)
- **Want to understand the flow?** → See [08_REQUEST_FLOW.md](./08_REQUEST_FLOW.md)
- **Need to add a syscall?** → Follow [11_ADDING_NEW_SYSCALLS_TUTORIAL.md](./11_ADDING_NEW_SYSCALLS_TUTORIAL.md)
- **Need to add functionality?** → Check [10_DEVELOPMENT_GUIDE.md](./10_DEVELOPMENT_GUIDE.md)
- **Debugging issues?** → See troubleshooting in [10_DEVELOPMENT_GUIDE.md](./10_DEVELOPMENT_GUIDE.md)

### By Role

- **Product Manager** → Read 01, 02
- **New Developer** → Read 01, 02, 03, 04, 06, 08, 11
- **Maintainer** → Read all documents
- **Contributor** → Read 03, 04, 06, 08, 09, 10, 11

## Prerequisites

Before diving into this documentation, you should be familiar with:

- **C Programming**: Pointers, structures, function pointers
- **UNIX Systems**: Processes, file descriptors, syscalls
- **Networking**: Basic socket programming concepts
- **Build Tools**: Make, GCC

## Getting Help

If you have questions not covered in this documentation:

1. Check the main project documentation in `/docs/`
2. Review the code comments in `/src/`
3. Look at test cases in `/src/program.c`
4. Check `TRANSPORT_CONFIGURATION.md` for usage examples

## Contributing to Documentation

Found an error or want to improve the docs? Please:

1. Keep explanations clear and concise
2. Include code examples where appropriate
3. Update PlantUML diagrams if architecture changes
4. Add cross-references to related sections

---

**Ready to start?** → [Continue to Introduction](./01_INTRODUCTION.md)
