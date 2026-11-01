// Macropad15 RP2350-Zero: ma tran 4x5 (hang GP22-25, cot GP17-21), 3 encoder, HID + raw HID, OLED SSD1306 (I2C0 SDA=GP12 SCL=GP13),
// Nokia multitap, tap dance, combo, latch, LED RGB onboard (GP16) theo layer.
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "ws2812.pio.h"
#include "tusb.h"
#include "font.h"

#define L 32              // so layer toi da (so layer dang dung nam trong cfg, them/xoa bang app)
#define NK 20
#define BASE_TD (L * NK + L * 6)
#define BASE_CB (BASE_TD + 12)
#define NLI (BASE_CB + 12)   // o luu so layer dang dung
#define LDB (NLI + 1)        // khoi cau hinh LED (app chinh): NLD o
#define NLD 8
#define NW (LDB + NLD)
#define LD(i) cfg.w[LDB + (i)]
enum { LD_MODE, LD_SPEED, LD_BRIGHT, LD_HUE, LD_SAT, LD_OVER, LD_OBMODE, LD_OBBRI };   // che do day den, toc do, do sang, mau (0-359), bao hoa, nhay khi bam, che do den onboard, do sang nen onboard
#define NFX 25               // so che do day den (0-15 goc, 16-24 moi)
#define NOB 5                // so che do den onboard
#define MAGIC 0x4D503137u
#define KC_TRNS 1
#define CONS(x) (0x8000 | (x))
#define MO(n) (0x4000 | (n))
#define TL(n) (0x4100 | (n))   // toggle layer n (TL(0) = tat het layer toggle)
#define LYN 0x4200               // layer ke tiep (vong), LYN+1 = layer truoc
#define TD(n) (0x5000 | (n))
#define NKK(i) (0x6000 | (i))
#define KC_LATCH 0x7001      // 0x7002..0x7005 = toggle Space/Shift/Ctrl/Alt
#define KC_LEDFX 0x7010      // 0x7010 che do LED +, 0x7011 che do -, 0x7012/13 sang +/-, 0x7014/15 toc do +/-, 0x7016 doi mau (chi doi tam, bam "Luu" trong app de giu)
#define KC_LEDFX_LAST 0x7016
#define KC_BOOT 0x7F00
#define TAP_TERM 200
#define COMBO_TERM 30
#define NK_TERM 600
#define LED_PIN 16        // WS2812 onboard cua RP2350-Zero
#define EXT_PIN 4         // led WS2812 ngoai (7 mau), dong bo voi led tren board
#define STRIP_PIN 11      // day den WS2812 ngoai (DIN) - doi chan neu can
#define STRIP_N 15        // so den tren day
#define STRIP_SNAKE 1     // 1 = di day chu S (hang le di nguoc), 0 = moi hang di tu trai sang phai
#define STRIP_BASE 56     // do sang nen cua day den (0-255), giu thap de khong qua dong USB
#define LED_BASE 24       // do sang nen (0-255)
#define CFG_BUF 4096         // 1 sector flash: cfg (< NAME_OFF) + ten layer (tu NAME_OFF)
#define NAME_OFF 2048
#define NM 10                // so ky tu toi da cua ten layer
#define NAME_MAGIC 0x4C4E414Du
#define NMAC 16              // so macro
#define MSLOT 1020           // byte moi o macro: [0]=lap [1,2]=nghi ms [3]=do dai ten [4..15]=ten [16,17]=do dai buoc [18..]=buoc
#define MAC_SZ 16384         // 4 sector flash ngay duoi sector cau hinh: magic 4 byte + NMAC * MSLOT
#define MAC_OFF (FLASH_OFF - MAC_SZ)
#define MAC_MAGIC 0x3143414Du
#define MH 8                 // so phim macro co the giu cung luc
#define FW_VER 4             // giao thuc: 4 = co macro + 25 hieu ung
#define KC_RENAME 0x7020     // doi ten layer dang hien (go bang phim Nokia)
_Static_assert(sizeof(uint32_t) + 2 * NW <= NAME_OFF, "cfg qua lon");
#define FLASH_OFF (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

static const uint8_t rows[4] = {22, 23, 24, 25}, cols[5] = {17, 18, 19, 20, 21};   // ma tran o pad han mat duoi board
static const uint8_t encp[3][2] = {{9, 10}, {7, 8}, {6, 5}};   // E1=GP9/10, E2=GP7/8, E3=GP6/5 (da dao chieu quay)

static struct { uint32_t magic; uint16_t w[NW]; } cfg;
static struct { uint32_t magic; char n[L][NM + 1]; } lname;   // ten layer do nguoi dung dat (rong = ten mac dinh), luu flash tai NAME_OFF
#define KM(l, p) cfg.w[(l) * NK + (p)]
#define EN(l, e, d) cfg.w[L * NK + (l) * 6 + (e) * 2 + (d)]
#define TDW(n, k) cfg.w[BASE_TD + (n) * 3 + (k)]   // tap, hold, 2x tap
#define CB(n, k) cfg.w[BASE_CB + (n) * 3 + (k)]    // keycode A, keycode B, ket qua (A rong = tat)

static uint32_t ms(void) { return time_us_32() / 1000; }
static char *putnum(char *o, int v) { if (v >= 100) *o++ = '0' + v / 100 % 10; if (v >= 10) *o++ = '0' + v / 10 % 10; *o++ = '0' + v % 10; *o = 0; return o; }
static void lname_get(int l, char *o) {   // o >= NM + 1 byte: ten layer l (ten tuy chinh hoac mac dinh BASE / LAYERn / NOKIA)
  if (l >= 0 && l < L && lname.n[l][0]) { strcpy(o, lname.n[l]); return; }
  if (l == 0) strcpy(o, "BASE"); else if (l == 2) strcpy(o, "NOKIA"); else { strcpy(o, "LAYER"); putnum(o + 5, l); }
}

static void led_defaults(void) { LD(LD_MODE) = 0; LD(LD_SPEED) = 128; LD(LD_BRIGHT) = 200; LD(LD_HUE) = 210; LD(LD_SAT) = 255; LD(LD_OVER) = 1; LD(LD_OBMODE) = 0; LD(LD_OBBRI) = LED_BASE; }
static void defaults(void) {
  static const uint8_t pos[15] = {0, 1, 2, 3, 5, 6, 7, 8, 10, 11, 12, 13, 15, 16, 17};
  memset(&cfg, 0, sizeof cfg); cfg.magic = MAGIC; cfg.w[NLI] = 4; led_defaults();
  for (int l = 2; l < L; l++) for (int i = 0; i < NK; i++) KM(l, i) = KC_TRNS;
  for (int l = 2; l < L; l++) for (int e = 0; e < 3; e++) for (int d = 0; d < 2; d++) EN(l, e, d) = KC_TRNS;
  for (int i = 0; i < 15; i++) {
    KM(0, pos[i]) = 0x04 + i;
    KM(1, pos[i]) = i < 10 ? 0x1E + i : 0x28 + (i - 10);
  }
  KM(0, 4) = CONS(0xE2); KM(0, 9) = CONS(0xCD); KM(0, 14) = MO(1);
  KM(1, 4) = KC_BOOT; KM(1, 9) = MO(2); KM(1, 17) = TD(0);
  static const uint16_t nk[15] = {NKK(0), NKK(1), NKK(2), 0x2A, NKK(3), NKK(4), NKK(5), 0x28, NKK(6), NKK(7), NKK(8), 0x2C, NKK(10), NKK(9), KC_LATCH};
  for (int i = 0; i < 15; i++) KM(2, pos[i]) = nk[i];
  KM(2, 4) = KC_RENAME;   // layer Nokia: nut E1 = doi ten layer
  EN(0, 0, 0) = CONS(0xEA); EN(0, 0, 1) = CONS(0xE9);
  EN(0, 1, 0) = CONS(0xB6); EN(0, 1, 1) = CONS(0xB5);
  EN(0, 2, 0) = 0x50; EN(0, 2, 1) = 0x4F;
  EN(1, 0, 0) = 0x4E; EN(1, 0, 1) = 0x4B;
  TDW(0, 0) = 0x2C; TDW(0, 1) = 0xE1; TDW(0, 2) = 0x28;
}
static void load(void) {
  memcpy(&lname, (const void *)(XIP_BASE + FLASH_OFF + NAME_OFF), sizeof lname);
  if (lname.magic != NAME_MAGIC) memset(&lname, 0, sizeof lname);
  lname.magic = NAME_MAGIC;
  for (int l = 0; l < L; l++) { lname.n[l][NM] = 0; for (int i = 0; lname.n[l][i]; i++) if ((uint8_t)lname.n[l][i] < 32 || (uint8_t)lname.n[l][i] > 126) { lname.n[l][0] = 0; break; } }
  memcpy(&cfg, (const void *)(XIP_BASE + FLASH_OFF), sizeof cfg);
  if (cfg.magic != MAGIC) defaults();
  else if (LD(LD_MODE) >= NFX || LD(LD_BRIGHT) > 255 || LD(LD_OBMODE) >= NOB) led_defaults();   // cau hinh cu (chua co khoi LED, vung nay = 0xFFFF): giu phim, chi dat LED ve mac dinh
}
static uint8_t sbuf[CFG_BUF];
static void do_save(void *p) {   // p == NULL: luu cfg + ten (app); p != NULL: chi luu ten (giu nguyen cfg dang nam trong flash)
  memset(sbuf, 0xFF, sizeof sbuf);
  if (p) memcpy(sbuf, (const void *)(XIP_BASE + FLASH_OFF), sizeof cfg); else memcpy(sbuf, &cfg, sizeof cfg);
  memcpy(sbuf + NAME_OFF, &lname, sizeof lname);
  flash_range_erase(FLASH_OFF, FLASH_SECTOR_SIZE); flash_range_program(FLASH_OFF, sbuf, CFG_BUF);
}

