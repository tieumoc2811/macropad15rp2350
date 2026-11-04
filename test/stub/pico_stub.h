// Khung gia cho Pico SDK / TinyUSB: chi du de bien dich main.c tren may tinh (gcc) va chay mo phong END-TO-END (xem ../fw_sim.c)
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef unsigned int uint;
extern uint8_t fake_flash[];
#define XIP_BASE ((uintptr_t)fake_flash)
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#define FLASH_SECTOR_SIZE 4096
#define __no_inline_not_in_flash_func(f) f
#define GPIO_OUT 1
#define GPIO_IN 0
#define GPIO_FUNC_I2C 3
typedef struct { int dummy; } i2c_inst_t; extern i2c_inst_t *i2c0;
typedef struct { int dummy; } PIO_t; typedef PIO_t *PIO; extern PIO pio0;
typedef struct { int dummy; } pio_sm_config; typedef struct { int dummy; } pio_program_t; extern pio_program_t ws2812_program;
#define PIO_FIFO_JOIN_TX 1
#define clk_sys 0
#define ws2812_T1 2
#define ws2812_T2 5
#define ws2812_T3 3
static inline pio_sm_config ws2812_program_get_default_config(uint o) { (void)o; pio_sm_config c = {0}; return c; }
static inline uint pio_add_program(PIO p, const pio_program_t *g) { (void)p; (void)g; return 0; }
static inline void sm_config_set_sideset_pins(pio_sm_config *c, uint p) { (void)c; (void)p; }
static inline void sm_config_set_out_shift(pio_sm_config *c, bool a, bool b, uint n) { (void)c; (void)a; (void)b; (void)n; }
static inline void sm_config_set_fifo_join(pio_sm_config *c, int j) { (void)c; (void)j; }
static inline void sm_config_set_clkdiv(pio_sm_config *c, float d) { (void)c; (void)d; }
static inline uint32_t clock_get_hz(int c) { (void)c; return 150000000; }
static inline void pio_gpio_init(PIO p, uint pin) { (void)p; (void)pin; }
static inline void pio_sm_set_consecutive_pindirs(PIO p, uint sm, uint pin, uint n, bool o) { (void)p; (void)sm; (void)pin; (void)n; (void)o; }
static inline void pio_sm_init(PIO p, uint sm, uint off, const pio_sm_config *c) { (void)p; (void)sm; (void)off; (void)c; }
static inline void pio_sm_set_enabled(PIO p, uint sm, bool e) { (void)p; (void)sm; (void)e; }
static inline void pio_sm_put_blocking(PIO p, uint sm, uint32_t v) { (void)p; (void)sm; (void)v; }
static inline void gpio_init(uint g) { (void)g; } static inline void gpio_set_dir(uint g, bool o) { (void)g; (void)o; } static inline void gpio_put(uint g, bool v) { (void)g; (void)v; }
static inline bool gpio_get(uint g) { (void)g; return false; } static inline void gpio_pull_up(uint g) { (void)g; } static inline void gpio_pull_down(uint g) { (void)g; } static inline void gpio_set_function(uint g, int f) { (void)g; (void)f; }
static inline void busy_wait_us(uint32_t u) { (void)u; }
extern uint32_t fake_ms; static inline uint32_t time_us_32(void) { return fake_ms * 1000u; }
static inline int i2c_write_timeout_us(i2c_inst_t *i, uint8_t a, const uint8_t *s, size_t n, bool nostop, uint t) { (void)i; (void)a; (void)s; (void)nostop; (void)t; return (int)n; }
static inline void i2c_init(i2c_inst_t *i, uint b) { (void)i; (void)b; }
static inline uint32_t save_and_disable_interrupts(void) { return 0; } static inline void restore_interrupts(uint32_t f) { (void)f; }
static inline void reset_usb_boot(uint32_t a, uint32_t b) { (void)a; (void)b; }
typedef struct { uint32_t ctrl; } io_qspi_t; typedef struct { io_qspi_t io[8]; } ioqspi_hw_t; extern ioqspi_hw_t *ioqspi_hw;
typedef struct { uint32_t gpio_hi_in; } sio_hw_t; extern sio_hw_t *sio_hw;
#define GPIO_OVERRIDE_LOW 2
#define GPIO_OVERRIDE_NORMAL 0
#define IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB 12
#define IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS 0x3000
#define SIO_GPIO_HI_IN_QSPI_CSN_BITS 2
static inline void hw_write_masked(uint32_t *a, uint32_t v, uint32_t m) { *a = (*a & ~m) | (v & m); }
void flash_range_erase(uint32_t off, size_t n); void flash_range_program(uint32_t off, const uint8_t *d, size_t n);
static inline int flash_safe_execute(void (*f)(void *), void *p, uint32_t t) { (void)t; f(p); return 0; }
typedef int hid_report_type_t;
bool tud_hid_n_ready(uint8_t i); bool tud_hid_n_keyboard_report(uint8_t i, uint8_t id, uint8_t mod, const uint8_t k[6]); bool tud_hid_n_report(uint8_t i, uint8_t id, const void *r, uint16_t n);
static inline void tusb_init(void) {} static inline void tud_task(void) {}
