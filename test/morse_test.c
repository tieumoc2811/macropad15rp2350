// Kiem thu logic Morse (firmware/numpad21/morse.h) tren may tinh, khong can mach.
//   morse_test            : chay bo test tu dong
//   morse_test --stream   : doc su kien tu stdin (d=cham h=gach o=xong s=Space b=Backspace), in noi dung may tinh sau moi su kien (de so voi ban JS)
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../firmware/morse.h"
#include "build/mc_ascii.inc"   // trich NGUYEN VAN ham mc_ascii tu main.c (xem run_tests.sh)

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---------- "may tinh": ap dung Backspace va ky tu ----------
static char host[65536]; static int hn;
static void host_clear(void) { hn = 0; host[0] = 0; }
static void host_bs(void) { if (hn) host[--hn] = 0; }
static void host_ch(char c) { if (hn < (int)sizeof host - 2) { host[hn++] = c; host[hn] = 0; } }
static uint8_t big_room(void) { return 200; }
static void io_bs(void) { host_bs(); }
static void io_put(char c) { host_ch(c); }
static const morse_io IO = { io_bs, io_put, big_room };

// ---------- hang doi phim THAT (giong qpush_raw/qh/qt trong .ino) + "may tinh" lay ra tung phim ----------
static uint16_t q[16]; static uint8_t qh, qt;
static void qpush_raw(uint16_t k) { if ((uint8_t)(qh - qt) < 16) q[qh++ & 15] = k; }
static uint8_t q_room(void) { return 16 - (uint8_t)(qh - qt); }
static void q_bs(void) { qpush_raw(0x2A); }
static void q_put(char c) { uint16_t k = mc_ascii((uint8_t)c); if (k) qpush_raw(k); }
static const morse_io QIO = { q_bs, q_put, q_room };
static char hid_char(uint16_t k);   // ben duoi
static void q_drain_one(void) {     // 1 lan "go phim" ra may tinh
  if (qh == qt) return;
  uint16_t k = q[qt++ & 15];
  if (k == 0x2A) host_bs(); else { char c = hid_char(k); CHECK(c, "ma HID 0x%X khong ra ky tu", k); if (c) host_ch(c); }
}

// ---------- ban phim My (US) doc lap de doi chieu mc_ascii ----------
static char hid_char(uint16_t k) {
  int sh = (k & 0x2000) != 0; k &= 0xFF;
  if (k >= 4 && k <= 29) return (char)((sh ? 'A' : 'a') + k - 4);
  if (k >= 30 && k <= 39) return sh ? "!@#$%^&*()"[k - 30] : "1234567890"[k - 30];
  static const struct { uint8_t k; char n, s; } T[] = { {0x28,'\n','\n'}, {0x2B,'\t','\t'}, {0x2C,' ',' '}, {0x2D,'-','_'}, {0x2E,'=','+'}, {0x2F,'[','{'}, {0x30,']','}'},
    {0x31,'\\','|'}, {0x33,';',':'}, {0x34,'\'','"'}, {0x35,'`','~'}, {0x36,',','<'}, {0x37,'.','>'}, {0x38,'/','?'} };
  for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) if (T[i].k == k) return sh ? T[i].s : T[i].n;
  return 0;
}

static void type_code(morse_t *m, const char *code) { for (; *code; code++) morse_press(m, *code == '-', &IO); }
static const char *CODE_OF(char c) { for (int i = 0; i < MORSE_N; i++) if (MORSE_TAB[i].ch == c) return MORSE_TAB[i].code; return NULL; }

// ---------- mo hinh tham chieu DOC LAP: chu da chot + ky tu cua ma dang go ----------
typedef struct { char done[1024]; int dn; char code[16]; int len; } ref_t;
static void ref_press(ref_t *r, int dash) { if (r->len < MORSE_MAX) { r->code[r->len++] = dash ? '-' : '.'; r->code[r->len] = 0; } }
static void ref_commit(ref_t *r) { r->len = 0; r->code[0] = 0; }
static void ref_expect(const ref_t *r, char *out) {
  strcpy(out, r->done); char c = r->len ? morse_find(r->code) : 0; if (c) { int n = (int)strlen(out); out[n] = c; out[n + 1] = 0; }
}

