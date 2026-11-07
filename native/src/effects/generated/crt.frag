#version 330

layout(std140) uniform EffectUniforms_std140
{
    vec2 source_size_px;
    vec2 output_size_px;
    vec4 params0;
    vec4 params1;
} u;

uniform sampler2D SPIRV_Cross_Combinedt_sources_source;

#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseVertex gl_BaseVertexARB
#else
uniform int SPIRV_Cross_BaseVertex;
#endif
in vec2 v0;
layout(location = 0) out vec4 entryPointParam_fs_main;

void main()
{
    vec4 _280;
    do
    {
        vec2 _301 = v0 - vec2(0.5);
        float _302 = dot(_301, _301);
        vec2 _306 = v0 + (_301 * ((_302 * u.params0.y) * 2.0));
        float _307 = _306.x;
        bool _281;
        if (_307 < 0.0)
        {
            _281 = true;
        }
        else
        {
            _281 = _307 > 1.0;
        }
        if (_281)
        {
            _281 = true;
        }
        else
        {
            _281 = _306.y < 0.0;
        }
        if (_281)
        {
            _281 = true;
        }
        else
        {
            _281 = _306.y > 1.0;
        }
        if (_281)
        {
            _280 = vec4(0.0);
            break;
        }
        vec4 _282 = texture(SPIRV_Cross_Combinedt_sources_source, _306);
        if (u.params1.x > 0.001000000047497451305389404296875)
        {
            vec4 _422 = _282;
            _422.x = texture(SPIRV_Cross_Combinedt_sources_source, _306 + (vec2(u.params1.x, 0.0) / u.source_size_px)).x;
            _422.z = texture(SPIRV_Cross_Combinedt_sources_source, _306 + (vec2(-u.params1.x, 0.0) / u.source_size_px)).z;
            _282 = _422;
        }
        float _347 = sin((_306.y * u.source_size_px.y) * 3.1415927410125732421875);
        float _352 = 1.0 - (u.params0.x * (0.3499999940395355224609375 + ((0.64999997615814208984375 * _347) * _347)));
        uint _355 = uint(gl_FragCoord.x) % 3u;
        vec3 _283 = vec3(1.0 - (u.params0.w * 0.449999988079071044921875));
        if (_355 == 0u)
        {
            vec3 _426 = _283;
            _426.x = 1.0;
            _283 = _426;
        }
        else
        {
            if (_355 == 1u)
            {
                vec3 _428 = _283;
                _428.y = 1.0;
                _283 = _428;
            }
            else
            {
                vec3 _430 = _283;
                _430.z = 1.0;
                _283 = _430;
            }
        }
        float _373 = max(1.0 - ((u.params0.z * _302) * 1.5), 0.0);
        _280 = vec4((((_282.xyz * _283) * u.params1.y) * _352) * _373, (_282.w * _352) * _373);
        break;
    } while(false);
    entryPointParam_fs_main = _280;
}

