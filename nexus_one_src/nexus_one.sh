./build.sh
$ANDROID_DIR/out/host/linux-x86/bin/adb reboot-bootloader
sudo $ANDROID_DIR/out/host/linux-x86/bin/fastboot boot arch/arm/boot/zImage
