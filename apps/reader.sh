#!/bin/bash
make clean
make
dd if=/dev/urandom of=test_file bs=4096 count=1
./fs_make.x test.fs 100
./fs_ref.x script test.fs scripts/example.script
./test_fs.x script test.fs scripts/test.script
