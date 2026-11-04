// morse.h - logic go Morse 3 phim (cham / gach / xong), THUAN C, khong phu thuoc Arduino -> kiem thu duoc bang gcc (xem test/).
//
// Cach hoat dong (hien ky tu theo thoi gian thuc tren may tinh):
//   - Moi lan bam CHAM hoac GACH, them 1 phan tu vao ma dang go roi tra bang.
//   - Neu may tinh dang hien 1 ky tu tam (shown) thi gui Backspace de xoa no truoc.
//   - Neu ma dang go co ky tu tuong ung thi gui ky tu do (vd "." -> e, ".." -> i, ".-" -> a).
//     Neu khong co (vd "..--") thi chi xoa, may tinh khong hien gi, nhung ma van duoc giu de go tiep (vd "..--.." -> ?).
//   - GOI Y (tuy chon, bat/tat): sau ky tu go them " [.i -a]" (them cham ra i, them gach ra a); xoa cung luc voi ky tu tam o lan bam ke tiep.
//   - XONG: chot ky tu hien tai (chi xoa phan goi y neu co), lan go ke tiep bat dau ky tu moi.
//   - Phim khac (Space, Enter, Backspace, ...) cung chot ky tu dang go (xem morse_commit trong numpad21.ino).
//
// Luu y: file nay duoc include truoc cac #define cua numpad21.ino (L, NK, NW, MH, P, CB, TD, MO...) -> khong dung cac ten do.
#pragma once
#include <stdint.h>
#include <string.h>

#define MORSE_MAX 7   // do dai ma toi da (ky tu '$' dai 7 phan tu)
#define MORSE_HINT_MAX 8   // do dai toi da cua goi y: " [.i -a]"

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
  uint8_t hint;               // so ky tu goi y dang hien tren may tinh ngay sau ky tu tam (vd " [.i -a]" = 8), 0 = khong co
} morse_t;
static uint8_t morse_hint_on = 0;   // 1 = bat goi y tren van ban (firmware dat gia tri mac dinh luc khoi dong; doi bang morse_set_hint)

// Cac thao tac xuat ra may tinh. room() = so cho trong con lai cua hang doi phim (de khong bao gio gui thieu nua chung).
typedef struct { void (*bs)(void); void (*put)(char ch); uint8_t (*room)(void); } morse_io;

static char morse_find(const char *code) {   // tra bang, 0 = khong co ky tu
  for (int i = 0; i < MORSE_N; i++) if (!strcmp(MORSE_TAB[i].code, code)) return MORSE_TAB[i].ch;
  return 0;
}
// Goi y cho ma "code": " [.i -a]" = them cham ra 'i', them gach ra 'a' (chi in nhanh co ky tu). Tra ve do dai (0 neu khong co nhanh nao).
static uint8_t morse_hint_str(const char *code, char *o) {
  char nc[MORSE_MAX + 2]; uint8_t n = 0, cnt = 0; size_t l = strlen(code);
  o[0] = 0; if (l >= MORSE_MAX) return 0;
  memcpy(nc, code, l);
  for (int d = 0; d < 2; d++) {
    nc[l] = d ? '-' : '.'; nc[l + 1] = 0;
    char ch = morse_find(nc); if (!ch) continue;
    if (!cnt) { o[n++] = ' '; o[n++] = '['; } else o[n++] = ' ';
    o[n++] = nc[l]; o[n++] = ch; cnt++;
  }
  if (cnt) o[n++] = ']';
  o[n] = 0; return n;
}
static void morse_commit(morse_t *m) { m->len = 0; m->code[0] = 0; m->shown = 0; m->hint = 0; }   // chot ky tu (CHI dat lai trang thai, khong gui phim), bat dau ky tu moi

// Bam 1 phan tu (dash = 0 cham, 1 gach). Tra ve 1 neu da nhan, 0 neu bo qua (ma da day, hoac hang doi phim het cho).
// Khi bo qua thi trang thai KHONG doi -> may tinh va firmware khong bao gio lech nhau.
// Khi goi y bat: xoa (ky tu tam + goi y cu), gui ky tu moi, gui goi y moi. Chua san them cho de lat nua xoa goi y luc chot (morse_commit_io) luon du cho.
static uint8_t morse_press(morse_t *m, uint8_t dash, const morse_io *io) {
  if (m->len >= MORSE_MAX) return 0;
  char nc[MORSE_MAX + 2], h[MORSE_HINT_MAX + 4];
  memcpy(nc, m->code, m->len); nc[m->len] = dash ? '-' : '.'; nc[m->len + 1] = 0;
  char ch = morse_find(nc);
  uint8_t hl = morse_hint_on ? morse_hint_str(nc, h) : 0, del = (uint8_t)(m->shown + m->hint);
  uint8_t need = (uint8_t)(del + (ch ? 1 : 0) + hl + hl);
  if (io->room() < need) return 0;
  for (uint8_t i = 0; i < del; i++) io->bs();
  if (ch) io->put(ch);
  for (uint8_t i = 0; i < hl; i++) io->put(h[i]);
  memcpy(m->code, nc, (size_t)m->len + 2); m->len++; m->shown = ch ? 1 : 0; m->hint = hl;
  return 1;
}
// Chot ky tu: xoa phan goi y khoi van ban (ky tu tam o lai), roi dat lai trang thai.
static void morse_commit_io(morse_t *m, const morse_io *io) {
  for (uint8_t i = 0; i < m->hint; i++) io->bs();
  morse_commit(m);
}
// Bat / tat goi y. Dang go do thi hien (hoac xoa) goi y ngay. Tra ve 1 neu da doi, 0 neu hang doi phim het cho (khi do khong doi gi).
static uint8_t morse_set_hint(morse_t *m, uint8_t on, const morse_io *io) {
  on = on ? 1 : 0; if (on == morse_hint_on) return 1;
  if (!on) {
    if (io->room() < m->hint) return 0;
    for (uint8_t i = 0; i < m->hint; i++) io->bs();
    m->hint = 0; morse_hint_on = 0; return 1;
  }
  char h[MORSE_HINT_MAX + 4]; uint8_t hl = m->len ? morse_hint_str(m->code, h) : 0;
  if (io->room() < hl + hl) return 0;
  morse_hint_on = 1;
  for (uint8_t i = 0; i < hl; i++) io->put(h[i]);
  m->hint = hl; return 1;
}
