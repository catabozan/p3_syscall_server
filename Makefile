CC:=gcc
BUILD_FOLDER:=./build/
SRC_FOLDER:=./src/

create_folders:
	mkdir -p $(BUILD_FOLDER)

server:
	$(CC) $(SRC_FOLDER)syscall_server.c -o $(BUILD_FOLDER)syscall_server
	chmod +x $(BUILD_FOLDER)syscall_server

client:
	$(CC) -shared -fPIC -o $(BUILD_FOLDER)intercept.so $(SRC_FOLDER)intercept_client.c

program:
	$(CC) $(SRC_FOLDER)program.c -o $(BUILD_FOLDER)program
	chmod +x $(BUILD_FOLDER)program

clean:
	rm -r $(BUILD_FOLDER)*

all: create_folders server client program

run_server:
	$(BUILD_FOLDER)syscall_server

run_program:
	LD_PRELOAD=$(BUILD_FOLDER)intercept.so $(BUILD_FOLDER)program