static uint32_t layers, tl;
static int nl(void) { uint16_t v = cfg.w[NLI]; return v < 1 ? 1 : v > L ? L : v; }
static uint16_t pressed[NK], tap, q[16], lat; static uint8_t qh, qt, tap_phase, tg;
static bool dirty, latch_wait, odirty;
static bool rn_active, nsave_req;                 // dang doi ten layer / can luu ten vao flash
static uint32_t pseq[NK + 5], sq, ot;             // thu tu bam (phim dang de: pressed[], lat, 4 toggle), thoi diem bam/nha gan nhat
static void oled_forget(void);
static void rename_start(void);
static void qpush(uint16_t k) { if ((uint8_t)(qh - qt) < 16) q[qh++ & 15] = k; }

static uint16_t res_key(int p) { for (int l = nl() - 1; l >= 0; l--) if (l == 0 || (layers >> l & 1)) { uint16_t k = KM(l, p); if (k != KC_TRNS) return k; } return 0; }
static uint16_t res_enc(int e, int d) { for (int l = nl() - 1; l >= 0; l--) if (l == 0 || (layers >> l & 1)) { uint16_t k = EN(l, e, d); if (k != KC_TRNS) return k; } return 0; }
static void relayer(void) {
  int n = nl(); uint32_t mask = n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1; tl &= mask; layers = tl;
  for (int i = 0; i < NK; i++) if ((pressed[i] & 0xFF00) == 0x4000) layers |= 1u << (pressed[i] & 31);
  layers &= mask; odirty = true;
}


