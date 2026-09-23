struct DirectLightVertexInput
{
    uint vertex_id : SV_VertexID;
};

struct DirectLightVertexOutput
{
    float4 position : SV_Position;
};

DirectLightVertexOutput main(DirectLightVertexInput input)
{
    uint vertex_id = input.vertex_id;
    DirectLightVertexOutput output;

    float2 p = float2(
        float((vertex_id << 1) & 2),
        float(vertex_id & 2)
    );

    output.position = float4(p * 2.0 - 1.0, 0.0, 1.0);
    return output;
}
