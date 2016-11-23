#make ARCH=arm panda_kdgb_defconfig
make -j4 ARCH=arm uImage 2>&1 |tee $MYDROID/logs/kernel_make.out

