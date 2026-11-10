/* Save memory: the serial EEPROM (512 B or 8 KB) that games reach through DMA3 at 0x0D000000, one bit per halfword.
     write: "1 0", the address (6 or 14 bits, MSB first), 64 data bits, a stop bit  -> 73 or 81 halfwords
     read:  "1 1", the address, a stop bit -> 9 or 17 halfwords; then 68 halfwords come back: 4 dummy bits and the 64 data bits.
   The size follows from the length of the first request. The file (`<rom>.sav`) is rewritten after every write. */
#include "gba_sys.h"
#include <stdio.h>
#include <string.h>

static uint8_t eeprom[8192];
static u32 eeprom_size; /* 0 until the first request, then 512 or 8192 */
static char save_path[512];
static u32 read_block, read_pos; /* the block selected by the last read request, and how many bits were read */
static int dirty_error;

void save_init(const char *path) {
    memset(eeprom, 0xFF, sizeof eeprom);
    snprintf(save_path, sizeof save_path, "%s", path);
    FILE *f = fopen(save_path, "rb");
    if (!f) return;
    size_t n = fread(eeprom, 1, sizeof eeprom, f);
    fclose(f);
    if (n == 512 || n == 8192) eeprom_size = (u32)n;
}

static void flush(void) {
    if (!save_path[0] || !eeprom_size) return;
    FILE *f = fopen(save_path, "wb");
    if (!f) { if (!dirty_error) fprintf(stderr, "cannot write %s\n", save_path); dirty_error = 1; return; }
    fwrite(eeprom, 1, eeprom_size, f);
    fclose(f);
}

/* `n` bits (one per halfword) written by a DMA to the EEPROM area */
void eeprom_write_bits(const uint8_t *bits, u32 n) {
    if (n < 9 || bits[0] != 1) return;
    u32 abits;
    int is_write;
    if ((n == 9 || n == 17) && bits[1] == 1) { abits = n - 3; is_write = 0; }
    else if ((n == 73 || n == 81) && bits[1] == 0) { abits = n - 67; is_write = 1; }
    else return;
    if (!eeprom_size) eeprom_size = abits == 6 ? 512 : 8192;
    u32 addr = 0;
    for (u32 i = 0; i < abits; i++) addr = (addr << 1) | bits[2 + i];
    if (addr * 8 + 8 > sizeof eeprom) return;
    if (!is_write) { read_block = addr; read_pos = 0; return; }
    for (int b = 0; b < 8; b++) {
        uint8_t v = 0;
        for (int k = 0; k < 8; k++) v = (uint8_t)((v << 1) | bits[2 + abits + b * 8 + k]);
        eeprom[addr * 8 + b] = v;
    }
    flush();
}

/* the next bit that a DMA read from the EEPROM area gets */
u32 eeprom_read_bit(void) {
    u32 p = read_pos++;
    if (p < 4) return 0;
    p -= 4;
    if (p >= 64) return 1;
    return (eeprom[read_block * 8 + p / 8] >> (7 - p % 8)) & 1;
}
