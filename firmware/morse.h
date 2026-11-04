// morse.h - logic go Morse 3 phim (cham / gach / xong), THUAN C, khong phu thuoc Arduino -> kiem thu duoc bang gcc (xem test/).
//
// Cach hoat dong (hien ky tu theo thoi gian thuc tren may tinh):
//   - Moi lan bam CHAM hoac GACH, them 1 phan tu vao ma dang go roi tra bang.
//   - Neu may tinh dang hien 1 ky tu tam (shown) thi gui Backspace de xoa no truoc.
//   - Neu ma dang go co ky tu tuong ung thi gui ky tu do (vd "." -> e, ".." -> i, ".-" -> a).
//     Neu khong co (vd "..--") thi chi xoa, may tinh khong hien gi, nhung ma van duoc giu de go tiep (vd "..--.." -> ?).
//   - XONG: chot ky tu hien tai (khong gui gi), lan go ke tiep bat dau ky tu moi.
//   - Phim khac (Space, Enter, Backspace, ...) cung chot ky tu dang go (xem morse_commit trong numpad21.ino).
//
// Luu y: file nay duoc include truoc cac #define cua numpad21.ino (L, NK, NW, MH, P, CB, TD, MO...) -> khong dung cac ten do.
#pragma once
#include <stdint.h>
#include <string.h>

#define MORSE_MAX 7   // do dai ma toi da (ky tu '$' dai 7 phan tu)

typedef struct { const char *code; char ch; } morse_ent;
// Bang ma ITU: chu, so, dau cau. Trang web (numpad21.html, MORSE_TAB) phai giong het bang nay - test/run_tests.sh kiem tra.
static const morse_ent MORSE_TAB[] = {
  {".-", 'a'}, {"-...", 'b'}, {"-.-.", 'c'}, {"-..", 'd'}, {".", 'e'}, {"..-.", 'f'}, {"--.", 'g'}, {"....", 'h'}, {"..", 'i'},
  {".---", 'j'}, {"-.-", 'k'}, {".-..", 'l'}, {"--", 'm'}, {"-.", 'n'}, {"---", 'o'}, {".--.", 'p'}, {"--.-", 'q'}, {".-.", 'r'},
  {"...", 's'}, {"-", 't'}, {"..-", 'u'}, {"...-", 'v'}, {".--", 'w'}, {"-..-", 'x'}, {"-.--", 'y'}, {"--..", 'z'},
  {"-----", '0'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'}, {"....-", '4'}, {".....", '5'},
  {"-....", '6'}, {"--...", '7'}, {"---..", '8'}, {"----.", '9'},
  {".-.-.-", '.'}, {"--..--", ','}, {"..--..", '?'}, {".----.", '\''}, {"-.-.--", '!'}, {"-..-.", '/'}, {"-.--.", '('}, {"-.--.-", ')'},
  {".-...", '&'}, {"---...", ':'}, {"-.-.-.", ';'}, {"-...-", '='}, {".-.-.", '+'}, {"-....-", '-'}, {"..--.-", '_'}, {".-..-.", '"'},
  {"...-..-", '$'}, {".--.-.", '@'},
};
#define MORSE_N ((int)(sizeof MORSE_TAB / sizeof MORSE_TAB[0]))

typedef struct {
  char code[MORSE_MAX + 1];   // ma dang go, vd "..-"
  uint8_t len;                // so phan tu
  uint8_t shown;              // 1 = may tinh dang hien 1 ky tu tam cua ma nay (se bi xoa o lan bam ke tiep)
} morse_t;

// Cac thao tac xuat ra may tinh. room() = so cho trong con lai cua hang doi phim (de khong bao gio gui thieu nua chung).
typedef struct { void (*bs)(void); void (*put)(char ch); uint8_t (*room)(void); } morse_io;

static char morse_find(const char *code) {   // tra bang, 0 = khong co ky tu
  for (int i = 0; i < MORSE_N; i++) if (!strcmp(MORSE_TAB[i].code, code)) return MORSE_TAB[i].ch;
  return 0;
}
static void morse_commit(morse_t *m) { m->len = 0; m->code[0] = 0; m->shown = 0; }   // chot ky tu, bat dau ky tu moi

// Bam 1 phan tu (dash = 0 cham, 1 gach). Tra ve 1 neu da nhan, 0 neu bo qua (ma da day, hoac hang doi phim het cho).
// Khi bo qua thi trang thai KHONG doi -> may tinh va firmware khong bao gio lech nhau.
static uint8_t morse_press(morse_t *m, uint8_t dash, const morse_io *io) {
  if (m->len >= MORSE_MAX) return 0;
  char nc[MORSE_MAX + 2];
  memcpy(nc, m->code, m->len); nc[m->len] = dash ? '-' : '.'; nc[m->len + 1] = 0;
  char ch = morse_find(nc);
  uint8_t need = (uint8_t)((m->shown ? 1 : 0) + (ch ? 1 : 0));
  if (io->room() < need) return 0;
  if (m->shown) io->bs();
  if (ch) io->put(ch);
  memcpy(m->code, nc, (size_t)m->len + 2); m->len++; m->shown = ch ? 1 : 0;
  return 1;
}
