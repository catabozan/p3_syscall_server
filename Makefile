CC := gcc
BUILD_FOLDER := ./build/
SRC_FOLDER := ./src/
PROTOCOL_FOLDER := $(SRC_FOLDER)protocol/

# RPC/XDR configuration
RPCGEN := rpcgen
PROTOCOL_X := $(PROTOCOL_FOLDER)protocol.x
PROTOCOL_H := $(PROTOCOL_FOLDER)protocol.h
PROTOCOL_XDR := $(PROTOCOL_FOLDER)protocol_xdr.c
PROTOCOL_CLNT := $(PROTOCOL_FOLDER)protocol_clnt.c
PROTOCOL_SVC := $(PROTOCOL_FOLDER)protocol_svc.c

# Try to use libtirpc if available, otherwise use legacy RPC
RPC_CFLAGS := $(shell pkg-config --cflags libtirpc 2>/dev/null || echo "")
RPC_LDFLAGS := $(shell pkg-config --libs libtirpc 2>/dev/null || echo "-lnsl")

# Compiler flags
CFLAGS := -Wall -g $(RPC_CFLAGS) -I$(PROTOCOL_FOLDER) -I$(SRC_FOLDER)
LDFLAGS := $(RPC_LDFLAGS)

# Generated files
GENERATED_FILES := $(PROTOCOL_H) $(PROTOCOL_XDR) $(PROTOCOL_CLNT) $(PROTOCOL_SVC)

.PHONY: all clean create_folders rpc_gen server client program run_server run_program

all: create_folders rpc_gen server client program
all_temp: create_folders rpc_gen server client program_temp

create_folders:
	mkdir -p $(BUILD_FOLDER)

# Generate RPC stubs from protocol.x
rpc_gen: $(GENERATED_FILES)

$(PROTOCOL_H) $(PROTOCOL_XDR) $(PROTOCOL_CLNT) $(PROTOCOL_SVC): $(PROTOCOL_X)
	cd $(PROTOCOL_FOLDER) && $(RPCGEN) -C protocol.x
	@# Wrap main() in protocol_svc.c with RPC_SVC_FG guard
	@sed -i '/^int$$/,/^}$$/{s/^int$$/\#ifndef RPC_SVC_FG\nint/; s/^}$$/}\n\#endif \/\* RPC_SVC_FG \*\//; }' $(PROTOCOL_SVC)
	@# Make syscall_prog_1 non-static so we can call it from our main()
	@sed -i 's/^static void$$/void/' $(PROTOCOL_SVC)

# Build RPC server
server: rpc_gen
	$(CC) $(CFLAGS) -c -o $(BUILD_FOLDER)protocol_xdr.o $(PROTOCOL_XDR)
	$(CC) $(CFLAGS) -c -o $(BUILD_FOLDER)protocol_svc.o $(PROTOCOL_SVC) -DRPC_SVC_FG
	$(CC) $(CFLAGS) -o $(BUILD_FOLDER)syscall_server \
		$(SRC_FOLDER)rpc_server.c \
		$(BUILD_FOLDER)protocol_xdr.o \
		$(BUILD_FOLDER)protocol_svc.o \
		$(LDFLAGS)
	chmod +x $(BUILD_FOLDER)syscall_server

# Build interception client library
client: rpc_gen
	$(CC) $(CFLAGS) -shared -fPIC -o $(BUILD_FOLDER)intercept.so \
		$(SRC_FOLDER)rpc_client.c \
		$(PROTOCOL_XDR) \
		$(PROTOCOL_CLNT) \
		$(LDFLAGS) -ldl

# Build test program
program: create_folders
	$(CC) -o $(BUILD_FOLDER)program $(SRC_FOLDER)program.c
	chmod +x $(BUILD_FOLDER)program

program_temp: create_folders
	$(CC) -o $(BUILD_FOLDER)program $(SRC_FOLDER)program_temp.c
	chmod +x $(BUILD_FOLDER)program

clean:
	rm -rf $(BUILD_FOLDER)*
	rm -f $(GENERATED_FILES)

run_server:
	$(BUILD_FOLDER)syscall_server

run_program:
	LD_PRELOAD=$(BUILD_FOLDER)intercept.so $(BUILD_FOLDER)program

run_sqlite:
	LD_PRELOAD=$(BUILD_FOLDER)intercept.so ./sqlite-src-3510100/sqlite3 test.db
