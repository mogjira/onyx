#ifndef TANTO_S_SCENE_H
#define TANTO_S_SCENE_H

#include "r_geo.h"
#include "t_def.h"

#define TANTO_S_MAX_PRIMS  256 
#define TANTO_S_MAX_LIGHTS 32

typedef Tanto_Mask Tanto_S_DirtyMask;
typedef Mat4       Tanto_S_Xform;

typedef enum {
    TANTO_S_CAMERA_BIT = 0x00000001,
    TANTO_S_LIGHTS_BIT = 0x00000002,
    TANTO_S_XFORMS_BIT = 0x00000004,
} Tanto_S_DirtyBits;

typedef struct {
    Mat4 xform;
} Tanto_S_Camera;

typedef enum {
    TANTO_S_LIGHT_TYPE_POINT,
    TANTO_S_LIGHT_TYPE_DIRECTION
} Tanto_S_LightType;

typedef struct {
    Vec3 pos;
} Tanto_S_PointLight;

typedef struct {
    Vec3 dir;
} Tanto_S_DirectionLight;

typedef union {
    Tanto_S_PointLight     pointLight;
    Tanto_S_DirectionLight directionLight;
} Tanto_S_LightStructure;

typedef struct {
    Tanto_S_LightStructure structure;
    float                  intensity;
    Tanto_S_LightType      type;
} Tanto_S_Light;

typedef struct {
    Vec3     color;
    uint32_t id;
} Tanto_S_Matrial;

typedef struct {
    uint32_t          primCount;
    uint32_t          lightCount;
    Tanto_R_Primitive prims[TANTO_S_MAX_PRIMS];
    Tanto_S_Xform     xforms[TANTO_S_MAX_PRIMS];
    Tanto_S_Matrial   materials[TANTO_S_MAX_PRIMS];
    Tanto_S_Light     lights[TANTO_S_MAX_LIGHTS];
    Tanto_S_Camera    camera;
    Tanto_S_DirtyMask dirt;
} Tanto_S_Scene;

// counter-clockwise orientation
void tanto_s_CreateSimpleScene(Tanto_S_Scene* scene);
void tanto_s_CreateSimpleScene2(Tanto_S_Scene* scene);

#endif /* end of include guard: TANTO_S_SCENE_H */