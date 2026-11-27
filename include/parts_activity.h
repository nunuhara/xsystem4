/* Native PACT activity support, GPL-2.0-or-later. */
#ifndef XSYSTEM4_PARTS_ACTIVITY_H
#define XSYSTEM4_PARTS_ACTIVITY_H
#include <stdbool.h>
struct string;
struct page;
void PE_ReleaseActivities(void);
bool PE_IsExistActivity(struct string *name);
bool PE_CreateActivity(struct string *name);
bool PE_ReleaseActivity(struct string *name, struct page **erased);
bool PE_ReadActivityFile(struct string *name, struct string *file, bool edit);
bool PE_IsExistActivityFile(struct string *file);
bool PE_AddActivityParts(struct string *name, struct string *part_name, int no);
bool PE_RemoveActivityParts(struct string *name, struct string *part_name);
bool PE_RemoveAllActivityParts(struct string *name);
int PE_NumofActivityParts(struct string *name);
bool PE_GetActivityParts(int index, struct string *name, struct string **part_name, int *no);
bool PE_IsExistActivityPartsByName(struct string *name, struct string *part_name);
bool PE_IsExistActivityPartsByNumber(struct string *name, int no);
int PE_GetActivityPartsNumber(struct string *name, struct string *part_name);
struct string *PE_GetActivityPartsName(struct string *name, int no);
int PE_GetActivityEXID(struct string *name);
void PE_SetActivityEXText(struct string *name, struct string *text);
struct string *PE_GetActivityEXText(struct string *name);
void PE_SetActivityBG(struct string *name, struct string *cg);
struct string *PE_GetActivityBG(struct string *name);
void PE_AddActivityCloseParts(struct string *name, struct string *part_name);
void PE_RemoveActivityCloseParts(struct string *name, struct string *part_name);
void PE_RemoveAllActivityCloseParts(struct string *name);
bool PE_IsExistActivityCloseParts(struct string *name, struct string *part_name);
void PE_AddActivityLockedParts(struct string *name, struct string *part_name);
bool PE_IsExistActivityLockedParts(struct string *name, struct string *part_name);
void PE_SetActivityIntentData(struct string *name, struct string *part_name, struct string *dest, int type);
void PE_AddActivityIntentDataDestination(struct string *name, struct string *part_name, struct string *dest);
bool PE_IsExistActivityIntentData(struct string *name, struct string *part_name);
int PE_NumofActivityIntentData(struct string *name);
int PE_NumofActivityIntentDataDestination(struct string *name, struct string *part_name);
struct string *PE_GetActivityIntentDataSource(struct string *name, int index);
struct string *PE_GetActivityIntentDataDestination(int index, struct string *name, struct string *part_name);
int PE_GetActivityIntentDataType(struct string *name, struct string *part_name);
void PE_SetActivityFunc(struct string *name, struct string *part_name, int state,
		struct string *fmap_id, struct string *func_name);
struct string *PE_GetActivityFunc(struct string *name, struct string *part_name, int state, struct string *fmap_id);
int PE_NumofActivityFunc(struct string *name, struct string *part_name, int state);
void PE_GetActivityFuncPair(int index, struct string *name, struct string *part_name, int state,
		struct string **fmap_id, struct string **func_name);
bool PE_CrateActivityBinary(struct string *name, struct page **data);
bool PE_ReadActivityBinary(struct string *name, struct page **data);
void PE_SetActivityEndKey(struct string *name, int key);
void PE_EraseActivityEndKey(struct string *name, int key);
bool PE_IsExistActivityEndKey(struct string *name, int key);
int PE_NumofActivityEndKey(struct string *name);
int PE_GetActivityEndKey(struct string *name, int index);
void PE_SetUserComponentName(int no, struct string *name);
void PE_SetSpecialComponentType(int no, int type);
int PE_GetSpecialComponentType(int no);
struct string *PE_GetUserComponentName(int no);
void PE_SetUserComponentData(int no, struct string *key, struct string *value);
struct string *PE_GetUserComponentData(int no, struct string *key);
struct iarray_writer;
struct iarray_reader;
void parts_activities_save(struct iarray_writer *w);
void parts_activities_load(struct iarray_reader *r);
void parts_activities_reconcile_legacy(void);
#endif
