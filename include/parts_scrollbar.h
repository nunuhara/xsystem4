/* Native Parts scrollbars, GPL-2.0-or-later. */
#ifndef XSYSTEM4_PARTS_SCROLLBAR_H
#define XSYSTEM4_PARTS_SCROLLBAR_H
struct string;
#define SCROLLBAR_API(axis) \
void PE_Set##axis##ScrollbarSize(int no, int width, int height); \
void PE_Set##axis##ScrollbarTotalSize(int no, int size); \
void PE_Set##axis##ScrollbarViewSize(int no, int size); \
void PE_Set##axis##ScrollbarScrollPos(int no, int pos); \
void PE_Set##axis##ScrollbarScrollRate(int no, float rate); \
void PE_Set##axis##ScrollbarMoveSizeByButton(int no, int size); \
int PE_Get##axis##ScrollbarTotalSize(int no); \
int PE_Get##axis##ScrollbarViewSize(int no); \
int PE_Get##axis##ScrollbarScrollPos(int no); \
float PE_Get##axis##ScrollbarScrollRate(int no); \
int PE_Get##axis##ScrollbarMoveSizeByButton(int no); \
void PE_Set##axis##ScrollTotalSizeLinkNumber(int no, int target); \
void PE_Set##axis##ScrollViewSizeLinkNumber(int no, int target); \
int PE_Get##axis##ScrollTotalSizeLinkNumber(int no); \
int PE_Get##axis##ScrollViewSizeLinkNumber(int no);
SCROLLBAR_API(V)
SCROLLBAR_API(H)
#undef SCROLLBAR_API
#define SCROLLBAR_END_API(axis, end) \
void PE_Set##axis##Scrollbar##end(int no, int size); \
int PE_Get##axis##Scrollbar##end(int no);
SCROLLBAR_END_API(V, UpHeight)
SCROLLBAR_END_API(V, DownHeight)
SCROLLBAR_END_API(H, LeftWidth)
SCROLLBAR_END_API(H, RightWidth)
#undef SCROLLBAR_END_API
#define SCROLLBAR_NAME_API(axis, part, kind) \
void PE_Set##axis##Scrollbar##part##kind##Name(int no, struct string *name); \
struct string *PE_Get##axis##Scrollbar##part##kind##Name(int no);
#define SCROLLBAR_NAMES(axis, first, last, kind) \
SCROLLBAR_NAME_API(axis, , kind) \
SCROLLBAR_NAME_API(axis, Override##first, kind) \
SCROLLBAR_NAME_API(axis, Override##last, kind) \
SCROLLBAR_NAME_API(axis, OverrideBar, kind) \
SCROLLBAR_NAME_API(axis, OverrideBG, kind)
SCROLLBAR_NAMES(V, Up, Down, CG)
SCROLLBAR_NAMES(V, Up, Down, Flat)
SCROLLBAR_NAMES(H, Left, Right, CG)
SCROLLBAR_NAMES(H, Left, Right, Flat)
#undef SCROLLBAR_NAMES
#undef SCROLLBAR_NAME_API
void PE_SetComponentScrollPosXLinkNumber(int no, int target);
void PE_SetComponentScrollPosYLinkNumber(int no, int target);
void PE_SetComponentScrollAlphaLinkNumber(int no, int target);
int PE_GetComponentScrollPosXLinkNumber(int no);
int PE_GetComponentScrollPosYLinkNumber(int no);
int PE_GetComponentScrollAlphaLinkNumber(int no);
#define SLIDERBAR_API(axis) \
void PE_Set##axis##SliderBarSize(int no, int width, int height); \
void PE_Set##axis##SliderBarTotalSize(int no, int size); \
void PE_Set##axis##SliderBarViewSize(int no, int size); \
void PE_Set##axis##SliderBarScrollPos(int no, int pos); \
void PE_Set##axis##SliderBarScrollRate(int no, float rate); \
int PE_Get##axis##SliderBarTotalSize(int no); \
int PE_Get##axis##SliderBarViewSize(int no); \
int PE_Get##axis##SliderBarScrollPos(int no); \
float PE_Get##axis##SliderBarScrollRate(int no); \
void PE_Set##axis##SliderTotalSizeLinkNumber(int no, int target); \
void PE_Set##axis##SliderViewSizeLinkNumber(int no, int target); \
int PE_Get##axis##SliderTotalSizeLinkNumber(int no); \
int PE_Get##axis##SliderViewSizeLinkNumber(int no); \
void PE_Set##axis##SliderBarCGName(int no, struct string *name); \
void PE_Set##axis##SliderBarOverrideBarCGName(int no, struct string *name); \
void PE_Set##axis##SliderBarOverrideBGCGName(int no, struct string *name); \
struct string *PE_Get##axis##SliderBarCGName(int no); \
struct string *PE_Get##axis##SliderBarOverrideBarCGName(int no); \
struct string *PE_Get##axis##SliderBarOverrideBGCGName(int no); \
void PE_Set##axis##SliderBarFlatName(int no, struct string *name); \
void PE_Set##axis##SliderBarOverrideBarFlatName(int no, struct string *name); \
void PE_Set##axis##SliderBarOverrideBGFlatName(int no, struct string *name); \
struct string *PE_Get##axis##SliderBarFlatName(int no); \
struct string *PE_Get##axis##SliderBarOverrideBarFlatName(int no); \
struct string *PE_Get##axis##SliderBarOverrideBGFlatName(int no);
SLIDERBAR_API(V)
SLIDERBAR_API(H)
#undef SLIDERBAR_API
#endif
