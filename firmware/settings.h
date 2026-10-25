/*
 * settings.h - persistent settings, kept in the last flash sector.
 */

#ifndef BITCON_SETTINGS_H
#define BITCON_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

/* Reads the stored output mode, or DEFAULT_OUTPUT_MODE if none is stored.
 * Call once at boot, before USB starts. */
void settings_load(void);

/* The output mode this boot runs in. Fixed until the next reboot, because
 * the USB descriptors the host has already read depend on it. */
uint8_t settings_mode(void);

/* True for the gamepad-only modes: LR2, beatoraja and INFINITAS. */
bool settings_mode_is_gamepad(uint8_t mode);

/* "keyboard", "lr2", "beatoraja", "infinitas" or "composite". */
const char *settings_mode_name(uint8_t mode);

/* Parses a mode name. Returns false for anything else. */
bool settings_mode_from_name(const char *name, uint8_t *mode);

/* Stores the mode and reboots, so the host re-enumerates the new USB
 * layout. Never returns. */
void settings_save_and_reboot(uint8_t mode) __attribute__((noreturn));

#endif /* BITCON_SETTINGS_H */