static void test_table(void) {
  for (int i = 0; i < MORSE_N; i++) {
    const char *c = MORSE_TAB[i].code; int n = (int)strlen(c);
    CHECK(n >= 1 && n <= MORSE_MAX, "ma '%s' dai %d", c, n);
    for (int k = 0; k < n; k++) CHECK(c[k] == '.' || c[k] == '-', "ma '%s' co ky tu la", c);
    for (int j = i + 1; j < MORSE_N; j++) { CHECK(strcmp(c, MORSE_TAB[j].code), "trung ma '%s'", c); CHECK(MORSE_TAB[i].ch != MORSE_TAB[j].ch, "trung ky tu '%c'", MORSE_TAB[i].ch); }
  }
  for (char c = 'a'; c <= 'z'; c++) CHECK(CODE_OF(c), "thieu chu %c", c);
  for (char c = '0'; c <= '9'; c++) CHECK(CODE_OF(c), "thieu so %c", c);
  CHECK(MORSE_N == 26 + 10 + 18, "so muc bang = %d (26 chu + 10 so + 18 dau)", MORSE_N);
  // vai ma kinh dien
  CHECK(!strcmp(CODE_OF('s'), "..."), "S"); CHECK(!strcmp(CODE_OF('o'), "---"), "O"); CHECK(!strcmp(CODE_OF('q'), "--.-"), "Q");
  CHECK(!strcmp(CODE_OF('0'), "-----"), "0"); CHECK(!strcmp(CODE_OF('?'), "..--.."), "?"); CHECK(!strcmp(CODE_OF('@'), ".--.-."), "@");
}

static void test_user_example(void) {   // dung ky nang yeu cau: 1 cham -> e ; them cham/gach -> xoa ky tu truoc, hien ky tu moi
  morse_t m; morse_commit(&m); host_clear();
  morse_press(&m, 0, &IO); CHECK(!strcmp(host, "e"), "1 cham -> '%s'", host);
  morse_press(&m, 0, &IO); CHECK(!strcmp(host, "i"), "2 cham -> '%s'", host);
  morse_press(&m, 0, &IO); CHECK(!strcmp(host, "s"), "3 cham -> '%s'", host);
  morse_press(&m, 1, &IO); CHECK(!strcmp(host, "v"), "...- -> '%s'", host);
  morse_commit(&m); morse_press(&m, 0, &IO); CHECK(!strcmp(host, "ve"), "xong roi go cham -> '%s' (ky tu moi, giu 'v')", host);
  morse_press(&m, 1, &IO); CHECK(!strcmp(host, "va"), ".- -> '%s'", host);
  morse_commit(&m); morse_press(&m, 1, &IO); CHECK(!strcmp(host, "vat"), "'%s'", host);
}

static void test_invalid_then_valid(void) {   // ma tam thoi khong hop le: chi xoa, van go tiep duoc
  morse_t m; morse_commit(&m); host_clear();
  type_code(&m, "..-"); CHECK(!strcmp(host, "u"), "'%s'", host);
  morse_press(&m, 1, &IO); CHECK(!strcmp(host, ""), "..-- khong hop le phai xoa het, ra '%s'", host); CHECK(m.shown == 0 && m.len == 4, "trang thai");
  morse_press(&m, 0, &IO); CHECK(!strcmp(host, ""), "..--. van khong hop le: '%s'", host);
  morse_press(&m, 0, &IO); CHECK(!strcmp(host, "?"), "..--.. phai ra ?, ra '%s'", host);
  morse_press(&m, 0, &IO); CHECK(!strcmp(host, ""), "..--... khong hop le: '%s'", host);
  morse_commit(&m); CHECK(!strcmp(host, ""), "xong luc khong co ky tu: khong gui gi");
  morse_commit(&m); morse_press(&m, 0, &IO); CHECK(!strcmp(host, "e"), "'%s'", host);
}

