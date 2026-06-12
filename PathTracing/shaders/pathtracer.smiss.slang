// pathtracer.smiss.slang
// Shadow miss shader — sets visibility to 1 (not shadowed) when no geometry is hit.

#include "pathtracer_common.slang"

[shader("miss")]
void shadowMiss(inout Payload payload)
{
    payload.visibility = 1.0;
}
