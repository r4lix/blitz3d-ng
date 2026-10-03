precision highp float;
precision highp int;

uniform mat4 bbProjMatrix;
uniform mat4 bbViewMatrix;
uniform mat4 bbWorldMatrix;

uniform sampler2D bbTexture[8];
uniform samplerCube bbTextureCube[8];

#define FOG_NONE   0
#define FOG_LINEAR 1

struct BBLightData {
  mat4 TForm;     // light -> world
  vec4 Color;
  vec4 Params;    // type (1 distant, 2 point, 3 spot), range, cos(outer/2), cos(inner/2)
} ;

layout(std140) uniform BBLightState {
  BBLightData Light[8];

  int LightsUsed;
} LS;

// must be mindful of alignment when ordering...
struct BBTextureState {
  mat4 TForm;
  int Blend,SphereMap,Flags,CubeMap;
} ;

layout(std140) uniform BBRenderState {
  vec4 Ambient;
  vec4 BrushColor;
  vec4 FogColor;

  BBTextureState Texture[8];

  vec2 FogRange;

  int TexturesUsed;
  int UseVertexColor;
  float BrushShininess;
  int FullBright;
  int FogMode;
  int AlphaTest;
} RS;

#ifdef VERTEX
#define varying out
#else
#define varying in
#endif

varying vec3 bbVertex_Position;
varying vec4 bbVertex_Color;
varying vec3 bbVertex_Normal;
varying vec2 bbVertex_TexCoord[8];
varying float bbVertex_FogFactor;

mat4 rotationMatrix(vec3 axis, float angle){
  axis = normalize(axis);
  float s = sin(angle);
  float c = cos(angle);
  float oc = 1.0 - c;

  return mat4(oc * axis.x * axis.x + c,           oc * axis.x * axis.y - axis.z * s,  oc * axis.z * axis.x + axis.y * s,  0.0,
              oc * axis.x * axis.y + axis.z * s,  oc * axis.y * axis.y + c,           oc * axis.y * axis.z - axis.x * s,  0.0,
              oc * axis.z * axis.x - axis.y * s,  oc * axis.y * axis.z + axis.x * s,  oc * axis.z * axis.z + c,           0.0,
              0.0,                                0.0,                                0.0,                                1.0);
}

float fogFactorLinear(
  const float dist,
  const float start,
  const float end
) {
  return 1.0 - clamp((end - dist) / (end - start), 0.0, 1.0);
}

vec2 sphereMap(in vec3 normal, in vec3 ecPosition3){
  float m;
  vec3 r, u;
  u = normalize(ecPosition3);
  r = reflect(u, normal);
  m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
  return vec2 (r.x / m + 0.5, r.y / m + 0.5);
}




#ifdef VERTEX
/**
 * VERTEX
 **/

layout(location = 0) in vec3 bbPosition;
layout(location = 1) in vec3 bbNormal;
layout(location = 2) in vec4 bbColor;
layout(location = 3) in vec2 bbTexCoord0;
layout(location = 4) in vec2 bbTexCoord1;