// ---- nut BOOT tren board (BOOTSEL, doc qua chan QSPI CS): bam 1 lan = chuyen toggle sang layer ke tiep (vong tron) ----
static bool __no_inline_not_in_flash_func(bootsel_pressed)(void) {
  uint32_t f = save_and_disable_interrupts();
  hw_write_masked(&ioqspi_hw->io[1].ctrl, GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB, IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
  for (volatile int i = 0; i < 1000; i++) ;
  bool b = !(sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS);
  hw_write_masked(&ioqspi_hw->io[1].ctrl, GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB, IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
  restore_interrupts(f);
  return b;
}
static void led_flash(void);
static void cycle_layer(int dir);
static void boot_task(uint32_t now) {
  static uint32_t t; static bool st, raw; static uint8_t cnt;
  if (now - t < 5) return; t = now;
  bool b = bootsel_pressed();
  if (b != raw) { raw = b; cnt = 0; } else if (cnt < 4 && ++cnt == 4 && b != st) {   // chong doi ~20ms
    st = b;
    if (b) {
      cycle_layer(1);
    }
  }
}

// ---- LED RGB onboard (WS2812, GP16) + day den ngoai: che do/toc do/do sang/mau chinh tu app (khoi LD_* trong cfg) ----
static const uint8_t LCOL[4][3] = {{0, 80, 255}, {0, 255, 60}, {255, 90, 0}, {200, 0, 255}};  // L0 xanh duong, L1 xanh la, L2 cam, L3 tim
static uint8_t flash_lv; static uint32_t led_t, led_last = 0xFFFFFFFF;
static void led_col(int a, uint8_t *o) {
  if (a < 4) { memcpy(o, LCOL[a], 3); return; }
  int h = (a * 137) % 360, f = h % 60 * 255 / 60, s = h / 60; uint8_t v[6][3] = {{255, f, 0}, {255 - f, 255, 0}, {0, 255, f}, {0, 255 - f, 255}, {f, 0, 255}, {255, 0, 255 - f}};
  for (int i = 0; i < 3; i++) o[i] = v[s][i];
}
static void led_flash(void) { flash_lv = 255; }
static void led_put(uint8_t r, uint8_t g, uint8_t b) { uint32_t v = ((uint32_t)g << 16 | (uint32_t)r << 8 | b) << 8; pio_sm_put_blocking(pio0, 0, v); pio_sm_put_blocking(pio0, 2, v); }   // board + led ngoai GP4 cung mau
static void ws_sm_init(uint off, int sm, int pin) {
  pio_sm_config c = ws2812_program_get_default_config(off);
  sm_config_set_sideset_pins(&c, pin); sm_config_set_out_shift(&c, false, true, 24); sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  sm_config_set_clkdiv(&c, clock_get_hz(clk_sys) / (800000.0f * (ws2812_T1 + ws2812_T2 + ws2812_T3)));
  pio_gpio_init(pio0, pin); pio_sm_set_consecutive_pindirs(pio0, sm, pin, 1, true);
  pio_sm_init(pio0, sm, off, &c); pio_sm_set_enabled(pio0, sm, true);
}
static void led_init(void) {
  uint off = pio_add_program(pio0, &ws2812_program);
  ws_sm_init(off, 0, LED_PIN);       // den onboard
  ws_sm_init(off, 1, STRIP_PIN);     // day den ngoai
  ws_sm_init(off, 2, EXT_PIN);       // led 7 mau ngoai (GP4), copy den board
}

// -- tien ich: HSV, song tam giac, so ngau nhien, doc tham so (da kep) --
static uint32_t ph, ph_t;                       // pha hoat anh (cong don theo toc do): (ph >> 12) & 255 = 1 vong
static uint32_t rnd = 2463534242u;
static uint32_t rnd32(void) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return rnd; }
static uint32_t ldv(int i, uint32_t mx) { uint32_t v = LD(i); return v > mx ? mx : v; }
static uint8_t tri8(uint32_t x) { x &= 255; return x < 128 ? x * 2 : (255 - x) * 2; }
static void hsv(uint8_t h, uint8_t s, uint8_t v, uint8_t *o) {
  int r = h / 43, f = (h - r * 43) * 6; if (f > 255) f = 255;
  uint32_t c[3]; const uint32_t g = 255 - f;
  switch (r) {
    case 0: c[0] = 255; c[1] = f; c[2] = 0; break;
    case 1: c[0] = g; c[1] = 255; c[2] = 0; break;
    case 2: c[0] = 0; c[1] = 255; c[2] = f; break;
    case 3: c[0] = 0; c[1] = g; c[2] = 255; break;
    case 4: c[0] = f; c[1] = 0; c[2] = 255; break;
    default: c[0] = 255; c[1] = 0; c[2] = g; break;
  }
  for (int i = 0; i < 3; i++) { uint32_t x = c[i] + (255 - c[i]) * (255 - s) / 255; o[i] = x * v / 255; }
}
static void heat_rgb(uint8_t t, uint8_t *o) {
  uint32_t r = t * 3u; o[0] = r > 255 ? 255 : r;
  r = t > 85 ? (t - 85) * 3u : 0; o[1] = r > 255 ? 255 : r;
  r = t > 170 ? (t - 170) * 3u : 0; o[2] = (r > 255 ? 255 : r) / 3;   // it xanh de lua giu mau cam/vang
}
static void led_flash(void);

// ---- day den ngoai STRIP_N led (WS2812, STRIP_PIN): moi phim 1 led; NFX che do ----
//  0 Layer (mau theo layer, nhay khi bam)  1 Bay 7 mau  2 Tat  3 Mau tinh  4 Tho  5 Song cau vong  6 Doi mau
//  7 Duoi theo day  8 Gon song khi bam  9 Lua  10 Lap lanh  11 Mua roi  12 Cuc quang  13 Sang theo phim  14 Quet cot  15 Phim doi mau
//  16 Cot sang  17 Hang ngang  18 Chu thap  19 Set  20 Ban do nhiet  21 Sao choi  22 Dao mau  23 No tia  24 Phao hoa
static uint8_t sfl[STRIP_N], sx[STRIP_N], sy[STRIP_N], tw[STRIP_N], twh[STRIP_N], keyh[STRIP_N], heat[STRIP_N];
static int8_t klut[NK], gidx[4][4]; static uint32_t st_t, st_last[STRIP_N], fire_t;
static struct { uint16_t x, y; uint32_t t; uint8_t on; } rip[4]; static uint8_t rip_n;
static const uint8_t KEYPOS[15] = {0, 1, 2, 3, 5, 6, 7, 8, 10, 11, 12, 13, 15, 16, 17};
static const uint8_t PAL7[7][3] = {{255, 0, 0}, {255, 110, 0}, {255, 220, 0}, {0, 255, 40}, {0, 220, 255}, {0, 60, 255}, {190, 0, 255}};
#define RIP_MS 1400
static struct { uint32_t t; int8_t i; uint8_t h, on; } hits[8]; static uint8_t hits_n;   // cac lan bam gan day (hieu ung 16-24)
static uint32_t hmap[STRIP_N], trl[STRIP_N];                                             // do nong (ban do nhiet) va vet sao choi, x1024
static void hits_add(int i) { uint8_t k = hits_n++ & 7; hits[k].t = ms(); hits[k].i = i; hits[k].h = rnd32() >> 24; hits[k].on = 1; }
static uint32_t life256(uint32_t age, uint32_t dur) { return age >= dur ? 0 : 256 - age * 256 / dur; }
static void strip_map(void) {
  static const uint8_t rs[4] = {0, 4, 8, 12}, rc[4] = {4, 4, 4, 3};
  memset(klut, -1, sizeof klut); memset(gidx, -1, sizeof gidx);
  for (int i = 0; i < STRIP_N; i++) { sx[i] = i & 3; sy[i] = (i >> 2) & 3; }
  for (int i = 0; i < 15; i++) {
    int r = KEYPOS[i] / 5, c = KEYPOS[i] % 5, idx = rs[r] + ((STRIP_SNAKE && (r & 1)) ? rc[r] - 1 - c : c);
    if (idx < STRIP_N && r < 4 && c < 4) { klut[KEYPOS[i]] = idx; sx[idx] = c; sy[idx] = r; gidx[r][c] = idx; }
  }
}
static void ripple_add(int x16, int y16) { rip[rip_n & 3].x = x16; rip[rip_n & 3].y = y16; rip[rip_n & 3].t = ms(); rip[rip_n & 3].on = 1; rip_n++; }
static void strip_key(int p) {
  if (p >= 0 && p < NK && klut[p] >= 0) {
    int i = klut[p]; sfl[i] = 255; keyh[i] = rnd32() >> 24; ripple_add(sx[i] * 16, sy[i] * 16);
    hits_add(i); hmap[i] += 60u << 10; if (hmap[i] > 255u << 10) hmap[i] = 255u << 10; trl[i] = 255u << 10;
  } else {
    uint8_t h = rnd32() >> 24; for (int i = 0; i < STRIP_N; i++) { sfl[i] = 170; keyh[i] = h; hmap[i] += 25u << 10; if (hmap[i] > 255u << 10) hmap[i] = 255u << 10; trl[i] = 200u << 10; }
    ripple_add(24, 24); hits_add(-1);
  }
}
static void strip_all(void) { strip_key(-1); }
static void strip_task(uint32_t now) {
  if (now - st_t < 16) return; st_t = now;
  uint32_t spdv = ldv(LD_SPEED, 255), spd = 20 + spdv, dt = now - ph_t; ph_t = now; if (dt > 100) dt = 16; ph += dt * spd;
  uint32_t mode = ldv(LD_MODE, NFX - 1), bri = ldv(LD_BRIGHT, 255), sat = ldv(LD_SAT, 255), ov = LD(LD_OVER) != 0, u = ph >> 12;
  uint8_t H0 = ldv(LD_HUE, 359) * 256 / 360;
  int a = 0; for (int l = L - 1; l > 0; l--) if (layers >> l & 1) { a = l; break; }
  uint8_t lc[3]; led_col(a, lc);
  uint32_t dur = 500 + (255 - spdv) * 4;
  { uint32_t dh = dt * 20, dtr = dt * 255u * 1024 / (3500 - spdv * 8);   // ban do nhiet nguoi dan, vet sao choi tan dan
    for (int i = 0; i < STRIP_N; i++) { hmap[i] = hmap[i] > dh ? hmap[i] - dh : 0; trl[i] = trl[i] > dtr ? trl[i] - dtr : 0; }
    for (int r = 0; r < 8; r++) if (hits[r].on && now - hits[r].t >= 2600) hits[r].on = 0; }
  // mo phong co trang thai (lua, lap lanh)
  if (mode == 9 && now - fire_t >= 90 - spdv / 4) {
    fire_t = now;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
      int i = gidx[y][x]; if (i < 0) continue;
      int b = y < 3 ? gidx[y + 1][x] : -1;
      if (b >= 0) { int h = heat[b] * 7 / 8 - (int)(rnd32() % 30); heat[i] = h < 0 ? 0 : h; }
      else heat[i] = (heat[i] * 2 + 130 + rnd32() % 126) / 3;
    }
  }
  if (mode == 10) {
    for (int i = 0; i < STRIP_N; i++) tw[i] = tw[i] > 5 ? tw[i] - 5 : 0;
    if ((rnd32() & 255) < 20 + spdv / 4) { int i = rnd32() % STRIP_N; tw[i] = 255; twh[i] = H0 + (rnd32() & 63); }
  }
  uint8_t fr[STRIP_N][3]; uint32_t sum = 0;
  for (int i = 0; i < STRIP_N; i++) {
    uint8_t c[3] = {0, 0, 0}, tmp[3]; int x = sx[i], y = sy[i]; bool ovl = ov; uint32_t f = sfl[i];
    switch (mode) {
      case 0: for (int k = 0; k < 3; k++) c[k] = lc[k] * STRIP_BASE / 255; break;
      case 1: for (int k = 0; k < 3; k++) c[k] = PAL7[(i + u / 6) % 7][k] * STRIP_BASE / 255; break;
      case 2: ovl = false; break;
      case 3: hsv(H0, sat, 255, c); break;
      case 4: { uint32_t t = tri8(u * 2); hsv(H0, sat, 12 + (t * t / 255) * 243 / 255, c); break; }
      case 5: hsv(u + x * 40 + y * 24, sat, 255, c); break;
      case 6: hsv(u, sat, 255, c); break;
      case 7: { int pos = (int)(((u * 2) & 255) * STRIP_N / 256), d = (pos - i + STRIP_N) % STRIP_N; hsv(H0, sat, d < 6 ? 255 - d * 45 : 10, c); break; }
      case 8: {
        uint32_t v = 0;
        for (int r = 0; r < 4; r++) {
          if (!rip[r].on) continue; uint32_t age = now - rip[r].t; if (age > RIP_MS) { rip[r].on = 0; continue; }
          int R = age * (32 + spdv / 2) / 1000, dx = abs(x * 16 - (int)rip[r].x), dy = abs(y * 16 - (int)rip[r].y), d = (dx > dy ? dx + dy / 2 : dy + dx / 2), del = abs(d - R);
          if (del < 24) { uint32_t val = 255 * (24 - del) / 24 * (RIP_MS - age) / RIP_MS; if (val > v) v = val; }
        }
        hsv(H0, sat, v < 8 ? 8 : v, c); ovl = false; break;
      }
      case 9: heat_rgb(heat[i], c); ovl = false; break;
      case 10: hsv(twh[i], sat, tw[i], c); break;
      case 11: { int head = (int)(((u * 4 + x * 77) & 255) * 7 / 256), d = head - y; hsv(H0, sat, d == 0 ? 255 : d == 1 ? 150 : d == 2 ? 70 : d == 3 ? 25 : 0, c); break; }
      case 12: hsv(H0 + (tri8(u * 2 + x * 50 + y * 30) >> 1), sat, 120 + (tri8(u * 3 + x * 70) >> 1), c); break;
      case 13: hsv(H0, sat, 8 + f * 247 / 255, c); ovl = false; break;
      case 14: { int p16 = tri8(u * 2) * 48 / 255, d = abs(x * 16 - p16); hsv(H0, sat, d < 40 ? 12 + 243 * (40 - d) / 40 : 12, c); break; }
      case 16: case 17: case 18: {
        uint32_t v = 0;
        for (int r = 0; r < 8; r++) {
          if (!hits[r].on) continue; uint32_t l = life256(now - hits[r].t, dur);
          if (hits[r].i < 0) { uint32_t q = l * 140 / 256; if (q > v) v = q; continue; }
          int hx = sx[hits[r].i], hy = sy[hits[r].i];
          bool hh = mode == 16 ? x == hx : mode == 17 ? y == hy : (x == hx || y == hy);
          if (hh) { uint32_t q = l * (hits[r].i == i ? 255 : 200) / 256; if (q > v) v = q; }
        }
        hsv(H0, sat, v < 6 ? 6 : v, c); ovl = false; break;
      }
      case 19: {
        uint32_t v = 0;
        for (int r = 0; r < 8; r++) {
          if (!hits[r].on) continue; uint32_t a2 = now - hits[r].t; if (a2 > 380) continue;
          uint32_t l = 256 - a2 * 256 / 380;
          if (hits[r].i == i || ((i * 7u + (a2 / 45) * 13 + hits[r].h) % 5) < 2) { uint32_t q = 255 * l / 256; if (q > v) v = q; }
        }
        hsv(H0, sat / 2, v < 6 ? 6 : v, c); ovl = false; break;
      }
      case 20: { uint32_t t = hmap[i] >> 10; heat_rgb(t > 255 ? 255 : t, c); ovl = false; break; }
      case 21: { uint32_t t = trl[i] >> 10; if (t > 255) t = 255; hsv(H0 + (255 - t) / 5, sat, t < 5 ? 5 : t, c); ovl = false; break; }
      case 22: {
        uint32_t lm = 0;
        for (int r = 0; r < 8; r++) {
          if (!hits[r].on) continue; uint32_t l = life256(now - hits[r].t, dur), q = hits[r].i == i ? l : hits[r].i < 0 ? l / 2 : 0;
          if (q > lm) lm = q;
        }
        uint8_t b0[3], b1[3]; hsv(H0, sat, 150, b0); hsv(H0 + 128, sat, 255, b1);
        for (int k = 0; k < 3; k++) c[k] = (b0[k] * (256 - lm) + b1[k] * lm) / 256;
        ovl = false; break;
      }
      case 23: {
        uint32_t v = 0; uint8_t hh = H0;
        for (int r = 0; r < 8; r++) {
          if (!hits[r].on) continue; uint32_t a2 = now - hits[r].t;
          int d2 = hits[r].i < 0 ? abs(2 * x - 3) + abs(2 * y - 3) : 2 * (abs(x - sx[hits[r].i]) + abs(y - sy[hits[r].i]));
          int r256 = (int)(a2 * 5 * 256 / dur), del = abs(d2 * 128 - r256);
          if (del < 179) { uint32_t val = 255 * (179 - del) / 179 * life256(a2, dur) / 256; if (val > v) { v = val; hh = H0 + d2 * 11; } }
        }
        hsv(hh, sat, v < 6 ? 6 : v, c); ovl = false; break;
      }
      case 24: {
        uint32_t v = 0; uint8_t hh = 0;
        for (int r = 0; r < 8; r++) {
          if (!hits[r].on) continue; int a2 = (int)(now - hits[r].t); uint32_t seed = (i * 37 + hits[r].h * 11) & 255;
          a2 -= (int)(seed % 6 * dur / 10);
          if (a2 > 0) { int num = 10 * (int)dur - 14 * a2; if (num > 0) { uint32_t val = 255u * num / (10 * dur); if (val > v) { v = val; hh = hits[r].h + seed; } } }
        }
        hsv(hh, 255, v, c); ovl = false; break;
      }
      default: { hsv(u + i * 17, sat, 10, c); hsv(keyh[i], sat, 255, tmp); for (int k = 0; k < 3; k++) c[k] = (c[k] * (255 - f) + tmp[k] * f) / 255; ovl = false; break; }
    }
    if (ovl && f) for (int k = 0; k < 3; k++) { uint32_t tgt = mode == 0 ? (lc[k] + 110 > 255 ? 255 : lc[k] + 110) : 255; c[k] = (c[k] * (255 - f) + tgt * f) / 255; }
    if (sfl[i]) sfl[i] = sfl[i] > 10 ? sfl[i] - 10 : 0;
    for (int k = 0; k < 3; k++) { fr[i][k] = c[k] * bri / 255; sum += fr[i][k]; }
  }
  const uint32_t pmax = STRIP_N * 3 * 255 * 55 / 100;      // gioi han dong: tong ~55% trang toan day (~500mA)
  uint32_t frame[STRIP_N];
  for (int i = 0; i < STRIP_N; i++) {
    uint32_t o[3]; for (int k = 0; k < 3; k++) o[k] = sum > pmax ? fr[i][k] * pmax / sum : fr[i][k];
    frame[i] = o[1] << 16 | o[0] << 8 | o[2];
  }
  if (memcmp(frame, st_last, sizeof frame)) {   // chi gui ca khung khi co doi mau
    memcpy(st_last, frame, sizeof frame);
    for (int i = 0; i < STRIP_N; i++) pio_sm_put_blocking(pio0, 1, frame[i] << 8);
  }
}

