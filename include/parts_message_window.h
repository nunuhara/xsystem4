/* Native Parts message windows, GPL-2.0-or-later. */
#ifndef XSYSTEM4_PARTS_MESSAGE_WINDOW_H
#define XSYSTEM4_PARTS_MESSAGE_WINDOW_H
#include <stdbool.h>
struct string;
void PE_SetMessageWindowActive(int no, bool active);
void PE_SetMessageWindowInactiveMultipleColor(int no, int r, int g, int b);
int PE_GetMessageWindowInactiveMultipleColorR(int no);
int PE_GetMessageWindowInactiveMultipleColorG(int no);
int PE_GetMessageWindowInactiveMultipleColorB(int no);
void PE_SetMessageWindowCGName(int no, struct string *name);
struct string *PE_GetMessageWindowCGName(int no);
void PE_SetMessageWindowFlatName(int no, struct string *name);
struct string *PE_GetMessageWindowFlatName(int no);
void PE_SetMessageWindowFlatShowWaitFrameNumber(int no, int frame);
int PE_GetMessageWindowFlatShowWaitFrameNumber(int no);
bool PE_IsOverMessageWindowFlatShowWaitFrame(int no);
bool PE_BackMessageWindowFlatBeginFrame(int no);
bool PE_StepMessageWindowFlatFinalFrame(int no);
void PE_SetKeyWaitCGName(int no, struct string *name, int start, int count, int time);
void PE_GetKeyWaitCGName(int no, struct string **name, int *start, int *count, int *time);
void PE_SetKeyWaitFlatName(int no, struct string *name);
struct string *PE_GetKeyWaitFlatName(int no);
void PE_SetKeyWaitPos(int no, int x, int y, int z);
int PE_GetKeyWaitPosX(int no);
int PE_GetKeyWaitPosY(int no);
int PE_GetKeyWaitPosZ(int no);
void PE_SetKeyWaitShow(int no, bool show);
bool PE_IsKeyWaitShow(int no);
void PE_SetMessageWindowText(int no, struct string *text, int message,
		struct string *function, int version, int step);
struct string *PE_GetMessageWindowText(int no);
void PE_FixMessageWindowText(int no);
bool PE_IsFixedMessageWindowText(int no);
void PE_SetMessageWindowTextArea(int no, int x, int y, int width, int height);
void PE_GetMessageWindowTextArea(int no, int *x, int *y, int *width, int *height);
void PE_SetMessageWindowTextOriginPosMode(int no, int mode);
int PE_GetMessageWindowTextOriginPosMode(int no);
void PE_SetMessageWindowTextFont(int no, int face, int size, int r, int g, int b,
		float bold, int edge_r, int edge_g, int edge_b, float edge);
void PE_GetMessageWindowTextFont(int no, int *face, int *size, int *r, int *g, int *b,
		float *bold, int *edge_r, int *edge_g, int *edge_b, float *edge);
void PE_SetMessageWindowTextSpeed(int no, int speed);
int PE_GetMessageWindowTextSpeed(int no);
void PE_SetMessageWindowTextSpace(int no, int letter, int line);
void PE_GetMessageWindowTextSpace(int no, int *letter, int *line);
void PE_SetMessageWindowRubyFont(int no, int face, int size, int r, int g, int b,
		float bold, int edge_r, int edge_g, int edge_b, float edge);
void PE_GetMessageWindowRubyFont(int no, int *face, int *size, int *r, int *g, int *b,
		float *bold, int *edge_r, int *edge_g, int *edge_b, float *edge);
void PE_SetMessageWindowRubyCharSpace(int no, int space);
int PE_GetMessageWindowRubyCharSpace(int no);
void PE_SetMessageWindowRubyLineSpace(int no, int space);
int PE_GetMessageWindowRubyLineSpace(int no);
#endif
