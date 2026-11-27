/* Sys43 glyph metrics for text layout, GPL-2.0-or-later. */
#include <math.h>
#include "system4.h"
#include "system4/ain.h"
#include "system4/string.h"
#include "gfx/font.h"
#include "hll.h"

static struct {
	int face;
	int size;
	int r, g, b;
	float bold;
	float edge;
	int edge_r, edge_g, edge_b;
} tsm_state = { .size = 16 };

static void TextSurfaceManager_SetFontType(int face)
{
	tsm_state.face = face;
}

static void TextSurfaceManager_SetFontSize(int size)
{
	tsm_state.size = size;
}

static void TextSurfaceManager_SetFontColor(int r, int g, int b)
{
	tsm_state.r = r;
	tsm_state.g = g;
	tsm_state.b = b;
}

static void TextSurfaceManager_SetFontBoldWeight(float bold)
{
	tsm_state.bold = bold;
}

static void TextSurfaceManager_SetFontEdgeWeight(float edge)
{
	tsm_state.edge = edge;
}

static void TextSurfaceManager_SetFontEdgeColor(int r, int g, int b)
{
	tsm_state.edge_r = r;
	tsm_state.edge_g = g;
	tsm_state.edge_b = b;
}

static bool measure_font_width(struct string *text, int *width, int face, int size,
		float bold, float edge)
{
	*width = 0;
	if (!text || !text->size || size <= 0 || !isfinite(bold) || !isfinite(edge))
		return false;
	gfx_font_init();
	struct text_style style = { .face = face, .size = size, .scale_x = 1.f, .space_scale_x = 1.f };
	// This API measures the first character, even when given a longer string.
	// Each side reserves the larger of the bold and outline extents.
	*width = ceilf(gfx_size_char_unstyled(&style, text->text))
		+ 2 * ceilf(max(0.f, max(bold, edge)));
	return true;
}

static bool GetFontWidth(struct string *text, int *width, int face, int size,
		possibly_unused int r, possibly_unused int g, possibly_unused int b,
		float bold, float edge, possibly_unused int edge_r,
		possibly_unused int edge_g, possibly_unused int edge_b)
{
	return measure_font_width(text, width, face, size, bold, edge);
}

static bool GetFontWidth_v11(struct string *text, int *width)
{
	return measure_font_width(text, width, tsm_state.face, tsm_state.size,
			tsm_state.bold, tsm_state.edge);
}

static void TextSurfaceManager_PreLink(void);

HLL_LIBRARY(TextSurfaceManager,
	    HLL_EXPORT(_PreLink, TextSurfaceManager_PreLink),
	    HLL_EXPORT(SetFontType, TextSurfaceManager_SetFontType),
	    HLL_EXPORT(SetFontSize, TextSurfaceManager_SetFontSize),
	    HLL_EXPORT(SetFontColor, TextSurfaceManager_SetFontColor),
	    HLL_EXPORT(SetFontBoldWeight, TextSurfaceManager_SetFontBoldWeight),
	    HLL_EXPORT(SetFontEdgeWeight, TextSurfaceManager_SetFontEdgeWeight),
	    HLL_EXPORT(SetFontEdgeColor, TextSurfaceManager_SetFontEdgeColor),
	    HLL_EXPORT(GetFontWidth, GetFontWidth));

static void TextSurfaceManager_PreLink(void)
{
	int libno = ain_get_library(ain, "TextSurfaceManager");
	if (libno < 0)
		return;
	int fno = ain_get_library_function(ain, libno, "GetFontWidth");
	if (fno >= 0 && ain->libraries[libno].functions[fno].nr_arguments == 2)
		static_library_replace(&lib_TextSurfaceManager, "GetFontWidth", GetFontWidth_v11);
}
