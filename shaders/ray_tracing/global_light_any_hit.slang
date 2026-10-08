#include "global_light.slangh"

[shader("anyhit")]
void main(inout RadiancePayload payload : SV_RayPayload, in HitAttributes attributes : SV_IntersectionAttributes) {
    RayInstance instance = instances[InstanceID()];
    Material material = materials[instance.material_index];
    if (material.flags.x == 0u) return;
    float2 uv = hit_texcoord(instance, PrimitiveIndex(), attributes.barycentrics);
    float alpha = textures[NonUniformResourceIndex(material.base_color_texture_index)].SampleLevel(material_sampler, uv, 0).a;
    if (alpha < material.metallic_roughness_occlusion_alpha_cutoff.w) IgnoreHit();
}