// ---- den onboard: 0 mau layer, 1 cau vong, 2 mau tinh, 3 tho, 4 tat; nhay sang khi bam phim/xoay encoder ----
static void led_task(uint32_t now) {
  if (now - led_t < 8) return; led_t = now;
  uint32_t m = ldv(LD_OBMODE, NOB - 1), base = ldv(LD_OBBRI, 255), sat = ldv(LD_SAT, 255), u = ph >> 12;
  uint8_t H0 = ldv(LD_HUE, 359) * 256 / 360, col[3] = {0, 0, 0};
  int a = 0; for (int l = L - 1; l > 0; l--) if (layers >> l & 1) { a = l; break; }
  switch (m) {
    case 0: led_col(a, col); break;
    case 1: hsv(u, 255, 255, col); break;
    case 2: hsv(H0, sat, 255, col); break;
    case 3: { hsv(H0, sat, 255, col); uint32_t t = tri8(u * 2); base = 4 + base * (t * t / 255) / 255; break; }
    default: break;
  }
  uint32_t lv = base + (255 - base) * flash_lv / 255;      // sang dan ve nen
  uint32_t wh = flash_lv * flash_lv / 255 * 90 / 255;       // tron them trang khi vua bam
  uint8_t o[3];
  for (int i = 0; i < 3; i++) { uint32_t v = col[i] * lv / 255 + wh * lv / 255; o[i] = m == 4 ? 0 : v > 255 ? 255 : v; }
  if (flash_lv) flash_lv = flash_lv > 14 ? flash_lv - 14 : 0;
  uint32_t k = (uint32_t)o[0] << 16 | o[1] << 8 | o[2];
  if (k != led_last) { led_last = k; led_put(o[0], o[1], o[2]); }
}

static void led_flash(void);
static void cycle_layer(int dir) {
  if (rn_active) return;   // chuyen toggle sang layer ke tiep/truoc theo vong
  int n = nl(), cur = 0; for (int l = L - 1; l > 0; l--) if (tl >> l & 1) { cur = l; break; }
  int nx = (cur + dir + n) % n; tl = nx ? 1u << nx : 0;
  relayer(); dirty = true; led_flash(); oled_forget();   // hien ngay ten layer moi
}
static void note(uint16_t kc, const char *s);
static void led_cmd(int n) {   // phim LED: 0/1 che do +/-, 2/3 sang +/-, 4/5 toc do +/-, 6 doi mau
  char s[8] = "FX "; int v = 0;
  if (n < 2) { int m = (int)ldv(LD_MODE, NFX - 1); m = (m + (n ? NFX - 1 : 1)) % NFX; LD(LD_MODE) = m; v = m; }
  else if (n < 4) { int b = (int)ldv(LD_BRIGHT, 255) + (n == 2 ? 32 : -32); b = b > 255 ? 255 : b < 16 ? 16 : b; LD(LD_BRIGHT) = b; v = b * 100 / 255; s[0] = 'B'; s[1] = ' '; }
  else if (n < 6) { int p = (int)ldv(LD_SPEED, 255) + (n == 4 ? 24 : -24); p = p > 255 ? 255 : p < 0 ? 0 : p; LD(LD_SPEED) = p; v = p * 100 / 255; s[0] = 'S'; s[1] = 'P'; }
  else { int h = ((int)ldv(LD_HUE, 359) + 20) % 360; LD(LD_HUE) = h; v = h; s[0] = 'H'; s[1] = ' '; }
  int i = s[0] == 'F' ? 3 : 2;
  if (v >= 100) s[i++] = '0' + v / 100;
  if (v >= 10) s[i++] = '0' + v / 10 % 10;
  s[i++] = '0' + v % 10; s[i] = 0;
  note(0x7100 + n, s);
}
static bool layer_action(uint16_t kc) {   // TL(n), LY+, LY-: tra ve true neu da xu ly
  if ((kc & 0xFF00) == 0x4100) { if (rn_active) return true; int n = kc & 31; if (n) tl ^= 1u << n; else tl = 0; relayer(); dirty = true; char s[8]; if (n) { strcpy(s, "TG "); putnum(s + 3, n); } else strcpy(s, "TG CLR"); note(kc, s); return true; }
  if (kc >= KC_LEDFX && kc <= KC_LEDFX_LAST) { led_cmd(kc - KC_LEDFX); return true; }
  if ((kc & 0xFF00) == LYN) { cycle_layer((kc & 1) ? -1 : 1); return true; }
  return false;
}

// ---- ten phim cho OLED (giong ban RP2040) ----
static const struct { uint16_t k; const char *n; } NAMES[] = {
  {0x28, "ENTER"}, {0x58, "ENTER"}, {0x29, "ESC"}, {0x2A, "BKSP"}, {0x2B, "TAB"}, {0x2C, "SPACE"}, {0x4C, "DEL"}, {0x49, "INS"}, {0x4A, "HOME"}, {0x4D, "END"},
  {0x4B, "PG UP"}, {0x4E, "PG DN"}, {0x52, "UP"}, {0x51, "DOWN"}, {0x50, "LEFT"}, {0x4F, "RIGHT"},
  {0x37, "."}, {0x36, ","}, {0x38, "/"}, {0x2D, "-"}, {0x2E, "="}, {0x54, "/"}, {0x55, "*"}, {0x56, "-"}, {0x57, "+"}, {0x63, "."}, {0x62, "0"},
  {0xE0, "CTRL"}, {0xE4, "CTRL"}, {0xE1, "SHIFT"}, {0xE5, "SHIFT"}, {0xE2, "ALT"}, {0xE6, "ALT"}, {0xE3, "GUI"}, {0xE7, "GUI"},
  {CONS(0xE2), "MUTE"}, {CONS(0xE9), "VOL +"}, {CONS(0xEA), "VOL -"}, {CONS(0xCD), "PLAY"}, {CONS(0xB5), "NEXT"}, {CONS(0xB6), "PREV"}, {CONS(0x192), "CALC"},
  {KC_BOOT, "BOOT"}, {KC_LATCH, "LATCH"}, {KC_RENAME, "RENAME"}};
