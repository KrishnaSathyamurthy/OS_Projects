# export COMPILE="gcc -g -w -DDEBUG_TEST"
# export RUN_CODE="gdb mem_handle.out"
export RUN_CODE="./mem_handle.out"
export COMPILE="gcc -g -w"

echo Testing 64 bit
rm -rf *.o *.a *.out
make -f Makefile ARCHITECTURE=64
echo
${COMPILE} -o mem_handle.out benchmark/test_code.c my_vm.a
readelf -h my_vm.a | grep Class
readelf -h mem_handle.out | grep Class
${RUN_CODE}

echo Testing 32 bit
rm -rf *.o *.a *.out
make -f Makefile ARCHITECTURE=32
${COMPILE} -m32 -o mem_handle.out benchmark/test_code.c my_vm.a
readelf -h my_vm.a | grep Class
readelf -h mem_handle.out | grep Class
${RUN_CODE}