void main() {
  mat4 bbModelViewMatrix = bbViewMatrix * bbWorldMatrix;
  mat4 bbModelViewProjectionMatrix=bbProjMatrix * bbModelViewMatrix;
  mat3 bbNormalMatrix=transpose(inverse(mat3(bbModelViewMatrix)));

  bbVertex_Position = bbPosition;

  gl_Position = bbModelViewProjectionMatrix * vec4(bbPosition, 1.0);

  vec3 EyeNormal = bbNormalMatrix * bbNormal;

  for( int i=0;i<RS.TexturesUsed;i++ ){
    vec2 coord;
    if( RS.Texture[i].SphereMap==1 ) {
      coord=sphereMap( EyeNormal,gl_Position.xyz/gl_Position.w );
    }else if( RS.Texture[i].Flags==1 ) {
      coord=bbTexCoord1;
    }else {
      coord=bbTexCoord0;
    }
    bbVertex_TexCoord[i] = (RS.Texture[i].TForm * vec4(coord, 0.0, 1.0)).xy;
  }


  vec4 bbMaterialColor;
  if( RS.UseVertexColor>0 ){
    // vertex colour replaces the brush colour; the entity/brush alpha still applies
    bbMaterialColor = vec4( bbColor.rgb,RS.BrushColor.a*bbColor.a );
  }else{
    bbMaterialColor = RS.BrushColor;
  }

  if( RS.FullBright==0 ){
    bbVertex_Normal = normalize( EyeNormal );

    // Lighting as the original fixed-function pipeline did it, in world space: distant lights shine along
    // their forward axis, point lights fade with range/distance (D3D attenuation 1/(distance/range)), spot
    // lights add the cone. (This used to treat every light as a distant one, which lit objects far from any lamp.)
    vec3 worldPos = (bbWorldMatrix * vec4(bbPosition, 1.0)).xyz;
    vec3 N = normalize( transpose(inverse(mat3(bbWorldMatrix))) * bbNormal );
    vec3 camPos = -transpose(mat3(bbViewMatrix)) * bbViewMatrix[3].xyz;
    vec3 V = normalize( camPos - worldPos );

    vec4 Diffuse=vec4( 0.0 ),Specular=vec4( 0.0 );

    for( int i=0;i<LS.LightsUsed;i++ ){
      mat4 T = LS.Light[i].TForm;
      vec3 fwd = normalize( T[2].xyz );
      int type = int( LS.Light[i].Params.x + 0.5 );
      vec3 L;
      float atten = 1.0;

      if( type==1 ){
        L = -fwd;
      }else{
        vec3 d = T[3].xyz - worldPos;
        float dist = max( length(d), 0.001 );
        L = d / dist;
        atten = LS.Light[i].Params.y / dist;
        if( type==3 ){
          float rho = dot( -L,fwd );
          float co = LS.Light[i].Params.z,ci = LS.Light[i].Params.w;
          atten *= rho>=ci ? 1.0 : ( rho<=co ? 0.0 : (rho-co)/max(ci-co,0.0001) );
        }
      }

      float nDotL = max( 0.0,dot( N,L ) );
      Diffuse += LS.Light[i].Color * (nDotL*atten);

      if( RS.BrushShininess>0.0 && nDotL>0.0 ){
        float nDotH = max( 0.0,dot( N,normalize( L+V ) ) );
        Specular += LS.Light[i].Color * (pow( nDotH,RS.BrushShininess*128.0 )*min( RS.BrushShininess,1.0 )*atten);
      }
    }

    bbVertex_Color = RS.Ambient * bbMaterialColor +
                     Diffuse    * bbMaterialColor +
                     Specular;
    bbVertex_Color = clamp( bbVertex_Color, 0.0, 1.0 );
    bbVertex_Color.a = bbMaterialColor.a; // TODO: is this right?
  }else{
    bbVertex_Color = bbMaterialColor;
  }

  switch( RS.FogMode ){
  case FOG_NONE: break;
  // fixed-function style fog: linear in eye-space depth (not clip space, which the
  // projection stretches differently on each axis)
  case FOG_LINEAR: bbVertex_FogFactor=fogFactorLinear( abs( (bbModelViewMatrix * vec4(bbPosition, 1.0)).z ),RS.FogRange.x,RS.FogRange.y );break;
  }
}
#endif

#ifdef FRAGMENT
/**
 * FRAGMENT
 **/

out vec4 bbFragColor;

#define BLEND_REPLACE   0
#define BLEND_ALPHA     1
#define BLEND_MULTIPLY  2
#define BLEND_ADD       3
#define BLEND_DOT3      4
#define BLEND_MULTIPLY2 5

vec4 Sample2D( sampler2D tex,int i ){
  return texture( tex,bbVertex_TexCoord[i] );
}

vec4 SampleCube( samplerCube tex ){
  vec3 coord = normalize(vec3(bbVertex_Position.x,-bbVertex_Position.y,bbVertex_Position.z));
  return texture( tex,coord );
}

vec4 Blend( vec4 t0,vec4 t1,int i ){
  switch( RS.Texture[i].Blend ){
  default:
  case BLEND_REPLACE:  return t0;
  case BLEND_ALPHA:    return mix(t0, t1, t1.a);
  case BLEND_MULTIPLY: return t0*t1;
  case BLEND_ADD:      return vec4( t0.rgb+t1.rgb,t0.a ); // colour only: adding the alpha too made every additive pass fully opaque
  case BLEND_MULTIPLY2: return vec4( t0.rgb*t1.rgb*2.0,t0.a*t1.a );
  case BLEND_DOT3:     return t0; // bump mapping is not implemented
  }
}

void main() {
  bbFragColor=bbVertex_Color;

  // TODO: ES doesn't allow dynamic indexing of uniforms
  // so this (should) force the various compilers to unroll.
  #define ProcessTexture(i) if( i<RS.TexturesUsed ) bbFragColor=Blend( bbFragColor,RS.Texture[i].CubeMap!=1?Sample2D(bbTexture[i],i):SampleCube(bbTextureCube[i]),i );
  ProcessTexture(0);
  ProcessTexture(1);
  ProcessTexture(2);
  ProcessTexture(3);
  ProcessTexture(4);
  ProcessTexture(5);
  ProcessTexture(6);
  ProcessTexture(7);

  if( RS.FogMode>0 ){
    vec4 fogColor=vec4( RS.FogColor.rgb,bbFragColor.a );
    bbFragColor=mix( bbFragColor,fogColor,bbVertex_FogFactor );
  }

  if( RS.AlphaTest==1 && bbFragColor.a==0.0 ){
    discard;
  }
}
#endif
