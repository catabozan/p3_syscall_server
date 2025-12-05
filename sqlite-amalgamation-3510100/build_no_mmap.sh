#! /usr/bin/bash

gcc -O2 \
  -DSQLITE_MAX_MMAP_SIZE=0 \
  -DSQLITE_OMIT_WAL \
  -DSQLITE_OMIT_SHARED_CACHE \
  -DSQLITE_OMIT_MEMLOCK \
  shell.c sqlite3.c \
  -o sqlite3 \
  -ldl -lpthread
