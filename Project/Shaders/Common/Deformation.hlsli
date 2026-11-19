#ifndef NEXUS_DEFORMATION
#define NEXUS_DEFORMATION
struct DeformationData
{
    uint poseBuffer, jointCount, weightOffset, weightCount;
    uint morphBuffer, vertexCount, targetCount, padding;
};
float4x4 DeformationJoint(DeformationData data, uint joint)
{
    if (joint >= data.jointCount)
    {
        return float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1);
    }
    ByteAddressBuffer pose = ResourceDescriptorHeap[data.poseBuffer];
    uint offset = joint * 64;
    return float4x4(asfloat(pose.Load4(offset)), asfloat(pose.Load4(offset + 16)),
        asfloat(pose.Load4(offset + 32)), asfloat(pose.Load4(offset + 48)));
}
void ApplyMorph(DeformationData data, uint vertex, inout float3 position, inout float3 normal, inout float3 tangent)
{
    if (data.targetCount == 0 || vertex >= data.vertexCount || data.weightCount == 0)
    {
        return;
    }
    ByteAddressBuffer deltas = ResourceDescriptorHeap[data.morphBuffer];
    ByteAddressBuffer pose = ResourceDescriptorHeap[data.poseBuffer];
    for (uint target = 0; target < min(data.targetCount, data.weightCount); ++target)
    {
        float weight = asfloat(pose.Load(data.weightOffset + target * 4));
        if (weight != 0)
        {
            uint offset = (target * data.vertexCount + vertex) * 36;
            position += asfloat(deltas.Load3(offset)) * weight;
            normal += asfloat(deltas.Load3(offset + 12)) * weight;
            tangent += asfloat(deltas.Load3(offset + 24)) * weight;
        }
    }
}
#endif
