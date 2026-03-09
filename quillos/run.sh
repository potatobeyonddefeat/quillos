make

xorriso -as mkisofs \
-b limine-bios-cd.bin \
-no-emul-boot \
-boot-load-size 4 \
-boot-info-table \
--efi-boot limine-uefi-cd.bin \
-efi-boot-part --efi-boot-image \
--protective-msdos-label \
iso -o quillos.iso

./limine/limine.exe bios-install quillos.iso

qemu-system-x86_64 -cdrom quillos.iso
