#version 460 core

#define NBL_GLSL_SKINNING_CACHE_SKINNING_TRANSFORM_DESCRIPTOR_QUALIFIERS readonly restrict
#include "nbl/builtin/glsl/skinning/cache_descriptor_set.glsl"


#ifndef NBL_GLSL_SKINNING_DEBUG_DESCRIPTOR_SET
#define NBL_GLSL_SKINNING_DEBUG_DESCRIPTOR_SET 1
#endif

struct nbl_glsl_skinning_DebugData_t
{
	uint skinOffset;
	uint aabbOffset;
	uint pivotNode;
};
#ifndef NBL_GLSL_SKINNING_DEBUG_DATA_DESCRIPTOR_BINDING
#define NBL_GLSL_SKINNING_DEBUG_DATA_DESCRIPTOR_BINDING 0
#endif
#ifndef NBL_GLSL_SKINNING_DEBUG_DATA_DESCRIPTOR_QUALIFIERS
#define NBL_GLSL_SKINNING_DEBUG_DATA_DESCRIPTOR_QUALIFIERS readonly restrict
#endif
#ifndef NBL_GLSL_SKINNING_DEBUG_DATA_DESCRIPTOR_DECLARED
#define NBL_GLSL_SKINNING_DEBUG_DATA_DESCRIPTOR_DECLARED
layout(
    set=NBL_GLSL_SKINNING_DEBUG_DESCRIPTOR_SET,
    binding=NBL_GLSL_SKINNING_DEBUG_DATA_DESCRIPTOR_BINDING
) NBL_GLSL_SKINNING_DEBUG_DATA_DESCRIPTOR_QUALIFIERS buffer DebugData
{
    nbl_glsl_skinning_DebugData_t data[];
} debugData;
#endif

#ifndef NBL_GLSL_SKINNING_DEBUG_JOINT_COUNT_INCL_PREFIX_SUM_DESCRIPTOR_BINDING
#define NBL_GLSL_SKINNING_DEBUG_JOINT_COUNT_INCL_PREFIX_SUM_DESCRIPTOR_BINDING 1
#endif
#ifndef NBL_GLSL_SKINNING_DEBUG_JOINT_COUNT_INCL_PREFIX_SUM_DESCRIPTOR_QUALIFIERS
#define NBL_GLSL_SKINNING_DEBUG_JOINT_COUNT_INCL_PREFIX_SUM_DESCRIPTOR_QUALIFIERS readonly restrict
#endif
#ifndef NBL_GLSL_SKINNING_DEBUG_JOINT_COUNT_INCL_PREFIX_SUM_DESCRIPTOR_DECLARED
#define NBL_GLSL_SKINNING_DEBUG_JOINT_COUNT_INCL_PREFIX_SUM_DESCRIPTOR_DECLARED
layout(
    set=NBL_GLSL_SKINNING_DEBUG_DESCRIPTOR_SET,
    binding=NBL_GLSL_SKINNING_DEBUG_JOINT_COUNT_INCL_PREFIX_SUM_DESCRIPTOR_BINDING
) NBL_GLSL_SKINNING_DEBUG_JOINT_COUNT_INCL_PREFIX_SUM_DESCRIPTOR_QUALIFIERS buffer JointCountInclPrefixSum
{
    uint jointCountInclPrefixSum[];
};
#endif

#include "nbl/builtin/glsl/shapes/aabb.glsl"
#ifndef NBL_GLSL_SKINNING_DEBUG_AABB_DESCRIPTOR_BINDING
#define NBL_GLSL_SKINNING_DEBUG_AABB_DESCRIPTOR_BINDING 2
#endif
#ifndef NBL_GLSL_SKINNING_DEBUG_AABB_DESCRIPTOR_QUALIFIERS
#define NBL_GLSL_SKINNING_DEBUG_AABB_DESCRIPTOR_QUALIFIERS readonly restrict
#endif
#ifndef NBL_GLSL_SKINNING_DEBUG_AABB_DESCRIPTOR_DECLARED
#define NBL_GLSL_SKINNING_DEBUG_AABB_DESCRIPTOR_DECLARED
layout(
    set=NBL_GLSL_SKINNING_DEBUG_DESCRIPTOR_SET,
    binding=NBL_GLSL_SKINNING_DEBUG_AABB_DESCRIPTOR_BINDING
) NBL_GLSL_SKINNING_DEBUG_AABB_DESCRIPTOR_QUALIFIERS buffer DebugAABB
{
    nbl_glsl_shapes_CompressedAABB_t data[];
} debugAABB;
#endif


layout( push_constant, row_major ) uniform PushConstants
{
    mat4 viewProj;
	vec4 lineColor;
	vec3 aabbColor;
    uint skinCount;
} pc;


layout(location = 0) out vec3 outColor;

#include <nbl/builtin/glsl/algorithm.glsl>
NBL_GLSL_DEFINE_UPPER_BOUND(jointCountInclPrefixSum,uint)


#include "nbl/builtin/glsl/utils/transform.glsl"
void main()
{
    const uint skinInstanceID = upper_bound_jointCountInclPrefixSum_NBL_GLSL_LESS(0u,pc.skinCount,gl_InstanceIndex);
	const nbl_glsl_skinning_DebugData_t dd = debugData.data[skinInstanceID];
    
    uint jointID = gl_InstanceIndex;
    if (bool(skinInstanceID))
        jointID -= jointCountInclPrefixSum[skinInstanceID-1u];

    // TODO: draw the parent hierarchy too
	const nbl_glsl_shapes_AABB_t aabb = nbl_glsl_shapes_CompressedAABB_t_decompress(debugAABB.data[dd.aabbOffset+jointID]);

	const bvec3 mask = bvec3(gl_VertexIndex&0x1u,gl_VertexIndex&0x2u,gl_VertexIndex&0x4u);
	const vec3 pos = nbl_glsl_pseudoMul3x4with3x1(
        /*nbl_glsl_pseudoMul4x3with4x3(*/nodeGlobalTransforms.data[dd.pivotNode],//skinningTransforms.data[dd.skinOffset+jointID]),
        mix(aabb.minVx,aabb.maxVx,mask)
    );

	outColor = pc.aabbColor;
	gl_Position = nbl_glsl_pseudoMul4x4with3x1(pc.viewProj,pos);

}