#!/bin/sh

awk '/# Install kernel/,/# End of Install kernel/' preseed/after_install_1.sh > after_install_1_kernel.sh

if [ x"${EMULATE_NVIDIA_GPU}" != x"" ]; then
    sed -i 's/^nvidia_gpu=.*/nvidia_gpu="nVidia"/' after_install_1_kernel.sh
fi
if [ x"${EMULATE_AMD_GPU}" != x"" ]; then
    sed -i 's/^amd_gpu=.*/amd_gpu="VGA_AMD"/' after_install_1_kernel.sh
fi
