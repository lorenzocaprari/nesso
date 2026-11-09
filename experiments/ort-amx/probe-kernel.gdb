set pagination off
set confirm off
set breakpoint pending on
break MlasGemmU8S8CopyPackBAmx
break MlasGemmU8S8KernelAvx512Vnni
run
printf "first_stop\n"
quit
