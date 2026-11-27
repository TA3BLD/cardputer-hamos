# Publishing HamOS in M5Burner

M5Burner (M5Stack's flashing app) lists firmware that users upload themselves.

1. Build: `pio run -e cardputer-adv`
2. Package: `sh tools/m5burner/make_package.sh` -> `dist/m5burner/hamOS_0x0.bin`, `description.txt`, `cover.png`
3. In M5Burner: log in with an M5Stack account, choose **Cardputer**, then the cloud / "Share"
   button, and upload `hamOS_0x0.bin` (it is written at address 0, the same file as in the GitHub release), the description and the cover.
4. Users then find it under Cardputer in M5Burner and install it with one click.

The same file works with any flasher at address 0, and with the browser installer (`tools/flasher`).
