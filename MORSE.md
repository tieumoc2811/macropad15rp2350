# Go Morse 3 phim + goi y (firmware v7)

Port tu Numpad21 (nRF52). Logic thuan C trong `firmware/morse.h`, dung chung 100% voi ban Numpad21.

- 3 phim: **Cham** (`0x7060`), **Gach** (`0x7061`), **Xong** (`0x7062`). Gan duoc vao phim bat ky o layer bat ky, nut nhan encoder, xoay encoder, tap dance, ket qua combo.
- Bam cham/gach: ky tu hien NGAY tren may tinh (`.` -> e, `..` -> i, `..-` -> u). Bam them: gui Backspace xoa ky tu cu roi gui ky tu cua ma dai hon. Ma chua co ky tu (vd `..--`) chi xoa. Toi da 7 net.
- **Xong** chot ky tu (chi xoa phan goi y neu dang bat). Space / Backspace / Enter / moi phim khac / macro cung chot ky tu dang go.
- **Goi y tren van ban** (phim Morse thu 4: **Goi y**, bat/tat; mac dinh bat, doi bang `MORSE_HINT_DEF` trong `main.c`): sau ky tu go them ` [.i -a]` = them cham ra `i`, them gach ra `a`. Vi du bam `.` -> `e [.i -a]`; bam `.` nua -> `i [.s -u]`; Xong -> `i`. Phan goi y tu xoa o lan bam ke tiep, khi bam Xong, hoac khi bam bat ky phim khac (Space / Enter / Backspace... duoc giu lai den khi xoa xong, nha som van khong mat phim). Chi nhanh nao co ky tu moi hien. Trang thai bat/tat KHONG luu flash (moi lan cam USB ve `MORSE_HINT_DEF`). Layer 3 mac dinh: phim Goi y o hang duoi, cot 4. Thiet bi da co cau hinh cu: tab Ban phim -> chon phim -> nhom Morse -> Goi y.
  - Luu y: Word / Google Docs / macOS tu doi `...` thanh `…` (hoac `--` thanh gach dai) lam lech so lan Backspace -> dung o van ban thuan (Notepad, trinh duyet, terminal) hoac tat tu sua. Tat bo go tieng Viet (Unikey/Telex) khi go Morse. Giu Shift thi goi y hien dau Shift (`>I _A`), khong anh huong ket qua vi bi xoa. Hang doi phim 64 o (truoc 16); nhan qua nhanh thi OLED hien `FULL` va bo qua lan bam do.
- Layer 3 mac dinh (thiet bi moi): hang duoi `Cham Gach Xong Goi y`; cot 4: Backspace (hang 1), Enter (hang 2), Space (hang 3); hang 2: Caps Lock, giu Shift; o dau hang 1 = ve layer 0. Nut BOOT tren board bam 3 lan de vao layer 3.
- Thiet bi da co cau hinh cu: layer 3 van trong -> tab **Morse** tren trang web -> «Bo cuc Morse day du -> Layer 3» -> Luu vao thiet bi.
- OLED: hien ky tu + ma dang go (vd `E .`, `U ..-`), `OK` khi chot.
- Tap dance: khi bam phim khac luc tap dance dang cho, tap dance duoc chot NGAY theo thu tu bam (truoc day tre 200 ms nen "Xong" co the chay sau "Cham" -> `.` sau Xong ra `i` thay vi `e`).
- Kiem thu: `sh test/run_tests.sh` (gcc + node): test don vi/fuzz `morse.h`, mo phong end-to-end chinh `main.c` (stub Pico SDK / TinyUSB), doi chieu C <-> JS tren 60000 su kien.
