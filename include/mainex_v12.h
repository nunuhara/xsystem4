#ifndef XSYSTEM4_MAINEX_V12_H
#define XSYSTEM4_MAINEX_V12_H
struct ex;
// Ownership is transferred to the reader registry.
int mainex_v12_register(struct ex *ex);
int mainex_v12_register_at(int preferred_id, struct ex *ex);
void mainex_v12_unregister(int id);
void mainex_v12_unregister_activity(int id);
#endif
