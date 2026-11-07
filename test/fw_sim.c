// Mo phong END-TO-END tren may tinh: nhung CHINH firmware/main.c vao day (Pico SDK / TinyUSB thay bang stub/), bam phim vat ly gia (apply),
// chay hid_task() that va giai ma bao cao HID nhu may tinh phim My. Chay: sh test/run_tests.sh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico_stub.h"
uint8_t fake_flash[PICO_FLASH_SIZE_BYTES]; uint32_t fake_ms;
i2c_inst_t *i2c0; PIO pio0; pio_program_t ws2812_program; ioqspi_hw_t qspi_o; ioqspi_hw_t *ioqspi_hw = &qspi_o; sio_hw_t sio_o; sio_hw_t *sio_hw = &sio_o;
void flash_range_erase(uint32_t off, size_t n) { memset(fake_flash + off, 0xFF, n); }
void flash_range_program(uint32_t off, const uint8_t *d, size_t n) { memcpy(fake_flash + off, d, n); }
// ---- "may tinh": giai ma tung bao cao ban phim ----
static char host[512]; static int hn; static uint8_t prevk[6]; static bool caps;
static void hput(char c) { if (hn < (int)sizeof host - 1) host[hn++] = c; host[hn] = 0; }
static char us(uint8_t k, bool sh) {
  if (k >= 4 && k <= 29) return (sh ? 'A' : 'a') + k - 4;
  if (k >= 30 && k <= 39) return sh ? "!@#$%^&*()"[k - 30] : "1234567890"[k - 30];
  static const struct { uint8_t k; char n, s; } T[] = { {0x28,'\n','\n'}, {0x2C,' ',' '}, {0x2D,'-','_'}, {0x2E,'=','+'}, {0x2F,'[','{'}, {0x30,']','}'}, {0x31,'\\','|'}, {0x33,';',':'}, {0x34,'\'','"'}, {0x35,'`','~'}, {0x36,',','<'}, {0x37,'.','>'}, {0x38,'/','?'} };
  for (unsigned i = 0; i < sizeof T / sizeof *T; i++) if (T[i].k == k) return sh ? T[i].s : T[i].n;
  return 0;
}
static bool usb_up = true;
bool tud_hid_n_ready(uint8_t i) { (void)i; return usb_up; }
bool tud_hid_n_keyboard_report(uint8_t i, uint8_t id, uint8_t mod, const uint8_t k[6]) {
  (void)i; (void)id;
  for (int a = 0; a < 6; a++) {
    uint8_t c = k[a]; if (!c) continue;
    bool was = false; for (int j = 0; j < 6; j++) if (prevk[j] == c) was = true;
    if (was) continue;
    if (c == 0x2A) { if (hn) host[--hn] = 0; }
    else if (c == 0x39) caps = !caps;
    else { char ch = us(c, (mod & 0x22) != 0); if (ch) { if (caps && ch >= 'a' && ch <= 'z') ch -= 32; hput(ch); } }
  }
  memcpy(prevk, k, 6); return true;
}
bool tud_hid_n_report(uint8_t i, uint8_t id, const void *r, uint16_t n) { (void)i; (void)id; (void)r; (void)n; return true; }

