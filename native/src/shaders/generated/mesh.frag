#version 330

struct _Array_std140_vector_float_4_4
{
    vec4 data[4];
};

layout(std140) uniform MeshGroupUniforms_std140
{
    float has_albedo;
    float albedo_sample_is_surface;
    vec2 _padding0;
} g;

uniform sampler2D SPIRV_Cross_Combinedt_albedos_albedo;

in vec2 v0;
in vec4 v1;
layout(location = 0) out vec4 entryPointParam_fs_main;

void main()
{
    vec4 albedo = vec4(1.0);
    if (g.has_albedo > 0.5)
    {
        albedo = texture(SPIRV_Cross_Combinedt_albedos_albedo, v0);
        bool _111;
        if (g.albedo_sample_is_surface > 0.5)
        {
            _111 = albedo.w > 9.9999997473787516355514526367188e-05;
        }
        else
        {
            _111 = false;
        }
        if (_111)
        {
            vec4 _160 = albedo;
            vec3 _165 = _160.xyz / vec3(_160.w);
            vec4 _217 = _160;
            _217.x = _165.x;
            _217.y = _165.y;
            _217.z = _165.z;
            albedo = _217;
        }
    }
    entryPointParam_fs_main = albedo * v1;
}

