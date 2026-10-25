/*
 * settings.c - persistent settings, kept in the last flash sector.
 *
 * The sector sits far past the firmware image, so flashing a new UF2 leaves
 * it alone and the stored mode survives firmware updates.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#include "config.h"
#include "settings.h"

#define SETTINGS_OFFSET   (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SETTINGS_MAGIC    0x4E435442u  /* "BTCN" */
#define SETTINGS_VERSION  1

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  mode;
    uint8_t  reserved[2];
} settings_t;

static uint8_t g_mode = DEFAULT_OUTPUT_MODE;

static bool mode_valid(uint8_t mode)
{
    return mode >= OUT_KEYBOARD && mode <= OUT_BEATORAJA;
}

void settings_load(void)
{
    /* Erased flash reads as 0xFF, which fails the magic check. */
    const settings_t *s = (const settings_t *)(XIP_BASE + SETTINGS_OFFSET);
    if (s->magic == SETTINGS_MAGIC && s->version == SETTINGS_VERSION &&
        mode_valid(s->mode)) {
        g_mode = s->mode;
    }
}

uint8_t settings_mode(void)
{
    return g_mode;
}

bool settings_mode_is_gamepad(uint8_t mode)
{
    return mode == OUT_LR2 || mode == OUT_BEATORAJA || mode == OUT_INFINITAS;
}

const char *settings_mode_name(uint8_t mode)
{
    switch (mode) {
    case OUT_KEYBOARD:  return "keyboard";
    case OUT_LR2:       return "lr2";
    case OUT_BEATORAJA: return "beatoraja";
    case OUT_INFINITAS: return "infinitas";
    case OUT_COMPOSITE: return "composite";
    default:            return "unknown";
    }
}

bool settings_mode_from_name(const char *name, uint8_t *mode)
{
    for (uint8_t m = OUT_KEYBOARD; m <= OUT_BEATORAJA; m++) {
        if (strcmp(name, settings_mode_name(m)) == 0) {
            *mode = m;
            return true;
        }
    }
    return false;
}

void settings_save_and_reboot(uint8_t mode)
{
    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));

    const settings_t s = {
        .magic   = SETTINGS_MAGIC,
        .version = SETTINGS_VERSION,
        .mode    = mode,
    };
    memcpy(page, &s, sizeof(s));

    /*
     * Both cores execute from flash, and flash cannot be read while it is
     * being erased. Core1 is held in reset rather than parked, since the
     * board reboots straight after, and core0 runs with interrupts off so
     * no handler fetches from flash mid-erase.
     */
    multicore_reset_core1();
    (void)save_and_disable_interrupts();

    flash_range_erase(SETTINGS_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_OFFSET, page, FLASH_PAGE_SIZE);

    watchdog_reboot(0, 0, 0);
    while (true) {
        tight_loop_contents();
    }
}