#define main fw_main
#include "../firmware/main.c"
#undef main

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void pump(int rounds) { for (int i = 0; i < rounds; i++) { fake_ms++; hid_task(); } }
static uint32_t down;
static void press2(int p, int settle) { fake_ms += 10; down |= 1u << p; apply(down); if (settle) pump(60); }
static void release2(int p, int settle) { fake_ms += 10; down &= ~(1u << p); apply(down); if (settle) pump(60); }
#define press(p) press2(p, 1)
#define release(p) release2(p, 1)
static void tapk(int p) { press(p); release(p); }
enum { P_DOT = 15, P_DASH = 16, P_OK = 17, P_HINT = 18, P_BS = 3, P_ENT = 8, P_SPC = 13, P_CAPS = 5, P_SHIFT = 6, P_ESC = 0 };
static void morse(const char *code, int ok) { for (; *code; code++) tapk(*code == '-' ? P_DASH : P_DOT); if (ok) tapk(P_OK); }
static void expect(const char *want, const char *what) { CHECK(!strcmp(host, want), "%s: may tinh co '%s', mong '%s'", what, host, want); }
static void to_morse_layer(void) { for (int i = 0; i < 3; i++) { cycle_layer(1); pump(5); } }   // nut BOOT tren board: L0 -> L1 -> L2 -> L3
static void tapH(int p) { press2(p, 0); pump(3); release2(p, 0); pump(400); }   // bam NGAN (nha khi goi y chua xoa xong) roi cho hang doi xa het   // bam roi cho hang doi xa het (goi y: moi lan bam ~20 phim ao)
static void reset_all(void) { memset(sq_cur, 0xFF, sizeof sq_cur); morse_hint_on = 0; hn = 0; host[0] = 0; memset(prevk, 0, 6); usb_up = true; down = 0; tl = 0; memset(pressed, 0, sizeof pressed); defaults(); relayer(); morse_commit(&mrs); qh = qt = 0; tap = 0; tap_phase = 0; caps = false; tds = 0; pend = -1; tg = 0; latch_wait = false; lat = 0; pump(20); }

