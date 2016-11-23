./build.sh
$ANDROID_DIR/out/host/linux-x86/bin/adb reboot-bootloader
./mk_crespo_boot_img.sh
sudo $ANDROID_DIR/out/host/linux-x86/bin/fastboot boot crespo_boot.img