static void kcname(uint16_t k, char *o) {   // o >= 8 byte
  o[0] = 0;
  if ((k & 0xF000) == 0x2000 && (k & 0xFF)) { char sub[8]; kcname(k & 0xFF, sub); o[0] = 'S'; o[1] = '+'; strncpy(o + 2, sub, 5); o[7] = 0; return; }
  if ((k & 0xFF00) == 0x4000) { strcpy(o, "MO "); putnum(o + 3, k & 31); return; }
  if ((k & 0xFF00) == 0x4100) { if (k & 31) { strcpy(o, "TG "); putnum(o + 3, k & 31); } else strcpy(o, "TG CLR"); return; }
  if (k >= 4 && k <= 29) { o[0] = 'A' + k - 4; o[1] = 0; return; }
  if (k >= 30 && k <= 39) { o[0] = k == 39 ? '0' : '1' + k - 30; o[1] = 0; return; }
  if (k >= 0x59 && k <= 0x61) { o[0] = '1' + k - 0x59; o[1] = 0; return; }
  if (k >= 0x3A && k <= 0x45) { o[0] = 'F'; putnum(o + 1, k - 0x39); return; }
  if (k >= 0x68 && k <= 0x73) { o[0] = 'F'; putnum(o + 1, k - 0x68 + 13); return; }
  for (unsigned i = 0; i < sizeof NAMES / sizeof *NAMES; i++) if (NAMES[i].k == k) { strcpy(o, NAMES[i].n); return; }
  strcpy(o, "KEY");
}
// cac phim dang de, theo thu tu bam (cu -> moi), toi da 4 phim moi nhat, khong trung
static int held_list(uint16_t *out) {
  static const uint16_t tgk[4] = {0x2C, 0xE1, 0xE0, 0xE2};
  uint32_t sv[NK + 5]; uint16_t kv[NK + 5]; int n = 0;
  for (int i = 0; i < NK + 5; i++) {
    uint16_t c = i < NK ? pressed[i] : i == NK ? lat : ((tg >> (i - NK - 1) & 1) ? tgk[i - NK - 1] : 0);
    if (!c || c == KC_TRNS) continue;
    sv[n] = pseq[i]; kv[n++] = c;
  }
  for (int i = 1; i < n; i++) { uint32_t a = sv[i]; uint16_t b = kv[i]; int j = i - 1; while (j >= 0 && sv[j] > a) { sv[j + 1] = sv[j]; kv[j + 1] = kv[j]; j--; } sv[j + 1] = a; kv[j + 1] = b; }
  uint16_t u[NK + 5]; int m = 0;
  for (int i = 0; i < n; i++) { bool d = false; for (int j = 0; j < m; j++) if (u[j] == kv[i]) d = true; if (!d) u[m++] = kv[i]; }
  int f = m > 4 ? m - 4 : 0; for (int i = f; i < m; i++) out[i - f] = u[i];
  return m - f;
}
static bool held_has(uint16_t kc) { uint16_t h[4]; int n = held_list(h); for (int i = 0; i < n; i++) if (h[i] == kc) return true; return false; }
static char lastn[8]; static uint16_t rkc, rcnt;
// phim vua bam: hien to giua man hinh; xN = so lan bam lien tiep cung phim (con hien hoac con de, het 1,2 s thi dem lai)
static void note(uint16_t kc, const char *s) {
  uint32_t n = ms();
  if (lastn[0] && kc == rkc && (n - ot < 1200 || held_has(kc))) { if (rcnt < 999) rcnt++; } else { rcnt = 1; rkc = kc; }
  ot = n; strncpy(lastn, s, 7); lastn[7] = 0; odirty = true;
}
static void oled_forget(void) { lastn[0] = 0; rcnt = 0; rkc = 0; odirty = true; }   // ve man hinh ten layer ngay

// ---- Nokia multitap ----
static const char *NKT[11] = {"1abc", "2def", "3ghi", "4jkl", "5mno", "6pqr", "7stu", "8vwx", "9yz", "0 ", ".,?!"};
static uint16_t ckc(char c) {
  if (c >= 'a') return 4 + c - 'a'; if (c == '0') return 39; if (c >= '1' && c <= '9') return 30 + c - '1';
  switch (c) { case ' ': return 0x2C; case '.': return 0x37; case ',': return 0x36; case '?': return 0x2038; default: return 0x201E; }
}
static void nokia(int i) {
  static int ki = -1; static uint32_t kt; static int kn;
  if (i > 10) return; uint32_t n = ms();
  if (i == ki && n - kt < NK_TERM) { kn++; qpush(0x2A); } else kn = 0;
  ki = i; kt = n; char ch = NKT[i][kn % strlen(NKT[i])]; qpush(ckc(ch));
  char s[4] = {ch >= 'a' ? ch - 32 : ch, 0}; if (ch == ' ') strcpy(s, "SPC"); note(NKK(i), s);
}

// ---- doi ten layer (giong ban RP2040): bam phim RENAME -> go ten bang phim Nokia (layer 2), ENTER = luu, BKSP = xoa, bam RENAME/ESC/TAB = huy ----
static uint8_t rn_layer, rn_len, rn_idx; static int8_t rn_last = -1; static char rn_buf[NM + 1]; static uint32_t rn_time, rn_sw;
static void rename_start(void) {
  if (rn_active) return;
  int a = 0; for (int l = L - 1; l > 0; l--) if (layers >> l & 1) { a = l; break; }
  rn_layer = a; lname_get(a, rn_buf); rn_len = strlen(rn_buf); rn_last = -1; rn_sw = 0; rn_active = true; odirty = true;
}
static void rename_finish(bool save) {
  if (save) { if (rn_len == 0) lname.n[rn_layer][0] = 0; else memcpy(lname.n[rn_layer], rn_buf, NM + 1); nsave_req = true; }
  rn_active = false; oled_forget();
}
static void rename_key(int p) {   // p = vi tri phim; dich theo phim cua layer 2 (Nokia)
  uint16_t kc = KM(2, p); if (kc == KC_TRNS || !kc) kc = res_key(p);
  if ((kc & 0xF000) == 0x6000 && (kc & 15) <= 10) {
    int i = kc & 15; const char *t = NKT[i]; int tl2 = strlen(t); uint32_t n = ms();
    if (rn_last == i && rn_len > 0 && n - rn_time < NK_TERM) rn_idx = (rn_idx + 1) % tl2;
    else { if (rn_len >= NM) return; rn_idx = 0; rn_len++; }
    char c = t[rn_idx]; if (c >= 'a' && c <= 'z') c -= 32;
    rn_buf[rn_len - 1] = c; rn_buf[rn_len] = 0; rn_last = i; rn_time = n; odirty = true; return;
  }
  if (kc == 0x2A) { if (rn_len) rn_buf[--rn_len] = 0; rn_last = -1; odirty = true; }
  else if (kc == 0x28 || kc == 0x58) rename_finish(true);
  else if (kc == 0x29 || kc == 0x2B || kc == KC_RENAME) rename_finish(false);
}

// ---- tap dance (1 cai tai 1 thoi diem) ----
static uint8_t tds, tdp, tdn, tdc; static uint32_t tdt;
static void td_press(int p, int n) {
  if (tds == 0) { tds = 1; tdp = p; tdn = n; tdc = 1; tdt = ms(); }
  else if (tds == 3 && tdn == n) { tds = 1; tdc = 2; tdp = p; tdt = ms(); }
}
static void td_release(int p) {
  if (p != tdp) return;
  if (tds == 1) { if (tdc == 2) { qpush(TDW(tdn, 2)); tds = 0; } else { tds = 3; tdt = ms(); } }
  else if (tds == 2) tds = 0;
}
static void td_task(uint32_t now) {
  if (tds == 1 && now - tdt >= TAP_TERM) { pressed[tdp] = TDW(tdn, 1); pseq[tdp] = ++sq; tds = 2; relayer(); dirty = true; }
  else if (tds == 3 && now - tdt >= TAP_TERM) { qpush(TDW(tdn, 0)); tds = 0; }
}