int main(void) {
  reset_all();
  CHECK(KM(3, 15) == KC_MORSE_DOT && KM(3, 16) == KC_MORSE_DASH && KM(3, 17) == KC_MORSE_OK, "defaults(): layer 3 chua dat phim Morse");
  { char nm[12]; lname_get(3, nm); CHECK(!strcmp(nm, "MORSE"), "ten mac dinh layer 3 = '%s'", nm); lname_get(5, nm); CHECK(!strcmp(nm, "LAYER5"), "layer khac van LAYERn"); }
  to_morse_layer();
  CHECK(res_key(P_DOT) == KC_MORSE_DOT && res_key(P_OK) == KC_MORSE_OK, "chua vao layer Morse sau 3 lan");
  CHECK(res_key(1) == 0, "phim khac bi chan");
  CHECK(res_key(4) == CONS(0xE2), "nut encoder 1 con la Mute (TRNS)");

  tapk(P_DOT); expect("e", "1 cham");
  tapk(P_DOT); expect("i", "2 cham");
  tapk(P_DASH); expect("u", "..-");
  tapk(P_DASH); expect("", "..-- khong hop le: xoa het");
  tapk(P_DOT); tapk(P_DOT); expect("?", "..--.. = ?");
  tapk(P_OK); expect("?", "Xong chot, khong xoa");
  tapk(P_DOT); expect("?e", "ky tu moi sau Xong");        // YEU CAU CUA NGUOI DUNG: sau Xong, cham phai ra e chu khong ra i
  tapk(P_BS); expect("?", "Backspace xoa ky tu dang hien");
  tapk(P_DOT); expect("?e", "sau Backspace la ky tu moi");

  reset_all(); to_morse_layer();
  morse("....", 1); morse(".", 1); morse(".-..", 1); morse(".-..", 1); morse("---", 1); tapk(P_SPC); morse(".--", 1); morse("---", 1); morse(".-.", 1); morse(".-..", 1); morse("-..", 1); tapk(P_ENT); morse("...--", 1); morse(".--.-.", 0);
  expect("hello world\n3@", "cau hoan chinh");

  reset_all(); to_morse_layer(); tapk(P_DOT); tapk(P_SPC); tapk(P_DOT); expect("e e", "Space chot ky tu"); tapk(P_DASH); expect("e a", "khong xoa dau cach");

  reset_all(); to_morse_layer();   // bam nhanh khi hang doi chua kip xa
  for (int r = 0; r < 20; r++) {
    for (const char *c = "...-..-"; *c; c++) { int p = *c == '-' ? P_DASH : P_DOT; press2(p, 0); release2(p, 0); fake_ms++; hid_task(); }
    pump(60); tapk(P_OK);
  }
  { char w[32] = {0}; for (int i = 0; i < 20; i++) w[i] = '$'; expect(w, "bam nhanh 20 lan $"); }

  reset_all(); to_morse_layer(); tapk(P_CAPS); morse("...", 1); morse("---", 1); morse("...", 0); expect("SOS", "Caps Lock"); tapk(P_OK); tapk(P_CAPS); morse(".----", 0); expect("SOS1", "so khong doi");
  reset_all(); to_morse_layer(); tapk(P_SHIFT); morse(".-", 0); tapk(P_SHIFT); CHECK(host[0] == 'A', "giu Shift (TG) -> chu hoa, co '%s'", host);

  reset_all(); to_morse_layer(); tapk(P_ESC); CHECK(res_key(P_DOT) == 0x1E + 0 || res_key(P_DOT) == (0x04 + 12), "TL(0) ve layer 0: phim 15 = '%x'", res_key(P_DOT));

  // thiet bi chua cam USB: Morse van nhat quan (hang doi cho, khong mat Backspace)
  reset_all(); to_morse_layer(); usb_up = false; tapk(P_DOT); tapk(P_DOT); pump(60); expect("", "chua cam USB: chua gui gi"); usb_up = true; pump(200); expect("i", "cam lai: ra dung 'i' (e, BS, i)");

  // Morse gan vao BAT KY phim o BAT KY layer (layer 0)
  reset_all(); KM(0, 1) = KC_MORSE_DOT; KM(0, 2) = KC_MORSE_DASH; KM(0, 3) = KC_MORSE_OK;
  tapk(1); expect("e", "layer 0 cham"); tapk(2); expect("a", ".-"); tapk(3); tapk(1); expect("ae", "Xong roi go tiep");
  // theo cau hinh cua nguoi dung: Gach = phim 0, Cham = phim '.', Xong = Enter, o layer 4 (macropad: layer 3 voi vi tri tuong tu)
  reset_all(); KM(2, 5) = KC_MORSE_DOT; KM(2, 6) = KC_MORSE_DASH; KM(2, 7) = KC_MORSE_OK; tl = 1u << 2; relayer();
  tapk(5); tapk(7); tapk(5); expect("ee", "gan layer 2: cham, Xong, cham"); tapk(7); tapk(6); expect("eet", "gach");
  // phim thuong chot ky tu
  reset_all(); KM(0, 1) = KC_MORSE_DOT; tapk(1); tapk(0); expect("ea", "phim a chot; cham tiep");   // KM(0,0) = 'a'
  tapk(1); expect("eae", "sau phim thuong, cham moi la ky tu moi");
  // encoder: quay ra Morse
  reset_all(); EN(0, 0, 0) = KC_MORSE_DOT; EN(0, 0, 1) = KC_MORSE_DASH;
  { uint16_t k = res_enc(0, 0); CHECK(k == KC_MORSE_DOT, "res_enc"); if (IS_MORSE(k)) morse_key(k); pump(60); expect("e", "encoder -> cham"); morse_key(KC_MORSE_DASH); pump(60); expect("a", "encoder -> gach"); }
  // tap dance: bam = cham, giu = gach
  reset_all(); KM(0, 1) = TD(1); TDW(1, 0) = KC_MORSE_DOT; TDW(1, 1) = KC_MORSE_DASH;
  press(1); release(1); fake_ms += 250; td_task(fake_ms); pump(60); expect("e", "tap dance: bam -> cham");
  press(1); fake_ms += 250; td_task(fake_ms); pump(60); release(1); expect("a", "tap dance: giu -> gach");
  // combo -> Morse
  reset_all(); KM(0, 1) = 0x04; KM(0, 2) = 0x05; CB(0, 0) = 0x04; CB(0, 1) = 0x05; CB(0, 2) = KC_MORSE_DASH;
  press(1); press(2); release(1); release(2); expect("t", "combo -> gach");
  // loi cu: Xong dat trong tap dance, cham NGAY sau do -> phai ra "ee", khong phai "i"
  reset_all(); KM(0, 1) = KC_MORSE_DOT; KM(0, 2) = TD(1); TDW(1, 0) = KC_MORSE_OK;
  tapk(1); expect("e", "TD-Xong: cham");
  press(2); release(2); fake_ms += 60; tapk(1); pump(60); fake_ms += 300; td_task(fake_ms); pump(60); expect("ee", "TD-Xong roi cham nhanh: Xong chay truoc");
  reset_all(); KM(0, 1) = KC_MORSE_DOT; KM(0, 2) = TD(1); TDW(1, 0) = KC_MORSE_OK;   // cuon phim
  tapk(1); press(2); fake_ms += 40; tapk(1); release(2); pump(60); fake_ms += 300; td_task(fake_ms); pump(60); expect("ee", "TD-Xong cuon phim");
  // tap dance thuong van chay dung
  reset_all(); KM(0, 2) = TD(1); TDW(1, 0) = 0x04; TDW(1, 2) = 0x05;
  press(2); release(2); fake_ms += 30; press(2); release(2); fake_ms += 300; td_task(fake_ms); pump(60); expect("b", "bam doi -> b");
  hn = 0; host[0] = 0; press(2); release(2); fake_ms += 300; td_task(fake_ms); pump(60); expect("a", "bam don -> a");
  // macro chay: chot ky tu Morse
  // chuoi SEQ: phim 1 = S1 toi, phim 2 = S1 lui, phim 3 = dat lai; buoc a, b, c (4 = het)
  reset_all(); cfg.w[SQB] = 0x04; cfg.w[SQB + 1] = 0x05; cfg.w[SQB + 2] = 0x06; KM(0, 1) = 0x7200; KM(0, 2) = 0x7280; KM(0, 3) = 0x7300;
  tapk(1); tapk(1); tapk(1); tapk(1); expect("abca", "SEQ toi: quay vong sau buoc cuoi");
  hn = 0; host[0] = 0; tapk(2); expect("c", "SEQ lui: tu a ve c (vong)"); tapk(3); tapk(1); expect("ca", "SEQ dat lai roi toi: ve buoc dau");
  CHECK(seq_res(0x7208) == 0x7208, "SEQ ngoai pham vi 0-7 giu nguyen"); cfg.w[SQB + 8] = 0x5000; sq_cur[1] = SEQ_NONE; CHECK(seq_res(0x7201) == 0, "buoc la tap dance bi bo qua");
  CHECK(seq_res(0x7202) == 0, "chuoi trong khong lam gi");
  reset_all(); cfg.w[SQB] = 0x04; cfg.w[SQB + 1] = 0x05; { uint16_t a1 = seq_res(0x7200), a2 = seq_res(0x7200), a3 = seq_res(0x7200); CHECK(a1 == 4 && a2 == 5 && a3 == 4, "SEQ encoder: 4,5,4 (co %x %x %x)", a1, a2, a3); a1 = seq_res(0x7280); CHECK(a1 == 5, "SEQ encoder nguoc"); }
  reset_all(); KM(0, 1) = KC_MORSE_DOT; tapk(1); mc_start(0); morse_commit(&mrs); tapk(1); CHECK(mrs.len == 1, "sau macro cham moi la ky tu moi");
  // doi layer giua chung: phim layer chot
  reset_all(); to_morse_layer(); tapk(P_DOT); cycle_layer(1); relayer(); tapk(P_DOT); CHECK(mrs.len == 1 || mrs.len == 0, "doi layer khong lam hong trang thai");

  // ---------- GOI Y tren van ban (" [.i -a]") ----------
  reset_all(); CHECK(KM(3, 18) == KC_MORSE_HINT && IS_MORSE(KC_MORSE_HINT), "layer 3 mac dinh co phim Goi y o vi tri 18"); CHECK(MORSE_HINT_DEF == 1, "mac dinh goi y bat");
  morse_hint_on = 1; to_morse_layer();
  tapH(P_DOT); expect("e [.i -a]", "goi y: 1 cham");
  tapH(P_DOT); expect("i [.s -u]", "goi y: 2 cham");
  tapH(P_DASH); expect("u [.f]", "goi y: ..-");
  tapH(P_OK); expect("u", "Xong chi xoa goi y");
  tapH(P_SPC); expect("u ", "Space");
  tapH(P_DOT); expect("u e [.i -a]", "ky tu moi co goi y");
  tapH(P_SPC); expect("u e ", "Space chot: goi y bi xoa TRUOC, roi moi ra dau cach");
  tapH(P_DOT); tapH(P_DASH); expect("u e a [.r -w]", ".-");
  tapH(P_BS); expect("u e ", "Backspace chot: xoa goi y roi xoa 'a' (khong xoa nham goi y)");
  tapH(P_DOT); tapH(P_ENT); expect("u e e\n", "Enter chot");
  // giu phim thuong (khong tha ngay): van ra SAU khi xoa goi y
  tapH(P_DOT); press2(P_SPC, 0); pump(400); release2(P_SPC, 0); pump(60); expect("u e e\ne ", "giu Space trong luc con goi y");
  // bam rat ngan roi nha ngay (truoc khi goi y xoa xong): phim van phai ra, khong mat, khong xoa nham
  reset_all(); morse_hint_on = 1; to_morse_layer(); tapH(P_DOT); press2(P_SPC, 0); pump(1); release2(P_SPC, 0); pump(400); expect("e ", "Space bam ngan luc con goi y");
  tapH(P_DOT); press2(P_BS, 0); pump(1); release2(P_BS, 0); pump(400); expect("e ", "Backspace bam ngan: xoa 'e' moi, khong xoa nham goi y");
  // bat / tat bang phim Goi y
  reset_all(); morse_hint_on = 1; to_morse_layer();
  tapH(P_DOT); expect("e [.i -a]", "truoc khi tat"); tapH(P_HINT); expect("e", "phim Goi y: tat -> xoa goi y"); CHECK(!morse_hint_on, "co tat");
  tapH(P_DOT); expect("i", "tat: chi con ky tu, khong goi y"); tapH(P_HINT); expect("i [.s -u]", "phim Goi y: bat lai giua chung"); CHECK(morse_hint_on, "co bat");
  tapH(P_OK); expect("i", "Xong"); tapH(P_SPC); expect("i ", "Space");
  // tat goi y: hanh vi cu giu nguyen (khong goi y, Space khong can cho)
  reset_all(); to_morse_layer(); tapk(P_DOT); tapk(P_DOT); tapk(P_SPC); expect("i ", "goi y tat: nhu cu");
  // bam rat nhanh (nguoi that bam cach nhau ~80 ms ~ 25 phim ao da xa): khong lech
  reset_all(); morse_hint_on = 1; to_morse_layer();
  for (int r = 0; r < 12; r++) { for (const char *c = "...-..-"; *c; c++) { int p = *c == '-' ? P_DASH : P_DOT; press2(p, 0); release2(p, 0); pump(120); } tapH(P_OK); }
  { char w[32] = {0}; for (int i = 0; i < 12; i++) w[i] = '$'; expect(w, "goi y bat: bam nhanh 12 lan $ (so luc bam day hang doi se hien FULL, khong lech)"); }

  printf("%d kiem tra end-to-end, %d loi\n", checks, fails);
  return fails ? 1 : 0;
}