static void test_full_length(void) {   // ma dai toi da 7, phan tu thu 8 bi bo qua, khong lam hong trang thai
  morse_t m; morse_commit(&m); host_clear();
  type_code(&m, "...-..-"); CHECK(!strcmp(host, "$"), "$ -> '%s'", host); CHECK(m.len == 7, "len");
  uint8_t r = morse_press(&m, 0, &IO); CHECK(r == 0, "phan tu thu 8 phai bi bo qua"); CHECK(!strcmp(host, "$") && m.len == 7 && m.shown, "trang thai khong doi");
  morse_commit(&m); morse_press(&m, 1, &IO); CHECK(!strcmp(host, "$t"), "'%s'", host);
}

static void test_every_char_and_prefixes(void) {   // moi ky tu: tung buoc dung bang tra, ket thuc dung 1 ky tu, khong du ky tu thua
  for (int i = 0; i < MORSE_N; i++) {
    morse_t m; morse_commit(&m); host_clear(); const char *c = MORSE_TAB[i].code; char pre[16];
    for (int k = 0; k < (int)strlen(c); k++) {
      morse_press(&m, c[k] == '-', &IO);
      memcpy(pre, c, (size_t)k + 1); pre[k + 1] = 0; char e = morse_find(pre);
      CHECK((e ? (hn == 1 && host[0] == e) : hn == 0), "ma '%s' buoc %d: host='%s' mong '%c'", c, k + 1, host, e ? e : '-');
    }
    CHECK(hn == 1 && host[0] == MORSE_TAB[i].ch, "ma '%s' ket qua '%s'", c, host);
  }
}

static void test_sentence(void) {
  morse_t m; morse_commit(&m); host_clear(); const char *txt = "hello, world 2026! sos?";
  for (const char *p = txt; *p; p++) {
    if (*p == ' ') { morse_commit(&m); host_ch(' '); continue; }   // Space la phim khac: chot ky tu roi go dau cach
    type_code(&m, CODE_OF(*p)); morse_commit(&m);
  }
  CHECK(!strcmp(host, txt), "'%s' != '%s'", host, txt);
}

static uint8_t r0(void) { return 0; }
static uint8_t r1(void) { return 1; }
static uint8_t r2(void) { return 2; }
static void test_room(void) {   // hang doi phim day -> bo qua ca lan bam, trang thai khong doi
  morse_t m; morse_commit(&m); host_clear(); morse_io io = { io_bs, io_put, r0 };
  CHECK(morse_press(&m, 0, &io) == 0 && hn == 0 && m.len == 0, "het cho: phai bo qua");
  io.room = r1; CHECK(morse_press(&m, 0, &io) == 1 && !strcmp(host, "e"), "1 cho: ky tu dau (chi 1 phim) phai nhan");
  CHECK(morse_press(&m, 0, &io) == 0 && !strcmp(host, "e") && m.len == 1 && m.shown, "1 cho: thay ky tu can 2 (BS + chu) phai bo qua");
  io.room = r2; CHECK(morse_press(&m, 0, &io) == 1 && !strcmp(host, "i"), "2 cho: thay ky tu");
  io.room = r1; CHECK(morse_press(&m, 1, &io) == 0, "'..-' can BS+u = 2");
  morse_commit(&m); host_clear(); io.room = r0;   // ma khong hop le sau khi chua hien gi: can 0 cho -> van nhan
  m.len = 4; strcpy(m.code, "..--"); m.shown = 0;
  CHECK(morse_press(&m, 0, &io) == 1 && m.len == 5 && hn == 0, "khong can cho thi van nhan");
}

static uint32_t rs = 0xC0FFEEu; static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

