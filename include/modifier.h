#ifndef SYSTEM4_MODIFIER_H
#define SYSTEM4_MODIFIER_H

#include <stdbool.h>

union vm_value;

void modifier_on_ain_init(void);
void modifier_end_session(void);
void modifier_on_frame(void);
void modifier_on_config_loaded(void);
void modifier_on_globals_loaded(void);
void modifier_before_method(void);
void modifier_after_method_return(void);
bool modifier_keep_dec_item(union vm_value *value);

#endif /* SYSTEM4_MODIFIER_H */
