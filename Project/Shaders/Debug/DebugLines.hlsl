struct DebugLine { float4 start; float4 end; float4 color; };
cbuffer DebugDraw : register(b0) {
    row_major float4x4 viewProjection;
    DebugLine lines[1024];
};
struct Vertex { float4 position : SV_POSITION; float4 color : COLOR0; };
Vertex VSMain(uint id : SV_VertexID) {
    DebugLine segment = lines[id / 2];
    Vertex output;
    output.position = mul((id & 1) ? segment.end : segment.start, viewProjection);
    output.color = segment.color;
    return output;
}
float4 PSMain(Vertex input) : SV_TARGET { return input.color; }