// ---- macro: 16 o, luu flash rieng (MAC_OFF), chay khong chan bang may trang thai. Buoc: 1 nhan, 2 giu, 3 tha (ma 16 bit), 4 cho (ms 16 bit), 5 go chu (n, ky tu ASCII) ----
static uint8_t macimg[MAC_SZ] __attribute__((aligned(4)));
static bool mac_dirty;
static uint8_t *mslot(int i) { return macimg + 4 + i * MSLOT; }
static void mac_put(int i, const char *name, uint8_t rp, uint16_t gap, const uint8_t *st, uint16_t n) {
  uint8_t *m = mslot(i); memset(m, 0, MSLOT); uint8_t nl = strlen(name); if (nl > 12) nl = 12;
  m[0] = rp; m[1] = gap & 255; m[2] = gap >> 8; m[3] = nl; memcpy(m + 4, name, nl); m[16] = n & 255; m[17] = n >> 8; if (n) memcpy(m + 18, st, n);
}
static void mac_defaults(void) {
  static const uint8_t cp[] = {2, 0xE0, 0, 1, 6, 0, 3, 0xE0, 0};                                        // giu Ctrl, nhan C, tha Ctrl
  static const uint8_t hi[] = {5, 9, 'X', 'i', 'n', ' ', 'c', 'h', 'a', 'o', '!', 1, 40, 0};            // go "Xin chao!" roi Enter
  uint32_t mg = MAC_MAGIC; memset(macimg, 0, sizeof macimg); memcpy(macimg, &mg, 4);
  for (int i = 0; i < NMAC; i++) mac_put(i, "", 1, 0, NULL, 0);
  mac_put(0, "Sao chep", 1, 0, cp, sizeof cp); mac_put(1, "Xin chao", 1, 0, hi, sizeof hi);
}
static void mac_load(void) {
  uint32_t mg; memcpy(macimg, (const void *)(XIP_BASE + MAC_OFF), MAC_SZ); memcpy(&mg, macimg, 4);
  if (mg != MAC_MAGIC) mac_defaults();
}
static void do_save_mac(void *p) { (void)p; flash_range_erase(MAC_OFF, MAC_SZ); flash_range_program(MAC_OFF, macimg, MAC_SZ); }

