#ifndef XSYSTEM4_MESSAGE_LAYOUT_H
#define XSYSTEM4_MESSAGE_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>

struct ain;

struct message_layout_cell {
	int code, source_y, indent, width, height, line_height, space;
	int x, y;
};

bool message_layout_reflow(struct message_layout_cell *cells, size_t count,
		int width, int height, int line_space);
bool message_layout_init_rance03(struct ain *ain);
void message_layout_fit_rance03(int view_slot);

#endif
