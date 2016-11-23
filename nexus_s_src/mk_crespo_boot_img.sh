$ANDROID_DIR/out/host/linux-x86/bin/mkbootimg --kernel ./arch/arm/boot/zImage  --ramdisk $ANDROID_DIR/out/target/product/crespo/ramdisk.img  --cmdline 'console=ttyFIQ0 no_console_suspend' --base 0x30000000 --pagesize 4096 --output ./crespo_boot.img

