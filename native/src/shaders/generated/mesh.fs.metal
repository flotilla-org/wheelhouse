#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 90 "core"
struct pixelOutput_0
{
    float4 output_0 [[color(0)]];
};


#line 90
struct pixelInput_0
{
    float2 texcoord_0 [[user(TEXCOORD)]];
    float4 color_0 [[user(TEXCOORD_1)]];
};


#line 12 "mesh.slang"
struct MeshGroupUniforms_0
{
    float has_albedo_0;
    float albedo_sample_is_surface_0;
    float2 _padding0_0;
};




struct KernelContext_0
{
    MeshGroupUniforms_0 constant* g_0;
    texture2d<float, access::sample> t_albedo_0;
    sampler s_albedo_0;
};


#line 65
[[fragment]] pixelOutput_0 fs_main(pixelInput_0 _S1 [[stage_in]], float4 position_0 [[position]], MeshGroupUniforms_0 constant* g_1 [[buffer(1)]], texture2d<float, access::sample> t_albedo_1 [[texture(0)]], sampler s_albedo_1 [[sampler(0)]])
{

#line 65
    thread KernelContext_0 kernelContext_0;

#line 65
    (&kernelContext_0)->g_0 = g_1;

#line 65
    (&kernelContext_0)->t_albedo_0 = t_albedo_1;

#line 65
    (&kernelContext_0)->s_albedo_0 = s_albedo_1;

    thread float4 albedo_0 = float4(1.0f) ;
    if((g_1->has_albedo_0) > 0.5f)
    {
        albedo_0 = (((&kernelContext_0)->t_albedo_0).sample(((&kernelContext_0)->s_albedo_0), (_S1.texcoord_0)));

#line 70
        bool _S2;


        if(((&kernelContext_0)->g_0->albedo_sample_is_surface_0) > 0.5f)
        {

#line 73
            _S2 = (albedo_0.w) > 0.00009999999747379f;

#line 73
        }
        else
        {

#line 73
            _S2 = false;

#line 73
        }

#line 73
        if(_S2)
        {
            albedo_0.xyz = albedo_0.xyz / float3(albedo_0.w) ;

#line 73
        }

#line 68
    }

#line 68
    pixelOutput_0 _S3 = { albedo_0 * _S1.color_0 };

#line 78
    return _S3;
}

