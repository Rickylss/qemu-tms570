qemu-system-arm -M tms570-ls3137 -no-reboot -serial telnet:localhost:9000,server -apptestaddr C:\msys32\home\Tang\misc\ppc755\mytest\bios.bin,0xfff00000   -apptestaddr  C:\msys32\home\Tang\misc\ppc755\Qemu755Test\os.bin,0x1000000 -s -S
#qemu-system-arm -M tms570-ls3137 -no-reboot -serial telnet:localhost:9000,server -kernel C:\msys32\home\Tang\misc\ppc755\mytest\bios.bin -s -S
#qemu-system-ppc -M mpc5675 -no-reboot -serial telnet:localhost:9000,server -apptestaddr C:\msys32\home\Tang\misc\ppc755\mytest\bios.bin,0xfff00000   -apptestaddr  C:\msys32\home\Tang\misc\ppc755\Qemu755Test\os.bin,0x1000000 -s -S
#qemu-system-ppc -M ppc755 -no-reboot -serial telnet:localhost:9000,server -serial telnet:localhost:9001,server -apptestaddr C:\msys32\home\Tang\misc\ppc755\mytest\bios.bin,0xfff00000   -apptestaddr  C:\msys32\home\Tang\misc\ppc755\Qemu755Test\os.bin,0x1000000 -s -S
pause>nul