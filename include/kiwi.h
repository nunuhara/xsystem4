#ifndef XSYSTEM4_KIWI_H
#define XSYSTEM4_KIWI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct string;
bool kiwi_play_system_sound(struct string *name);
void kiwi_stop_non_system_sound(void);
bool kiwi_prepare_from_memory(int id, uint8_t *buf, size_t size, int group);
bool kiwi_play_sound(int id);
bool kiwi_stop_sound(int id);
bool kiwi_unprepare_sound(int id);
bool kiwi_is_playing_sound(int id);
int kiwi_get_sound_pos(int id);
bool kiwi_seek_sound(int id, int milliseconds);

#endif
