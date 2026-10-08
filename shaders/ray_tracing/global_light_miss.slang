#include "global_light.slangh"

[shader("miss")]
void main(inout RadiancePayload payload : SV_RayPayload) {
    payload.hit = 0u;
}