static uint16_t mc_held[MH]; static int8_t mc_id = -1; static uint16_t mc_pos, mc_ti, mc_cur; static uint8_t mc_rep, mc_ph; static uint32_t mc_until;
static bool mc_ok(uint16_t c) { return c < 0x100 || (c & 0xF000) == 0x2000 || (c & 0x8000); }
static void mc_down(uint16_t c) {
  if (!c || !mc_ok(c)) return;
  for (int i = 0; i < MH; i++) if (mc_held[i] == c) return;
  for (int i = 0; i < MH; i++) if (!mc_held[i]) { mc_held[i] = c; dirty = true; return; }
}
static void mc_up(uint16_t c) { for (int i = 0; i < MH; i++) if (mc_held[i] == c) { mc_held[i] = 0; dirty = true; } }
static void mc_stop(void) { memset(mc_held, 0, sizeof mc_held); mc_id = -1; mc_ph = 0; mc_cur = 0; dirty = odirty = true; }
static uint16_t mc_ascii(uint8_t ch) {
  static const char U[] = " \n\t-=[]\\;'`,./"; static const uint8_t UK[] = {0x2C, 0x28, 0x2B, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
  static const char S[] = "!@#$%^&*()_+{}|:\"~<>?"; static const uint8_t SK[] = {30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
  if (ch >= 'a' && ch <= 'z') return 4 + ch - 'a';
  if (ch >= 'A' && ch <= 'Z') return 0x2000 | (4 + ch - 'A');
  if (ch >= '1' && ch <= '9') return 30 + ch - '1';
  if (ch == '0') return 39;
  if (!ch) return 0;
  const char *q = strchr(U, ch); if (q) return UK[q - U];
  q = strchr(S, ch); if (q) return 0x2000 | SK[q - S];
  return 0;
}
static void mc_start(int id) {
  if (mc_id >= 0) { mc_stop(); note(0x7100 + id, "STOP"); return; }   // bam them 1 phim macro khi dang chay = dung
  const uint8_t *m = mslot(id); uint16_t n = m[16] | m[17] << 8;
  char nm[8]; if (m[3]) { memcpy(nm, m + 4, 7); nm[m[3] < 7 ? m[3] : 7] = 0; } else { nm[0] = 'M'; putnum(nm + 1, id + 1); }
  note(0x7100 + id, nm);
  if (!n || n > MSLOT - 18) return;
  mc_id = id; mc_pos = 0; mc_ti = 0; mc_ph = 0; mc_rep = m[0] < 1 ? 1 : m[0] > 99 ? 99 : m[0]; mc_until = ms();
}
static void mc_task(uint32_t now) {
  if (mc_id < 0 || (int32_t)(now - mc_until) < 0) return;
  const uint8_t *m = mslot(mc_id), *st = m + 18; uint16_t len = m[16] | m[17] << 8;
  if (len > MSLOT - 18) len = 0;
  if (mc_ph == 1) { mc_up(mc_cur); mc_cur = 0; mc_ph = 0; mc_until = now + 20; return; }   // nha phim cua buoc nhan / go chu
  if (mc_pos >= len) {
    if (mc_rep > 1) { mc_rep--; mc_pos = 0; mc_ti = 0; mc_until = now + (m[1] | m[2] << 8); }
    else mc_stop();
    return;
  }
  uint16_t c = mc_pos + 3 <= len ? st[mc_pos + 1] | st[mc_pos + 2] << 8 : 0;
  switch (st[mc_pos]) {
    case 1: mc_pos += 3; mc_down(c); mc_cur = c; mc_ph = 1; mc_until = now + 30; break;
    case 2: mc_pos += 3; mc_down(c); mc_until = now + 10; break;
    case 3: mc_pos += 3; mc_up(c); mc_until = now + 10; break;
    case 4: mc_pos += 3; mc_until = now + c; break;
    case 5: {
      uint8_t n = mc_pos + 1 < len ? st[mc_pos + 1] : 0;
      if (mc_pos + 2 + n > len) { mc_pos = len; break; }
      if (mc_ti >= n) { mc_pos += 2 + n; mc_ti = 0; break; }
      uint16_t k = mc_ascii(st[mc_pos + 2 + mc_ti++]);
      if (k) { mc_down(k); mc_cur = k; mc_ph = 1; mc_until = now + 20; }
      break;
    }
    default: mc_pos = len; break;
  }
}

// ---- xu ly nhan phim ----
static void fire(int p, uint16_t kc) {
  if (!kc || kc == KC_TRNS) return;
  if (kc == KC_LATCH) { if (lat) { lat = 0; ot = ms(); } else latch_wait = !latch_wait; odirty = dirty = true; return; }
  if (kc > KC_LATCH && kc <= KC_LATCH + 4) {
    static const uint16_t tgk2[4] = {0x2C, 0xE1, 0xE0, 0xE2}; int b = kc - KC_LATCH - 1; tg ^= 1 << b; dirty = odirty = true; ot = ms();
    if (tg >> b & 1) { pseq[NK + 1 + b] = ++sq; char nm[8]; kcname(tgk2[b], nm); note(tgk2[b], nm); }
    return;
  }
  if (kc == KC_RENAME) { rename_start(); return; }
  if (kc == KC_BOOT) reset_usb_boot(0, 0);
  if ((kc & 0xFF00) == 0x7100 && (kc & 255) < NMAC) { if (!latch_wait) mc_start(kc & 255); return; }
  if ((kc & 0xF000) == 0x5000) { td_press(p, kc & 3); return; }
  if ((kc & 0xF000) == 0x6000) { nokia(kc & 15); return; }
  if (layer_action(kc)) return;
  if ((kc & 0xFF00) == 0x4000) { char nm[8]; kcname(kc, nm); note(kc, nm); pressed[p] = kc; pseq[p] = ++sq; return; }
  char nm[8]; kcname(kc, nm); note(kc, nm);
  if (latch_wait) { lat = kc; pseq[NK] = ++sq; latch_wait = false; odirty = dirty = true; return; }
  pressed[p] = kc; pseq[p] = ++sq;
}
static int pend = -1; static uint16_t pkc; static uint32_t pt;
static bool in_combo(uint16_t kc) {
  if (!kc || kc == KC_TRNS || (kc >= 0x4000 && !(kc & 0x8000))) return false;
  for (int n = 0; n < 4; n++) if (CB(n, 0) && (CB(n, 0) == kc || CB(n, 1) == kc)) return true;
  return false;
}
static void apply(uint32_t raw) {
  static uint32_t st;
  for (int p = 0; p < NK; p++) {
    uint32_t b = 1u << p; if (!((raw ^ st) & b)) continue;
    if (rn_active) {   // dang doi ten: phim vua bam di vao o ten, khong gui xuong may tinh
      if (raw & b) { rn_sw |= b; rename_key(p); continue; }
      else if (rn_sw & b) { rn_sw &= ~b; continue; }
    }
    if (raw & b) {
      led_flash(); strip_key(p);
      uint16_t kc = res_key(p);
      if (pend >= 0) {
        int hit = -1;
        for (int n = 0; n < 4; n++) if (CB(n, 0) && ((CB(n, 0) == pkc && CB(n, 1) == kc) || (CB(n, 1) == pkc && CB(n, 0) == kc))) hit = n;
        if (hit >= 0) { char nm[8]; kcname(CB(hit, 2), nm); note(CB(hit, 2), nm); qpush(CB(hit, 2)); pend = -1; continue; }
        fire(pend, pkc); pend = -1;
      }
      if (in_combo(kc)) { pend = p; pkc = kc; pt = ms(); } else fire(p, kc);
    } else {
      pressed[p] = 0; ot = ms();
      if (pend == p) { char nm[8]; kcname(pkc, nm); note(pkc, nm); qpush(pkc); pend = -1; }
      td_release(p);
    }
    relayer();
  }
  st = raw; dirty = true; odirty = true;
}

static uint32_t scan(void) {
  uint32_t m = 0;
  for (int r = 0; r < 4; r++) {
    gpio_put(rows[r], 1); busy_wait_us(5);
    for (int c = 0; c < 5; c++) if (gpio_get(cols[c])) m |= 1u << (r * 5 + c);
    gpio_put(rows[r], 0);
  }
  return m;
}
static void enc_poll(void) {
  static const int8_t t[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  static uint8_t prev[3]; static int8_t acc[3];
  for (int e = 0; e < 3; e++) {
    uint8_t cur = gpio_get(encp[e][0]) << 1 | gpio_get(encp[e][1]);
    if (cur == prev[e]) continue;
    acc[e] += t[prev[e] << 2 | cur]; prev[e] = cur;
    int d = acc[e] >= 4 ? 1 : acc[e] <= -4 ? 0 : -1;
    if (d >= 0) { acc[e] = 0; if (rn_active) continue; led_flash(); strip_all(); uint16_t k = res_enc(e, d); if (layer_action(k)) { /* da chuyen layer */ } else if ((k & 0xFF00) == 0x7100 && (k & 255) < NMAC) { if (mc_id < 0) mc_start(k & 255); } else if (k) { char nm[8]; kcname(k, nm); note(k, nm); qpush(k); } }
  }
}
static void hid_task(void) {
  static const uint8_t tgk[4] = {0x2C, 0xE1, 0xE0, 0xE2};
  static uint8_t lm, lk[6]; static uint16_t lc;
  if (!tud_hid_n_ready(0)) return;
  if (tap_phase == 2) { tap = 0; tap_phase = 0; dirty = true; }
  else if (tap_phase == 0 && qh != qt) { tap = q[qt++ & 15]; tap_phase = 1; dirty = true; }
  if (!dirty) return;
  uint8_t mod = 0, k[6] = {0}; int n = 0; uint16_t cons = 0;
  for (int i = 0; i < NK + 6 + MH; i++) {
    uint16_t c = i < NK ? pressed[i] : i == NK ? tap : i == NK + 1 ? lat : i < NK + 6 ? ((tg >> (i - NK - 2) & 1) ? tgk[i - NK - 2] : 0) : mc_held[i - NK - 6];
    if (c & 0x8000) { cons = c & 0x7FFF; continue; }
    if ((c & 0xF000) == 0x2000) { mod |= 2; c &= 0xFF; } else if (c >= 0x100) continue;
    if (c >= 0xE0 && c <= 0xE7) mod |= 1 << (c - 0xE0); else if (c >= 4 && n < 6) k[n++] = c;
  }
  if (mod != lm || memcmp(k, lk, 6)) { lm = mod; memcpy(lk, k, 6); tud_hid_n_keyboard_report(0, 1, mod, k); return; }
  if (cons != lc) { lc = cons; tud_hid_n_report(0, 2, &cons, 2); return; }
  dirty = false; if (tap_phase == 1) tap_phase = 2;
}

// ---- OLED SSD1306 128x64, moi lan gui 1 page de khong chan USB/quet phim ----
#define OA 0x3C
#define OLED_H 32                 // 32 = OLED 0.91" (128x32) ; 64 = OLED 0.96" (128x64)
#define OLED_ROT 180               // xoay man hinh: 0 / 180 = ngang, 90 / 270 = doc
#define OLED_PAGES (OLED_H / 8)
static uint8_t fb[128 * OLED_PAGES], opage = OLED_PAGES; static bool oled_ok;
#if OLED_ROT == 0 || OLED_ROT == 180
#define PXW 128
#define PXH OLED_H
#else
#define PXW OLED_H
#define PXH 128
#endif
static void px(int x, int y) {   // toa do logic (sau xoay) -> toa do man hinh that
  if ((unsigned)x >= PXW || (unsigned)y >= PXH) return;
#if OLED_ROT == 270
  int X = y, Y = OLED_H - 1 - x;
#elif OLED_ROT == 90
  int X = 127 - y, Y = x;
#elif OLED_ROT == 180
  int X = 127 - x, Y = OLED_H - 1 - y;
#else
  int X = x, Y = y;
#endif
  fb[(Y >> 3) * 128 + X] |= 1 << (Y & 7);
}
static void txt(int x, int y, int s, const char *t) {
  for (; *t; t++, x += 6 * s) {
    char ch = *t >= 'a' && *t <= 'z' ? *t - 32 : *t; const char *g = strchr(FC, ch); int i = g ? g - FC : 0;
    for (int c = 0; c < 5; c++) for (int r = 0; r < 7; r++) if (FD[i * 5 + c] >> r & 1) for (int a = 0; a < s; a++) for (int b = 0; b < s; b++) px(x + c * s + a, y + r * s + b);
  }
}
// bo cuc giong ban RP2040: giua = ten layer (ranh) HOAC phim vua bam (chu to nhat), goc tren phai = xN so lan bam, hang duoi = cac phim dang de (toi da 4)
#if OLED_ROT == 0 || OLED_ROT == 180
#if OLED_H == 32
#define CNT_SC 1      // 128x32: chu nho 8 px o tren/duoi, giua chu 2-3 (16-24 px)
#define HELD_SC 1
#define KEY_SC 3
#define IDLE_SC 3
#else
#define CNT_SC 2      // 128x64: giong het ban RP2040
#define HELD_SC 2
#define KEY_SC 4
#define IDLE_SC 3
#endif
#define MAIN_Y (8 * CNT_SC)
static int twd(const char *t, int sc) { int n = strlen(t); return n ? n * 6 * sc - sc : 0; }
static int fitsc(const char *t, int mx) { int sc = mx < 1 ? 1 : mx; while (sc > 1 && twd(t, sc) > 128) sc--; return sc; }
static void ctxt(const char *t, int y, int sc) { int w = twd(t, sc); txt(w < 128 ? (128 - w) / 2 : 0, y, sc, t); }
static void render_rename(void) {
  char top[12] = "RENAME L"; putnum(top + 8, rn_layer); txt(0, 0, 1, top);
  int n = rn_len + (rn_len < NM ? 1 : 0), sc = OLED_H == 32 ? 2 : 3; while (sc > 1 && n * 6 * sc - sc > 128) sc--;
  int w = n * 6 * sc - sc, x = (128 - w) / 2, y = OLED_H == 32 ? 8 : 16 + (32 - 8 * sc) / 2;
  txt(x, y, sc, rn_buf);
  if (rn_len < NM) for (int a = 0; a < 5 * sc; a++) for (int b = 0; b < (OLED_H == 32 ? 1 : sc); b++) px(x + rn_len * 6 * sc + a, y + 7 * sc + b);   // con tro (font khong co '_')
  txt(0, OLED_H - 8, 1, "ENT:OK RENAME:CANCEL");
}
static void render(bool vis) {
  memset(fb, 0, sizeof fb);
  if (rn_active) { render_rename(); return; }
  uint16_t hk[4]; int nh = held_list(hk);
  int y1 = (nh || OLED_H > 32) ? OLED_H - 8 * HELD_SC : OLED_H, ah = y1 - MAIN_Y, cap;
  char big[12];
  if (latch_wait) { strcpy(big, "LATCH ?"); cap = IDLE_SC; }
  else if (vis) { strcpy(big, lastn); cap = KEY_SC; }
  else { int a = 0; for (int l = L - 1; l > 0; l--) if (layers >> l & 1) { a = l; break; } lname_get(a, big); cap = IDLE_SC; }
  int sc = fitsc(big, cap < ah / 8 ? cap : ah / 8);
  ctxt(big, MAIN_Y + (ah - 8 * sc) / 2, sc);
  if (vis && !latch_wait) { char c[8]; c[0] = 'x'; putnum(c + 1, rcnt); txt(128 - twd(c, CNT_SC), 0, CNT_SC, c); }
  if (nh) {
    char buf[40]; int first = 0, hs = HELD_SC;
    for (;;) {
      buf[0] = 0;
      for (int i = first; i < nh; i++) { char lab[8]; kcname(hk[i], lab); if (i > first) strcat(buf, " "); strncat(buf, lab, sizeof buf - strlen(buf) - 1); }
      if (twd(buf, hs) <= 128) break;
      if (first + 1 < nh) first++; else if (hs > 1) hs--; else break;
    }
    int w = twd(buf, hs); txt(w < 128 ? 128 - w : 0, OLED_H - 8 * hs, hs, buf);
  }
}
#else  // man hinh xoay doc: LW x LH = OLED_H x 128, chu tu dong xuong dong
#define LW OLED_H
#define LH 128
#define MAIN_Y 8
static int cpl_of(int sc) { int n = (LW + sc) / (6 * sc); return n < 1 ? 1 : n; }
static int wlines(int len, int sc) { int c = cpl_of(sc); return (len + c - 1) / c; }
static void wtxt(const char *t, int y, int sc, bool center) {   // chuoi xuong dong, moi dong toi da cpl ky tu
  int len = strlen(t), cpl = cpl_of(sc), k = 0; char ln[16];
  for (int i = 0; i < len; i += cpl, k++) {
    int n = len - i < cpl ? len - i : cpl; if (n > 15) n = 15; memcpy(ln, t + i, n); ln[n] = 0;
    int w = n * 6 * sc - sc; txt(center && w < LW ? (LW - w) / 2 : 0, y + k * (8 * sc + 1), sc, ln);
  }
}
static void render_rename(void) {
  char top[12] = "L"; putnum(top + 1, rn_layer); txt(0, 0, 1, "NAME"); txt(0, 9, 1, top);
  const int ny = 22; wtxt(rn_buf, ny, 1, false);
  if (rn_len < NM) { int cpl = cpl_of(1); for (int a = 0; a < 5; a++) px((rn_len % cpl) * 6 + a, ny + (rn_len / cpl) * 9 + 7); }   // con tro
  txt(0, LH - 32, 1, "ENT"); txt(0, LH - 24, 1, "=SAVE"); txt(0, LH - 16, 1, "ESC"); txt(0, LH - 8, 1, "=QUIT");
}
static void render(bool vis) {
  memset(fb, 0, sizeof fb);
  if (rn_active) { render_rename(); return; }
  uint16_t hk[4]; int nh = held_list(hk);
  int y1 = LH - 8 * nh, ah = y1 - MAIN_Y - 1, cap;
  char big[12];
  if (latch_wait) { strcpy(big, "LATCH ?"); cap = 2; }
  else if (vis) { strcpy(big, lastn); cap = 3; }
  else { int a = 0; for (int l = L - 1; l > 0; l--) if (layers >> l & 1) { a = l; break; } lname_get(a, big); cap = 2; }
  int len = strlen(big), sc = 1;
  for (int s = cap; s >= 1; s--) { if (s > 1 && cpl_of(s) < (len < 3 ? len : 3)) continue; if (wlines(len, s) * (8 * s + 1) <= ah) { sc = s; break; } }
  wtxt(big, MAIN_Y + (ah - wlines(len, sc) * (8 * sc + 1)) / 2, sc, true);
  if (vis && !latch_wait) { char c[8]; c[0] = 'x'; putnum(c + 1, rcnt); txt(LW - ((int)strlen(c) * 6 - 1), 0, 1, c); }
  for (int i = 0; i < nh; i++) {
    char lab[8]; kcname(hk[i], lab); if ((int)strlen(lab) > cpl_of(1)) lab[cpl_of(1)] = 0;
    txt(LW - ((int)strlen(lab) * 6 - 1), y1 + i * 8, 1, lab);
  }
}
#endif
static void oled_task(uint32_t now) {
  static uint32_t lt; static bool ip;
  if (!oled_ok) return;
  if (opage < OLED_PAGES) {
    uint8_t c[] = {0, 0x21, 0, 127, 0x22, opage, opage}; uint8_t d[129]; d[0] = 0x40; memcpy(d + 1, fb + opage * 128, 128);
    i2c_write_timeout_us(i2c0, OA, c, 7, false, 5000); i2c_write_timeout_us(i2c0, OA, d, 129, false, 5000); opage++; return;
  }
  bool vis = lastn[0] && (now - ot < 1200 || held_has(rkc)); if (vis != ip) { ip = vis; odirty = true; }
  if (odirty && now - lt >= 30) { lt = now; odirty = false; render(vis); opage = 0; }
}
static void oled_init(void) {
  static const uint8_t ini[] = {0, 0xAE, 0xD5, 0x80, 0xA8, OLED_H - 1, 0xD3, 0, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, OLED_H == 32 ? 0x02 : 0x12, 0x81, 0xFF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
  i2c_init(i2c0, 400000); gpio_set_function(12, GPIO_FUNC_I2C); gpio_set_function(13, GPIO_FUNC_I2C); gpio_pull_up(12); gpio_pull_up(13);
  oled_ok = i2c_write_timeout_us(i2c0, OA, ini, sizeof ini, false, 10000) > 0; odirty = true;
}

// ---- Raw HID: [cmd, off, n, data...] cmd 1=doc 2=ghi 3=luu flash 4=mac dinh 5=info 6=doc macro 7=ghi macro (byte, n<=28, off cao o byte 31) ----
static uint8_t rep[32]; static bool rep_pending, save_req;
void tud_hid_set_report_cb(uint8_t itf, uint8_t id, hid_report_type_t t, uint8_t const *b, uint16_t sz) {
  (void)id; (void)t; if (itf != 1 || sz < 1) return;
  memset(rep, 0, 32); rep[0] = b[0];
  uint16_t off = sz >= 3 ? b[1] | (sz >= 32 ? b[31] << 8 : 0) : 0;   // offset 16 bit: byte 1 (thap) + byte 31 (cao)
  if ((b[0] == 1 || b[0] == 2) && sz >= 3 && b[2] <= 14 && off + b[2] <= NW) {
    rep[1] = b[1]; rep[2] = b[2]; rep[31] = off >> 8;
    for (int i = 0; i < b[2]; i++) {
      if (b[0] == 2 && sz >= 3 + 2 * (i + 1)) cfg.w[off + i] = b[3 + 2 * i] | b[4 + 2 * i] << 8;
      rep[3 + 2 * i] = cfg.w[off + i] & 255; rep[4 + 2 * i] = cfg.w[off + i] >> 8;
    }
    if (b[0] == 2) relayer();
  } else if ((b[0] == 6 || b[0] == 7) && sz >= 3 && b[2] <= 28 && off + b[2] <= NMAC * MSLOT) {   // 6 = doc macro, 7 = ghi macro (theo byte)
    rep[1] = b[1]; rep[2] = b[2]; rep[31] = off >> 8;
    if (b[0] == 7) { mc_stop(); mac_dirty = true; }
    for (int i = 0; i < b[2]; i++) {
      if (b[0] == 7 && sz >= 4 + i) macimg[4 + off + i] = b[3 + i];
      rep[3 + i] = macimg[4 + off + i];
    }
  } else if (b[0] == 3) save_req = true;
  else if (b[0] == 4) { defaults(); relayer(); mc_stop(); mac_defaults(); mac_dirty = true; }
  else if (b[0] == 5) { rep[1] = L; rep[2] = NW & 255; rep[3] = NW >> 8; rep[4] = FW_VER; rep[5] = NFX; rep[6] = NMAC; rep[7] = MSLOT & 255; rep[8] = MSLOT >> 8; }
  rep_pending = true;
}
uint16_t tud_hid_get_report_cb(uint8_t i, uint8_t id, hid_report_type_t t, uint8_t *b, uint16_t n) { (void)i; (void)id; (void)t; (void)b; (void)n; return 0; }

int main(void) {
  load(); mac_load();
  for (int r = 0; r < 4; r++) { gpio_init(rows[r]); gpio_set_dir(rows[r], GPIO_OUT); gpio_put(rows[r], 0); }
  for (int c = 0; c < 5; c++) { gpio_init(cols[c]); gpio_set_dir(cols[c], GPIO_IN); gpio_pull_down(cols[c]); }
  for (int e = 0; e < 3; e++) for (int i = 0; i < 2; i++) { gpio_init(encp[e][i]); gpio_set_dir(encp[e][i], GPIO_IN); gpio_pull_up(encp[e][i]); }
  tusb_init(); oled_init(); led_init(); strip_map();
  uint32_t last = 0, raw_prev = 0, cnt = 0;
  while (1) {
    tud_task();
    uint32_t now = ms();
    if (now != last) {
      last = now; uint32_t raw = scan();
      if (raw != raw_prev) { raw_prev = raw; cnt = 0; } else if (cnt < 5 && ++cnt == 5) apply(raw);
      enc_poll();
      if (pend >= 0 && now - pt >= COMBO_TERM) { fire(pend, pkc); pend = -1; relayer(); dirty = true; }
      td_task(now); mc_task(now); oled_task(now); led_task(now); strip_task(now); boot_task(now);
    }
    hid_task();
    if (rep_pending && tud_hid_n_ready(1)) { tud_hid_n_report(1, 0, rep, 32); rep_pending = false; }
    if (save_req && !rep_pending) { save_req = false; nsave_req = false; flash_safe_execute(do_save, NULL, 500); if (mac_dirty) { mac_dirty = false; flash_safe_execute(do_save_mac, NULL, 500); } }
    else if (nsave_req && !rep_pending) { nsave_req = false; flash_safe_execute(do_save, (void *)1, 500); }
  }
}
