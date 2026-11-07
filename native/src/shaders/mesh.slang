// Geo3D meshes: indexed triangles, instanced. Each vertex is 11 floats
// (position, normal, texcoord, rgb); each instance is an R_Mesh3DInst (a
// transform, then the albedo uv remap). Matrices arrive as four float4
// columns, so no backend's default matrix layout matters:
// M * v = v.x*c0 + v.y*c1 + v.z*c2 + v.w*c3.

struct MeshUniforms
{
  float4 view_projection[4]; // columns
}

struct MeshGroupUniforms
{
  float has_albedo;
  float albedo_sample_is_surface; // the albedo is a view surface: premultiplied linear colour + coverage
  float2 _padding0;
}

ConstantBuffer<MeshUniforms> u;
ConstantBuffer<MeshGroupUniforms> g;
Texture2D t_albedo;
SamplerState s_albedo;

struct Vertex
{
  float3 position : POS;
  float3 normal   : NOR;
  float2 texcoord : TEX;
  float3 color    : COL;
}

struct Instance
{
  float4 xform0       : XFORM0; // columns
  float4 xform1       : XFORM1;
  float4 xform2       : XFORM2;
  float4 xform3       : XFORM3;
  float4 albedo_remap : REMAP;  // uv' = xy + uv*zw
}

struct V2P
{
  float4 position : SV_Position;
  float2 texcoord : TEXCOORD0;
  float4 color    : TEXCOORD1;
}

float4 transform(float4 c0, float4 c1, float4 c2, float4 c3, float4 v)
{
  return v.x * c0 + v.y * c1 + v.z * c2 + v.w * c3;
}

[shader("vertex")]
V2P vs_main(Vertex vtx, Instance inst)
{
  float4 world = transform(inst.xform0, inst.xform1, inst.xform2, inst.xform3, float4(vtx.position, 1.0));
  V2P out;
  out.position = transform(u.view_projection[0], u.view_projection[1], u.view_projection[2], u.view_projection[3], world);
  out.texcoord = inst.albedo_remap.xy + vtx.texcoord * inst.albedo_remap.zw;
  out.color = float4(vtx.color, 1.0);
  return out;
}

[shader("fragment")]
float4 fs_main(V2P in) : SV_Target
{
  float4 albedo = float4(1.0);
  if(g.has_albedo > 0.5)
  {
    albedo = t_albedo.Sample(s_albedo, in.texcoord);
    // un-premultiply a surface, so the pass's source-alpha blend reproduces
    // premultiplied source-over
    if(g.albedo_sample_is_surface > 0.5 && albedo.a > 0.0001)
    {
      albedo.rgb /= albedo.a;
    }
  }
  return albedo * in.color;
}
