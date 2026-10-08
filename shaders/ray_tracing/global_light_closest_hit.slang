#include "global_light.slangh"

[shader("closesthit")]
void main(inout RadiancePayload payload : SV_RayPayload, in HitAttributes attributes : SV_IntersectionAttributes) {
    payload.hit = 1u;
    payload.instance_index = InstanceID();
    payload.primitive_index = PrimitiveIndex();
    payload.barycentrics = attributes.barycentrics;
    payload.distance = RayTCurrent();
}
