#! /usr/bin/bash

gcc -O2 \
  -DSQLITE_MAX_MMAP_SIZE=0 \
  -DSQLITE_OMIT_WAL \
  -DSQLITE_OMIT_SHARED_CACHE \
  -DSQLITE_OMIT_MEMLOCK \
  -DSQLITE_DISABLE_MMAP \
  shell.c sqlite3.c \
  -o sqlite3 \
  -static \
  -ldl -lpthread