static void test_fuzz_vs_reference(void) {   // chuoi su kien ngau nhien: moi buoc may tinh phai bang mo hinh tham chieu
  for (int run = 0; run < 400; run++) {
    morse_t m; morse_commit(&m); host_clear(); ref_t r; memset(&r, 0, sizeof r);
    for (int step = 0; step < 300; step++) {
      uint32_t e = rnd() % 10;
      if (e < 4) { morse_press(&m, 0, &IO); ref_press(&r, 0); }
      else if (e < 7) { morse_press(&m, 1, &IO); ref_press(&r, 1); }
      else if (e < 9) { morse_commit(&m); { char c = r.len ? morse_find(r.code) : 0; if (c) r.done[r.dn++] = c; r.done[r.dn] = 0; } ref_commit(&r); }
      else { morse_commit(&m); { char c = r.len ? morse_find(r.code) : 0; if (c) r.done[r.dn++] = c; } r.done[r.dn++] = ' '; r.done[r.dn] = 0; ref_commit(&r); host_ch(' '); }
      char exp[1100]; ref_expect(&r, exp);
      if (strcmp(exp, host)) { CHECK(0, "run %d step %d: host='%s' mong '%s'", run, step, host, exp); return; }
    }
  }
}

static void test_queue_never_desyncs(void) {   // hang doi 16 cho + may tinh lay phim ngau nhien (cham): bam nhanh khong bao gio lam lech
  for (int run = 0; run < 400; run++) {
    morse_t m; morse_commit(&m); host_clear(); qh = qt = (uint8_t)(rnd() & 255); ref_t r; memset(&r, 0, sizeof r);
    for (int step = 0; step < 400; step++) {
      uint32_t e = rnd() % 10;
      if (e < 7) { uint8_t dash = rnd() & 1; if (morse_press(&m, dash, &QIO)) ref_press(&r, dash); }
      else if (e < 9) { morse_commit(&m); { char c = r.len ? morse_find(r.code) : 0; if (c) r.done[r.dn++] = c; r.done[r.dn] = 0; } ref_commit(&r); }
      else { /* may tinh chua kip lay het: khong lam gi them */ }
      int drain = (int)(rnd() % 3);   // 0..2 phim duoc lay ra moi buoc -> hang doi co luc day
      while (drain--) q_drain_one();
      CHECK((uint8_t)(qh - qt) <= 16, "hang doi tran");
    }
    while (qh != qt) q_drain_one();
    char exp[1100]; ref_expect(&r, exp);
    CHECK(!strcmp(exp, host), "run %d sau khi lay het hang doi: host='%s' mong '%s'", run, host, exp);
  }
}

static void test_hid_mapping(void) {   // moi ky tu cua bang -> ma HID THAT trong firmware -> ban phim US -> dung ky tu do (kem Shift khi can)
  for (int i = 0; i < MORSE_N; i++) {
    char c = MORSE_TAB[i].ch; uint16_t k = mc_ascii((uint8_t)c);
    CHECK(k != 0, "mc_ascii('%c') = 0", c); CHECK(hid_char(k) == c, "'%c' -> 0x%X -> '%c'", c, k, hid_char(k));
    CHECK(!(k & 0x8000) && (k & 0xF000) != 0x4000, "'%c' ma khong phai phim dac biet", c);
    CHECK(k != 0x2A, "'%c' khong duoc trung Backspace", c);
  }
}

static int stream_mode(void) {
  morse_t m; morse_commit(&m); host_clear(); int c;
  while ((c = getchar()) != EOF) {
    if (c == 'd') morse_press(&m, 0, &IO); else if (c == 'h') morse_press(&m, 1, &IO); else if (c == 'o') morse_commit(&m);
    else if (c == 's') { morse_commit(&m); host_ch(' '); } else if (c == 'b') { morse_commit(&m); host_bs(); } else continue;
    printf("%d|%s\n", hn, hn > 40 ? host + hn - 40 : host);   // do dai + 40 ky tu cuoi
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--stream")) return stream_mode();
  test_table(); test_user_example(); test_invalid_then_valid(); test_full_length(); test_every_char_and_prefixes(); test_sentence(); test_room();
  test_fuzz_vs_reference(); test_queue_never_desyncs(); test_hid_mapping();
  printf("%d kiem tra, %d loi\n", checks, fails);
  return fails ? 1 : 0;
}
