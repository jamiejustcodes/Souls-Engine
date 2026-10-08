cbuffer Frame : register(b0) {
 column_major float4x4 ViewProjection;
 float4 CameraPosition, CameraRight, CameraUp, CameraForward;
 float4 LightDirection, LightColor, Options;
 float4 GridOrigin,GridU,GridV,GridNormal,SkyColor;
};
#ifdef SPIRV
struct DrawData { column_major float4x4 Model; float4 BaseColor,Flags; };
[[vk::push_constant]] ConstantBuffer<DrawData> Push;
#define Model Push.Model
#define BaseColor Push.BaseColor
#define Flags Push.Flags
#else
cbuffer Draw : register(b1) {
 column_major float4x4 Model;
 float4 BaseColor, Flags;
};
#endif
struct VertexInput { float3 position:POSITION;float3 normal:NORMAL; };
struct Surface { float4 position:SV_Position;float3 world:TEXCOORD0;float3 normal:TEXCOORD1; };
Surface VSMain(VertexInput v) {
 Surface o;float4 w=mul(Model,float4(v.position,1));o.world=w.xyz;o.position=mul(ViewProjection,w);
 float3 x=Model._m00_m10_m20,y=Model._m01_m11_m21,z=Model._m02_m12_m22;
 o.normal=normalize(x*v.normal.x/dot(x,x)+y*v.normal.y/dot(y,y)+z*v.normal.z/dot(z,z));return o;
}
float4 PSMain(Surface s):SV_Target {
 float3 n=normalize(s.normal),l=normalize(-LightDirection.xyz),v=normalize(CameraPosition.xyz-s.world);
 float diffuse=saturate(dot(n,l));float specular=pow(saturate(dot(n,normalize(l+v))),48)*0.28;
 float3 color=BaseColor.rgb*(0.19+diffuse*LightColor.rgb)+specular*LightColor.rgb;
 if(Options.w<0.5)color=BaseColor.rgb;
 if(Flags.x>0.5){float rim=pow(1-saturate(dot(n,v)),3);color=lerp(color,float3(1,0.53,0.09),0.15+rim*0.75);}
 color=color/(color+0.7);return float4(pow(color,1/2.2),1);
}
struct GridSurface { float4 position:SV_Position;float2 uv:TEXCOORD0; };
GridSurface GridVS(uint id:SV_VertexID) { GridSurface o;o.uv=float2((id<<1)&2,id&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o; }
struct GridOutput { float4 color:SV_Target;float depth:SV_Depth; };
GridOutput GridPS(GridSurface s) {
 GridOutput o;float3 ray=normalize(CameraForward.xyz+CameraRight.xyz*((s.uv.x*2-1)*Options.x*Options.y)+CameraUp.xyz*((1-s.uv.y*2)*Options.y));
 float horizon=saturate(ray.z*0.7+0.45);o.color=float4(lerp(float3(0.29,0.32,0.38),SkyColor.rgb,horizon),1);o.depth=1;if(SkyColor.a<0.5)o.color=float4(0.08,0.09,0.11,1);
 float denom=dot(ray,GridNormal.xyz);float t=dot(GridOrigin.xyz-CameraPosition.xyz,GridNormal.xyz)/denom;if(abs(denom)<0.0001||t<=0||t>950||Options.z<0.5)return o;
 float3 p=CameraPosition.xyz+ray*t;float2 uv=float2(dot(p-GridOrigin.xyz,GridU.xyz),dot(p-GridOrigin.xyz,GridV.xyz));float2 derivatives=max(fwidth(uv),0.0001);
 float2 lines=abs(frac(uv-0.5)-0.5)/derivatives;float minor=1-saturate(min(lines.x,lines.y));
 float2 majorLines=abs(frac(uv/10-0.5)-0.5)/(derivatives/10);float major=1-saturate(min(majorLines.x,majorLines.y));
 float checker=fmod(floor(uv.x/2)+floor(uv.y/2),2)==0?0.155:0.19;
 float3 floorColor=lerp(checker.xxx,float3(0.3,0.32,0.35),minor*0.55);floorColor=lerp(floorColor,float3(0.4,0.43,0.48),major*0.65);
 floorColor=lerp(floorColor,float3(0.64,0.16,0.12),1-saturate(abs(uv.y)/derivatives.y));
 floorColor=lerp(floorColor,float3(0.15,0.48,0.22),1-saturate(abs(uv.x)/derivatives.x));
 float fog=1-exp(-t*0.018);o.color=float4(lerp(floorColor,o.color.rgb,fog),1);float4 clip=mul(ViewProjection,float4(p,1));o.depth=clip.z/clip.w;return o;
}
