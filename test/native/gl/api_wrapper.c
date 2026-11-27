/* Exercise the actual HLL setters without booting a game's VM. The component
 * lookup is wrapped by the fixture; rendering/transforms/dirtying are real. */
#include <math.h>
#include "../../../src/hll/PartsEngine.c"
void test_set_reverse(bool lr, bool tb)
{
    PartsEngine_SetComponentReverseLR(1, lr);
    PartsEngine_SetComponentReverseTB(1, tb);
}
