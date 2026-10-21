#pragma once

#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VAMatrix VAMatrix;
typedef struct VAVector VAVector;
typedef struct VAEAXReverb VAEAXReverb;
typedef struct VALowPassFilter VALowPassFilter;
typedef struct VAColor VAColor;
typedef struct VAVisualisationData VAVisualisationData;
typedef struct VAProcessedReverb VAProcessedReverb;
typedef struct VACapsulePrimitive VACapsulePrimitive;
typedef struct VAConePrimitive VAConePrimitive;
typedef struct VACylinderPrimitive VACylinderPrimitive;
typedef struct VADiskPrimitive VADiskPrimitive;
typedef struct VAHemispherePrimitive VAHemispherePrimitive;
typedef struct VAMesh VAMesh;
typedef struct VAMeshPrimitive VAMeshPrimitive;
typedef struct VAPlanePrimitive VAPlanePrimitive;
typedef struct VAPrismPrimitive VAPrismPrimitive;
typedef struct VARectangularConePrimitive VARectangularConePrimitive;
typedef struct VASpherePrimitive VASpherePrimitive;
typedef struct VATrianglePrimitive VATrianglePrimitive;
typedef struct VATriangularConePrimitive VATriangularConePrimitive;
typedef struct VATriangularPrismPrimitive VATriangularPrismPrimitive;
typedef struct VAUnsafeVoxelPrimitive VAUnsafeVoxelPrimitive;
typedef struct VAVoxelPrimitive VAVoxelPrimitive;
typedef struct VAWorld VAWorld;
typedef struct VAEmitter VAEmitter;
typedef struct VACustomEAXFormulas VACustomEAXFormulas;
typedef struct VAAirAbsorptionSettings VAAirAbsorptionSettings;

#ifdef _WIN32
    #define EXPORT_API __declspec(dllexport)
    #define VA_THREAD_LOCAL __declspec(thread)
#else
    #define EXPORT_API __attribute__((visibility("default")))
    #define VA_THREAD_LOCAL __thread
#endif

// Material type enum. First 23 values contain default materials. First 1000 values are reserved.
typedef enum VAMaterialType
{
    VAMaterialAir = 0,
    VAMaterialBrick,
    VAMaterialCloth,
    VAMaterialConcrete,
    VAMaterialConcretePolished,
    VAMaterialDirt,
    VAMaterialGlass,
    VAMaterialGrass,
    VAMaterialGravel,
    VAMaterialGyprock,
    VAMaterialIce,
    VAMaterialLeaf,
    VAMaterialMarble,
    VAMaterialMetal,
    VAMaterialMud,
    VAMaterialRock,
    VAMaterialSand,
    VAMaterialSnow,
    VAMaterialTile,
    VAMaterialTree,
    VAMaterialWater,
    VAMaterialWoodIndoor,
    VAMaterialWoodOutdoor,
    VAMaterialTypeCount
} VAMaterialType;

typedef int VAResult;

#define VA_SUCCESS                   0
#define VA_INVALID_VALUE             1
#define VA_OUT_OF_RANGE              2
#define VA_ALREADY_EXISTS            3
#define VA_FEATURE_DISABLED          4
#define VA_ERROR_IN_USE              5
#define VA_INVALID_COUNT             6
#define VA_WORLD_CONFLICT            7
#define VA_ERROR_FILE_OPEN           8
#define VA_ERROR_FILE_WRITE          9
#define VA_ERROR_FILE_VERSION       10
#define VA_ERROR_FILE_CORRUPT       11
#define VA_INVALID_MATERIAL         12
#define VA_MATERIAL_DOES_NOT_EXIST  13
#define VA_NOT_ADDED_TO_WORLD       14
#define VA_INSUFFICIENT_VERTICES    15
#define VA_INVALID_VERTEX_COUNT     16
#define VA_INVALID_ARRAY            17
#define VA_INVALID_POINTER          18
#define VA_UNCHANGED				19
#define VA_NOT_FOUND				20
#define VA_STILL_RUNNING			21
#define VA_PENDING_REMOVAL          22
#define VA_CONFIG_ERROR             23
#define VA_TRUE                     24
#define VA_FALSE                    25
#define VA_WRONG_DIMENSION          26
#define VA_MISSING_MATERIAL_CALLBACK 27

// Create a new CapsulePrimitive with default values.
EXPORT_API VACapsulePrimitive* vaCapsulePrimitiveCreate(void);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if capsule is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaCapsulePrimitiveDestroy(VACapsulePrimitive* capsule);

// Set the material for this capsule.
// Returns VA_INVALID_POINTER if capsule is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaCapsulePrimitiveSetMaterial(VACapsulePrimitive* capsule, VAMaterialType material);

// Get the material for this capsule.
// Returns VAMaterialAir if capsule is NULL
EXPORT_API VAMaterialType vaCapsulePrimitiveGetMaterial(const VACapsulePrimitive* capsule);

// Set the radius of the cylinder and end caps.
// Returns VA_INVALID_POINTER if capsule is NULL, VA_INVALID_VALUE if radius is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaCapsulePrimitiveSetRadius(VACapsulePrimitive* capsule, float radius);

// Get the radius of the cylinder and end caps.
// Returns 0 if capsule is NULL
EXPORT_API float vaCapsulePrimitiveGetRadius(const VACapsulePrimitive* capsule);

// Set the length of the capsule cylinder, excluding end caps.
// Returns VA_INVALID_POINTER if capsule is NULL, VA_INVALID_VALUE if length is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaCapsulePrimitiveSetLength(VACapsulePrimitive* capsule, float length);

// Get the length of the capsule cylinder, excluding end caps.
// Returns 0 if capsule is NULL
EXPORT_API float vaCapsulePrimitiveGetLength(const VACapsulePrimitive* capsule);

// Set the world-space transform. Must not contain scale components.
// Returns VA_INVALID_POINTER if capsule or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity or contains a scale component (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaCapsulePrimitiveSetTransform(VACapsulePrimitive* capsule, const VAMatrix* transform);

// Get the world-space transform of this capsule.
// Returns NULL if capsule is NULL
EXPORT_API const VAMatrix* vaCapsulePrimitiveGetTransform(const VACapsulePrimitive* capsule);

// Set whether rays lose a flat percentage of energy the moment they touch this capsule, instead of calculating how long the ray spent inside it. Defaults to false (depth-based transmission). Returns VA_INVALID_POINTER if capsule is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaCapsulePrimitiveSetUseFlatTransmission(VACapsulePrimitive* capsule, bool useFlatTransmission);

// Get whether this capsule uses flat percentage transmission loss instead of depth-based transmission.
// Returns false if capsule is NULL
EXPORT_API bool vaCapsulePrimitiveGetUseFlatTransmission(const VACapsulePrimitive* capsule);

// Create a new ConePrimitive with default values.
EXPORT_API VAConePrimitive* vaConePrimitiveCreate(void);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if cone is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaConePrimitiveDestroy(VAConePrimitive* cone);

// Set the material for this cone.
// Returns VA_INVALID_POINTER if cone is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaConePrimitiveSetMaterial(VAConePrimitive* cone, VAMaterialType material);

// Get the material for this cone.
// Returns VAMaterialAir if cone is NULL
EXPORT_API VAMaterialType vaConePrimitiveGetMaterial(const VAConePrimitive* cone);

// Set the radius of the cone base.
// Returns VA_INVALID_POINTER if cone is NULL, VA_INVALID_VALUE if radius is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaConePrimitiveSetRadius(VAConePrimitive* cone, float radius);

// Get the radius of the cone base.
// Returns 0 if cone is NULL
EXPORT_API float vaConePrimitiveGetRadius(const VAConePrimitive* cone);

// Set the height of the cone.
// Returns VA_INVALID_POINTER if cone is NULL, VA_INVALID_VALUE if height is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaConePrimitiveSetHeight(VAConePrimitive* cone, float height);

// Get the height of the cone.
// Returns 0 if cone is NULL
EXPORT_API float vaConePrimitiveGetHeight(const VAConePrimitive* cone);

// Set the world-space transform. Must not contain scale components.
// Returns VA_INVALID_POINTER if cone or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity or contains a scale component (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaConePrimitiveSetTransform(VAConePrimitive* cone, const VAMatrix* transform);

// Get the world-space transform of this cone.
// Returns NULL if cone is NULL
EXPORT_API const VAMatrix* vaConePrimitiveGetTransform(const VAConePrimitive* cone);

// Set whether rays lose a flat percentage of energy the moment they touch this cone, instead of calculating how long the ray spent inside it. Defaults to false (depth-based transmission). Returns VA_INVALID_POINTER if cone is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaConePrimitiveSetUseFlatTransmission(VAConePrimitive* cone, bool useFlatTransmission);

// Get whether this cone uses flat percentage transmission loss instead of depth-based transmission.
// Returns false if cone is NULL
EXPORT_API bool vaConePrimitiveGetUseFlatTransmission(const VAConePrimitive* cone);

// Create a new CylinderPrimitive with default values.
EXPORT_API VACylinderPrimitive* vaCylinderPrimitiveCreate(void);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if cylinder is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaCylinderPrimitiveDestroy(VACylinderPrimitive* cylinder);

// Set the material for this cylinder.
// Returns VA_INVALID_POINTER if cylinder is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaCylinderPrimitiveSetMaterial(VACylinderPrimitive* cylinder, VAMaterialType material);

// Get the material for this cylinder.
// Returns VAMaterialAir if cylinder is NULL
EXPORT_API VAMaterialType vaCylinderPrimitiveGetMaterial(const VACylinderPrimitive* cylinder);

// Set the radius of the cylinder.
// Returns VA_INVALID_POINTER if cylinder is NULL, VA_INVALID_VALUE if radius is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaCylinderPrimitiveSetRadius(VACylinderPrimitive* cylinder, float radius);

// Get the radius of the cylinder.
// Returns 0 if cylinder is NULL
EXPORT_API float vaCylinderPrimitiveGetRadius(const VACylinderPrimitive* cylinder);

// Set the length of the cylinder body.
// Returns VA_INVALID_POINTER if cylinder is NULL, VA_INVALID_VALUE if length is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaCylinderPrimitiveSetLength(VACylinderPrimitive* cylinder, float length);

// Get the length of the cylinder body.
// Returns 0 if cylinder is NULL
EXPORT_API float vaCylinderPrimitiveGetLength(const VACylinderPrimitive* cylinder);

// Set the world-space transform. Must not contain scale components.
// Returns VA_INVALID_POINTER if cylinder or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity or contains a scale component (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaCylinderPrimitiveSetTransform(VACylinderPrimitive* cylinder, const VAMatrix* transform);

// Get the world-space transform of this cylinder.
// Returns NULL if cylinder is NULL
EXPORT_API const VAMatrix* vaCylinderPrimitiveGetTransform(const VACylinderPrimitive* cylinder);

// Set whether rays lose a flat percentage of energy the moment they touch this cylinder, instead of calculating how long the ray spent inside it. Defaults to false (depth-based transmission). Returns VA_INVALID_POINTER if cylinder is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaCylinderPrimitiveSetUseFlatTransmission(VACylinderPrimitive* cylinder, bool useFlatTransmission);

// Get whether this cylinder uses flat percentage transmission loss instead of depth-based transmission.
// Returns false if cylinder is NULL
EXPORT_API bool vaCylinderPrimitiveGetUseFlatTransmission(const VACylinderPrimitive* cylinder);

// Create a new DiskPrimitive with default values.
EXPORT_API VADiskPrimitive* vaDiskPrimitiveCreate(void);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if disk is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaDiskPrimitiveDestroy(VADiskPrimitive* disk);

// Set the material for this disk.
// Returns VA_INVALID_POINTER if disk is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaDiskPrimitiveSetMaterial(VADiskPrimitive* disk, VAMaterialType material);

// Get the material for this disk.
// Returns VAMaterialAir if disk is NULL
EXPORT_API VAMaterialType vaDiskPrimitiveGetMaterial(const VADiskPrimitive* disk);

// Set the radius of the disk.
// Returns VA_INVALID_POINTER if disk is NULL, VA_INVALID_VALUE if radius is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaDiskPrimitiveSetRadius(VADiskPrimitive* disk, float radius);

// Get the radius of the disk.
// Returns 0 if disk is NULL
EXPORT_API float vaDiskPrimitiveGetRadius(const VADiskPrimitive* disk);

// Set the world-space transform. Must not contain scale components.
// Returns VA_INVALID_POINTER if disk or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity or contains a scale component (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaDiskPrimitiveSetTransform(VADiskPrimitive* disk, const VAMatrix* transform);

// Get the world-space transform of this disk.
// Returns NULL if disk is NULL
EXPORT_API const VAMatrix* vaDiskPrimitiveGetTransform(const VADiskPrimitive* disk);

// Always true - a disk has zero thickness, so there is no interior for a ray to travel through.
// Returns false if disk is NULL
EXPORT_API bool vaDiskPrimitiveGetUseFlatTransmission(const VADiskPrimitive* disk);

// Always true for a disk. Set to false to get VA_INVALID_VALUE - a disk has zero thickness and must always use flat transmission.
// Returns VA_INVALID_POINTER if disk is NULL, VA_INVALID_VALUE if useFlatTransmission is false, VA_UNCHANGED if set to true
EXPORT_API VAResult vaDiskPrimitiveSetUseFlatTransmission(VADiskPrimitive* disk, bool useFlatTransmission);

// Create a new HemispherePrimitive with default values.
EXPORT_API VAHemispherePrimitive* vaHemispherePrimitiveCreate(void);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if hemisphere is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaHemispherePrimitiveDestroy(VAHemispherePrimitive* hemisphere);

// Set the material for this hemisphere.
// Returns VA_INVALID_POINTER if hemisphere is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaHemispherePrimitiveSetMaterial(VAHemispherePrimitive* hemisphere, VAMaterialType material);

// Get the material for this hemisphere.
// Returns VAMaterialAir if hemisphere is NULL
EXPORT_API VAMaterialType vaHemispherePrimitiveGetMaterial(const VAHemispherePrimitive* hemisphere);

// Set the radius of the hemisphere.
// Returns VA_INVALID_POINTER if hemisphere is NULL, VA_INVALID_VALUE if radius is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaHemispherePrimitiveSetRadius(VAHemispherePrimitive* hemisphere, float radius);

// Get the radius of the hemisphere.
// Returns 0 if hemisphere is NULL
EXPORT_API float vaHemispherePrimitiveGetRadius(const VAHemispherePrimitive* hemisphere);

// Set the world-space transform. Must not contain scale components.
// Returns VA_INVALID_POINTER if hemisphere or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity or contains a scale component (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaHemispherePrimitiveSetTransform(VAHemispherePrimitive* hemisphere, const VAMatrix* transform);

// Get the world-space transform of this hemisphere.
// Returns NULL if hemisphere is NULL
EXPORT_API const VAMatrix* vaHemispherePrimitiveGetTransform(const VAHemispherePrimitive* hemisphere);

// Set whether rays lose a flat percentage of energy the moment they touch this hemisphere, instead of calculating how long the ray spent inside it. Defaults to false (depth-based transmission). Returns VA_INVALID_POINTER if hemisphere is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaHemispherePrimitiveSetUseFlatTransmission(VAHemispherePrimitive* hemisphere, bool useFlatTransmission);

// Get whether this hemisphere uses flat percentage transmission loss instead of depth-based transmission.
// Returns false if hemisphere is NULL
EXPORT_API bool vaHemispherePrimitiveGetUseFlatTransmission(const VAHemispherePrimitive* hemisphere);

// Column-major 4x4 matrix
typedef struct VAMatrix
{
    float m11;
    float m12;
    float m13;
    float m14;
    float m21;
    float m22;
    float m23;
    float m24;
    float m31;
    float m32;
    float m33;
    float m34;
    float m41;
    float m42;
    float m43;
    float m44;
} VAMatrix;

extern const VAMatrix VA_MATRIX_IDENTITY;
extern const VAMatrix VA_MATRIX_EMPTY;

// Initializes a matrix with all 16 elements in column-major order (each group of 4 is one column)
EXPORT_API VAMatrix vaMatrixCreate(float m11, float m12, float m13, float m14,
                                   float m21, float m22, float m23, float m24,
                                   float m31, float m32, float m33, float m34,
                                   float m41, float m42, float m43, float m44);

// Get the inverse of the matrix. Asserts if the determinant is zero (no inverse exists).
// Returns VA_MATRIX_IDENTITY if mat is NULL
EXPORT_API VAMatrix vaMatrixInverse(const VAMatrix* mat);

// Creates a translation matrix.
EXPORT_API VAMatrix vaMatrixCreateTranslation(float x, float y, float z);

// Creates a rotation matrix around the X axis. rads is the angle in radians.
EXPORT_API VAMatrix vaMatrixCreateRotationX(float rads);

// Creates a rotation matrix around the Y axis. rads is the angle in radians.
EXPORT_API VAMatrix vaMatrixCreateRotationY(float rads);

// Creates a rotation matrix around the Z axis. rads is the angle in radians.
EXPORT_API VAMatrix vaMatrixCreateRotationZ(float rads);

// Creates a scale matrix. Note scale matrices can only be set on mesh primitives and not any other primitive.
EXPORT_API VAMatrix vaMatrixCreateScale(float scaleX, float scaleY, float scaleZ);

// Multiplies two matrices together.
// Returns VA_MATRIX_IDENTITY if a or b is NULL
EXPORT_API VAMatrix vaMatrixMultiply(const VAMatrix* a, const VAMatrix* b);

// Get whether all elements of a are exactly equal to those in b.
// Returns false if a or b is NULL
EXPORT_API bool vaMatrixIsEqual(const VAMatrix* a, const VAMatrix* b);

// Get whether the matrix only contains translation components.
// Returns false if a is NULL
EXPORT_API bool vaMatrixIsTranslationOnly(const VAMatrix* a);

// Returns true if any of the 16 elements of a are NaN or infinity. Returns false if a is NULL
EXPORT_API bool vaMatrixIsNanOrInfinity(const VAMatrix* a);

// Returns true if the 3x3 rotation part of a contains a scale component (any basis column's squared length is not ~1). Returns false if a is NULL
EXPORT_API bool vaMatrixHasScale(const VAMatrix* a);

typedef struct VAVector
{
    float x;
    float y;
    float z;
} VAVector;

// A vector with all components set to zero
static const VAVector VECTOR_ZERO = { 0, 0, 0 };
// A vector with all components set to one
static const VAVector VECTOR_ONE = { 1, 1, 1 };
// A unit vector pointing upward (0, 1, 0)
static const VAVector VECTOR_UP = { 0, 1, 0 };
// A vector with all components set to FLT_MIN
static const VAVector VECTOR_MIN = { FLT_MIN, FLT_MIN, FLT_MIN };
// A vector with all components set to FLT_MAX
static const VAVector VECTOR_MAX = { FLT_MAX, FLT_MAX, FLT_MAX };

// Creates a vector with the specified components
static inline VAVector vaVectorCreate(float x, float y, float z)
{
#ifdef __cplusplus
    VAVector v = { x, y, z }; return v;
#else
    return (VAVector){ x, y, z };
#endif
}

// Creates a vector with all components set to value
static inline VAVector vaVectorCreateUniform(float value)
{
#ifdef __cplusplus
    VAVector v = { value, value, value }; return v;
#else
    return (VAVector){ value, value, value };
#endif
}

// Returns the length of v
static inline float vaVectorMagnitude(VAVector v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

// Returns the component-wise sum of a and b
static inline VAVector vaVectorAdd(VAVector a, VAVector b)
{
    return vaVectorCreate(a.x + b.x, a.y + b.y, a.z + b.z);
}

// Returns the component-wise difference of a and b
static inline VAVector vaVectorSubtract(VAVector a, VAVector b)
{
    return vaVectorCreate(a.x - b.x, a.y - b.y, a.z - b.z);
}

// Multiplies the vector by a scalar value
static inline VAVector vaVectorMultiplyScalar(VAVector a, float scalar)
{
    return vaVectorCreate(a.x * scalar, a.y * scalar, a.z * scalar);
}

// Divides the vector by a scalar value
static inline VAVector vaVectorDivideScalar(VAVector a, float scalar)
{
    return vaVectorCreate(a.x / scalar, a.y / scalar, a.z / scalar);
}

// Divides a scalar vector by a vector value
static inline VAVector vaVectorDivideScalarBy(float scalar, VAVector a)
{
    return vaVectorCreate(scalar / a.x, scalar / a.y, scalar / a.z);
}

// Returns true if all components of a are greater than or equal to those of b
static inline bool vaVectorGreaterEqual(VAVector a, VAVector b)
{
    return a.x >= b.x && a.y >= b.y && a.z >= b.z;
}

// Returns true if all components of a are less than or equal to those of b
static inline bool vaVectorLessEqual(VAVector a, VAVector b)
{
    return a.x <= b.x && a.y <= b.y && a.z <= b.z;
}

// Returns true if any component of a is less than the corresponding component of b
static inline bool vaVectorLessAny(VAVector a, VAVector b)
{
    return a.x < b.x || a.y < b.y || a.z < b.z;
}

// Returns true if any component of a is greater than the corresponding component of b
static inline bool vaVectorGreaterAny(VAVector a, VAVector b)
{
    return a.x > b.x || a.y > b.y || a.z > b.z;
}

// Returns true if all components of a equal those of b
static inline bool vaVectorEqual(VAVector a, VAVector b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// Returns the component-wise minimum of a and b
static inline VAVector vaVectorMin(VAVector a, VAVector b)
{
    return vaVectorCreate(
        a.x < b.x ? a.x : b.x,
        a.y < b.y ? a.y : b.y,
        a.z < b.z ? a.z : b.z
    );
}

// Returns the component-wise maximum of a and b
static inline VAVector vaVectorMax(VAVector a, VAVector b)
{
    return vaVectorCreate(
        a.x > b.x ? a.x : b.x,
        a.y > b.y ? a.y : b.y,
        a.z > b.z ? a.z : b.z
    );
}

// Get a component via an index (0 = x, 1 = y, 2 = z)
static inline float vaVectorGetComponent(VAVector v, int index)
{
    if (index == 0) return v.x;
    if (index == 1) return v.y;
    return v.z;
}

// Returns the dot product of a and b
static inline float vaVectorDot(VAVector a, VAVector b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Returns the cross product of a and b
static inline VAVector vaVectorCross(VAVector a, VAVector b)
{
    float x = a.y * b.z - a.z * b.y;
    float y = a.z * b.x - a.x * b.z;
    float z = a.x * b.y - a.y * b.x;

    return vaVectorCreate(x, y, z);
}

// Returns the negation of a
static inline VAVector vaVectorNegate(VAVector a)
{
    return vaVectorCreate(-a.x, -a.y, -a.z);
}

// Returns a normalized copy of a
static inline VAVector vaVectorNormalize(VAVector a)
{
    float invMag = 1.0f / vaVectorMagnitude(a);
    return vaVectorCreate(a.x * invMag, a.y * invMag, a.z * invMag);
}

// Creates a directional vector from pitch and yaw angles (in radians). Yaw 0 looks down Z+ and positive yaw turns towards X+
static inline VAVector vaVectorFromPitchYaw(float pitch, float yaw)
{
    float cosPitch = cosf(pitch);
    return vaVectorCreate(cosPitch * sinf(yaw), sinf(pitch), cosPitch * cosf(yaw));
}

// Transforms a vector by a matrix, including translation (w = 1).
// Returns a unchanged if b is NULL
static inline VAVector vaVectorMultiplyMatrix(VAVector a, const VAMatrix* b)
{
    if (b == NULL)
        return a;

    float x = a.x * b->m11 + a.y * b->m21 + a.z * b->m31 + b->m41;
    float y = a.x * b->m12 + a.y * b->m22 + a.z * b->m32 + b->m42;
    float z = a.x * b->m13 + a.y * b->m23 + a.z * b->m33 + b->m43;

    return vaVectorCreate(x, y, z);
}

// Transforms a direction vector by a matrix, ignoring translation, and normalizes the result.
// Returns a normalized if b is NULL
static inline VAVector vaVectorTransformNormal(VAVector a, const VAMatrix* b)
{
    if (b == NULL)
        return vaVectorNormalize(a);

    float x = a.x * b->m11 + a.y * b->m21 + a.z * b->m31;
    float y = a.x * b->m12 + a.y * b->m22 + a.z * b->m32;
    float z = a.x * b->m13 + a.y * b->m23 + a.z * b->m33;

    return vaVectorNormalize(vaVectorCreate(x, y, z));
}

// Transforms a normal by the transpose of the given inverse matrix (inverse-transpose of the forward transform), supporting non-uniform scale.
// Returns a normalized if inverseMatrix is NULL
static inline VAVector vaVectorTransformNormalByInverseTranspose(VAVector a, const VAMatrix* inverseMatrix)
{
    if (inverseMatrix == NULL)
        return vaVectorNormalize(a);

    float x = a.x * inverseMatrix->m11 + a.y * inverseMatrix->m12 + a.z * inverseMatrix->m13;
    float y = a.x * inverseMatrix->m21 + a.y * inverseMatrix->m22 + a.z * inverseMatrix->m23;
    float z = a.x * inverseMatrix->m31 + a.y * inverseMatrix->m32 + a.z * inverseMatrix->m33;

    return vaVectorNormalize(vaVectorCreate(x, y, z));
}

// Returns true if any component of vec is NaN or infinity
static inline bool vaVectorIsNanOrInfinity(VAVector vec)
{
    return isnan(vec.x) || isinf(vec.x) || isnan(vec.y) || isinf(vec.y) || isnan(vec.z) || isinf(vec.z);
}

// Returns false if any component of vec is less than or equal to zero
static inline bool vaVectorIsPositive(VAVector vec)
{
    return vec.x > 0 && vec.y > 0 && vec.z > 0;
}

// Reflects a direction vector about a normalised vector
static inline VAVector vaVectorReflect(VAVector direction, VAVector normal)
{
    float dotProduct = vaVectorDot(direction, normal);
    VAVector scaledNormal = vaVectorMultiplyScalar(normal, 2.0f * dotProduct);
    return vaVectorSubtract(direction, scaledNormal);
}

// A triangulated mesh that is built once and can be shared across multiple VAMeshPrimitive instances.

// Create a new Mesh from a vertex buffer. vertices is the vertex buffer (3 per triangle), vertexCount is the total vertex count, minBounds/maxBounds are the pre-computed local-space AABB.
// Returns VA_INVALID_POINTER if outMesh is NULL, VA_INSUFFICIENT_VERTICES if vertexCount is less than 3, VA_INVALID_VERTEX_COUNT if vertexCount is not a multiple of 3, VA_INVALID_ARRAY if vertices is NULL
EXPORT_API VAResult vaMeshCreate(const VAVector* vertices, int vertexCount, VAVector minBounds, VAVector maxBounds, VAMesh** outMesh);

// Free all resources owned by this mesh. If a world's background threads may still be using it, it is freed automatically once they have finished, so the mesh must not be used again after this call succeeds.
// Returns VA_INVALID_POINTER if mesh is NULL, VA_ERROR_IN_USE if a VAMeshPrimitive using this mesh has not been destroyed yet
EXPORT_API VAResult vaMeshDestroy(VAMesh* mesh);

// Create a new MeshPrimitive. vertices is the vertex buffer (3 per triangle), vertexCount is total vertex count, minBounds/maxBounds are the pre-computed AABB, transform is the initial world-space transform. Returns VA_INVALID_POINTER if outPrimitive or transform is NULL, VA_INSUFFICIENT_VERTICES if vertexCount is less than 3, VA_INVALID_VERTEX_COUNT if vertexCount is not a multiple of 3, VA_INVALID_ARRAY if vertices is NULL
EXPORT_API VAResult vaMeshPrimitiveCreate(VAMaterialType material, const VAVector* vertices, int vertexCount, VAVector minBounds, VAVector maxBounds, const VAMatrix* transform, VAMeshPrimitive** outPrimitive);

// Create a new MeshPrimitive that shares geometry from a VAMesh.
// Returns VA_INVALID_POINTER if outPrimitive or transform is NULL, VA_INVALID_VALUE if mesh is NULL
EXPORT_API VAResult vaMeshPrimitiveCreateFromMesh(VAMaterialType material, VAMesh* mesh, const VAMatrix* transform, VAMeshPrimitive** outPrimitive);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if primitive is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaMeshPrimitiveDestroy(VAMeshPrimitive* primitive);

// Set the world-space transform matrix. Updates all triangle bounds.
// Returns VA_INVALID_POINTER if primitive or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaMeshPrimitiveSetTransform(VAMeshPrimitive* primitive, const VAMatrix* transform);

// Get the world-space transform matrix for this mesh.
// Returns NULL if primitive is NULL
EXPORT_API const VAMatrix* vaMeshPrimitiveGetTransform(const VAMeshPrimitive* primitive);

// Set the material for this mesh primitive.
// Returns VA_INVALID_POINTER if primitive is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaMeshPrimitiveSetMaterial(VAMeshPrimitive* primitive, VAMaterialType material);

// Get the material for this mesh primitive.
// Returns VAMaterialAir if primitive is NULL
EXPORT_API VAMaterialType vaMeshPrimitiveGetMaterial(const VAMeshPrimitive* primitive);

// Set whether rays lose a flat percentage of energy the moment they touch this mesh, instead of calculating how long the ray spent inside it. Only meaningful to disable for a watertight mesh - a non-watertight mesh has no well-defined interior. Returns VA_INVALID_POINTER if primitive is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaMeshPrimitiveSetUseFlatTransmission(VAMeshPrimitive* primitive, bool useFlatTransmission);

// Get whether this mesh uses flat percentage transmission loss instead of depth-based transmission.
// Returns false if primitive is NULL
EXPORT_API bool vaMeshPrimitiveGetUseFlatTransmission(const VAMeshPrimitive* primitive);

// Create a new PlanePrimitive with default values.
EXPORT_API VAPlanePrimitive* vaPlanePrimitiveCreate(void);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if plane is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaPlanePrimitiveDestroy(VAPlanePrimitive* plane);

// Set the material for this plane.
// Returns VA_INVALID_POINTER if plane is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaPlanePrimitiveSetMaterial(VAPlanePrimitive* plane, VAMaterialType material);

// Get the material for this plane.
// Returns VAMaterialAir if plane is NULL
EXPORT_API VAMaterialType vaPlanePrimitiveGetMaterial(const VAPlanePrimitive* plane);

// Set the width of the plane.
// Returns VA_INVALID_POINTER if plane is NULL, VA_INVALID_VALUE if width is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaPlanePrimitiveSetWidth(VAPlanePrimitive* plane, float width);

// Get the width of the plane.
// Returns 0 if plane is NULL
EXPORT_API float vaPlanePrimitiveGetWidth(const VAPlanePrimitive* plane);

// Set the height of the plane.
// Returns VA_INVALID_POINTER if plane is NULL, VA_INVALID_VALUE if height is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaPlanePrimitiveSetHeight(VAPlanePrimitive* plane, float height);

// Get the height of the plane.
// Returns 0 if plane is NULL
EXPORT_API float vaPlanePrimitiveGetHeight(const VAPlanePrimitive* plane);

// Set the world-space transform. Must not contain scale components.
// Returns VA_INVALID_POINTER if plane or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity or contains a scale component (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaPlanePrimitiveSetTransform(VAPlanePrimitive* plane, const VAMatrix* transform);

// Get the world-space transform of this plane.
// Returns NULL if plane is NULL
EXPORT_API const VAMatrix* vaPlanePrimitiveGetTransform(const VAPlanePrimitive* plane);

// Always true - a plane has zero thickness, so there is no interior for a ray to travel through.
// Returns false if plane is NULL
EXPORT_API bool vaPlanePrimitiveGetUseFlatTransmission(const VAPlanePrimitive* plane);

// Always true for a plane. Set to false to get VA_INVALID_VALUE - a plane has zero thickness and must always use flat transmission.
// Returns VA_INVALID_POINTER if plane is NULL, VA_INVALID_VALUE if useFlatTransmission is false, VA_UNCHANGED if set to true
EXPORT_API VAResult vaPlanePrimitiveSetUseFlatTransmission(VAPlanePrimitive* plane, bool useFlatTransmission);

// Create a new PrismPrimitive with default values.
EXPORT_API VAPrismPrimitive* vaPrismPrimitiveCreate(void);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if prism is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaPrismPrimitiveDestroy(VAPrismPrimitive* prism);

// Set the material for this prism.
// Returns VA_INVALID_POINTER if prism is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaPrismPrimitiveSetMaterial(VAPrismPrimitive* prism, VAMaterialType material);

// Get the material for this prism.
// Returns VAMaterialAir if prism is NULL
EXPORT_API VAMaterialType vaPrismPrimitiveGetMaterial(const VAPrismPrimitive* prism);

// Set the width, height, and depth of the prism.
// Returns VA_INVALID_POINTER if prism is NULL, VA_INVALID_VALUE if any component of size is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaPrismPrimitiveSetSize(VAPrismPrimitive* prism, VAVector size);

// Get the width, height, and depth of the prism.
// Returns VECTOR_ZERO if prism is NULL
EXPORT_API VAVector vaPrismPrimitiveGetSize(const VAPrismPrimitive* prism);

// Set the world-space transform. Must not contain scale components.
// Returns VA_INVALID_POINTER if prism or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity or contains a scale component (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaPrismPrimitiveSetTransform(VAPrismPrimitive* prism, const VAMatrix* transform);

// Get the world-space transform of this prism.
// Returns NULL if prism is NULL
EXPORT_API const VAMatrix* vaPrismPrimitiveGetTransform(const VAPrismPrimitive* prism);

// Set whether rays lose a flat percentage of energy the moment they touch this prism, instead of calculating how long the ray spent inside it. Defaults to false (depth-based transmission). Returns VA_INVALID_POINTER if prism is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaPrismPrimitiveSetUseFlatTransmission(VAPrismPrimitive* prism, bool useFlatTransmission);

// Get whether this prism uses flat percentage transmission loss instead of depth-based transmission.
// Returns false if prism is NULL
EXPORT_API bool vaPrismPrimitiveGetUseFlatTransmission(const VAPrismPrimitive* prism);

// Create a new RectangularConePrimitive with default values.
EXPORT_API VARectangularConePrimitive* vaRectangularConePrimitiveCreate(void);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if rectCone is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaRectangularConePrimitiveDestroy(VARectangularConePrimitive* rectCone);

// Set the material for this rectangular cone.
// Returns VA_INVALID_POINTER if rectCone is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaRectangularConePrimitiveSetMaterial(VARectangularConePrimitive* rectCone, VAMaterialType material);

// Get the material for this rectangular cone.
// Returns VAMaterialAir if rectCone is NULL
EXPORT_API VAMaterialType vaRectangularConePrimitiveGetMaterial(const VARectangularConePrimitive* rectCone);

// Set the width of the rectangular cone base.
// Returns VA_INVALID_POINTER if rectCone is NULL, VA_INVALID_VALUE if width is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaRectangularConePrimitiveSetWidth(VARectangularConePrimitive* rectCone, float width);

// Get the width of the rectangular cone base.
// Returns 0 if rectCone is NULL
EXPORT_API float vaRectangularConePrimitiveGetWidth(const VARectangularConePrimitive* rectCone);

// Set the length of the rectangular cone base.
// Returns VA_INVALID_POINTER if rectCone is NULL, VA_INVALID_VALUE if length is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaRectangularConePrimitiveSetLength(VARectangularConePrimitive* rectCone, float length);

// Get the length of the rectangular cone base.
// Returns 0 if rectCone is NULL
EXPORT_API float vaRectangularConePrimitiveGetLength(const VARectangularConePrimitive* rectCone);

// Set the height of the rectangular cone.
// Returns VA_INVALID_POINTER if rectCone is NULL, VA_INVALID_VALUE if height is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaRectangularConePrimitiveSetHeight(VARectangularConePrimitive* rectCone, float height);

// Get the height of the rectangular cone.
// Returns 0 if rectCone is NULL
EXPORT_API float vaRectangularConePrimitiveGetHeight(const VARectangularConePrimitive* rectCone);

// Set the world-space transform. Must not contain scale components.
// Returns VA_INVALID_POINTER if rectCone or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity or contains a scale component (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaRectangularConePrimitiveSetTransform(VARectangularConePrimitive* rectCone, const VAMatrix* transform);

// Get the world-space transform of this rectangular cone.
// Returns NULL if rectCone is NULL
EXPORT_API const VAMatrix* vaRectangularConePrimitiveGetTransform(const VARectangularConePrimitive* rectCone);

// Set whether rays lose a flat percentage of energy the moment they touch this rectangular cone, instead of calculating how long the ray spent inside it. Defaults to false (depth-based transmission). Returns VA_INVALID_POINTER if rectCone is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaRectangularConePrimitiveSetUseFlatTransmission(VARectangularConePrimitive* rectCone, bool useFlatTransmission);

// Get whether this rectangular cone uses flat percentage transmission loss instead of depth-based transmission.
// Returns false if rectCone is NULL
EXPORT_API bool vaRectangularConePrimitiveGetUseFlatTransmission(const VARectangularConePrimitive* rectCone);

// Create a new SpherePrimitive with default values.
EXPORT_API VASpherePrimitive* vaSpherePrimitiveCreate(void);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if sphere is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaSpherePrimitiveDestroy(VASpherePrimitive* sphere);

// Set the material for this sphere.
// Returns VA_INVALID_POINTER if sphere is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaSpherePrimitiveSetMaterial(VASpherePrimitive* sphere, VAMaterialType material);

// Get the material for this sphere.
// Returns VAMaterialAir if sphere is NULL
EXPORT_API VAMaterialType vaSpherePrimitiveGetMaterial(const VASpherePrimitive* sphere);

// Set the center of the sphere.
// Returns VA_INVALID_POINTER if sphere is NULL, VA_INVALID_VALUE if center is NaN or Infinity (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaSpherePrimitiveSetCenter(VASpherePrimitive* sphere, VAVector center);

// Get the center of the sphere.
// Returns VECTOR_ZERO if sphere is NULL
EXPORT_API VAVector vaSpherePrimitiveGetCenter(const VASpherePrimitive* sphere);

// Set the radius of the sphere.
// Returns VA_INVALID_POINTER if sphere is NULL, VA_INVALID_VALUE if radius is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaSpherePrimitiveSetRadius(VASpherePrimitive* sphere, float radius);

// Get the radius of the sphere.
// Returns 0 if sphere is NULL
EXPORT_API float vaSpherePrimitiveGetRadius(const VASpherePrimitive* sphere);

// Set whether rays lose a flat percentage of energy the moment they touch this sphere, instead of calculating how long the ray spent inside it. Defaults to false (depth-based transmission). Returns VA_INVALID_POINTER if sphere is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaSpherePrimitiveSetUseFlatTransmission(VASpherePrimitive* sphere, bool useFlatTransmission);

// Get whether this sphere uses flat percentage transmission loss instead of depth-based transmission.
// Returns false if sphere is NULL
EXPORT_API bool vaSpherePrimitiveGetUseFlatTransmission(const VASpherePrimitive* sphere);

// Create a new TrianglePrimitive with default values.
EXPORT_API VATrianglePrimitive* vaTrianglePrimitiveCreate(void);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if triangle is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaTrianglePrimitiveDestroy(VATrianglePrimitive* triangle);

// Set the material for this triangle.
// Returns VA_INVALID_POINTER if triangle is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTrianglePrimitiveSetMaterial(VATrianglePrimitive* triangle, VAMaterialType material);

// Get the material for this triangle.
// Returns VAMaterialAir if triangle is NULL
EXPORT_API VAMaterialType vaTrianglePrimitiveGetMaterial(const VATrianglePrimitive* triangle);

// Set the first vertex position.
// Returns VA_INVALID_POINTER if triangle is NULL, VA_INVALID_VALUE if position is NaN or Infinity (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTrianglePrimitiveSetPosition0(VATrianglePrimitive* triangle, VAVector position);

// Get the first vertex position.
// Returns VECTOR_ZERO if triangle is NULL
EXPORT_API VAVector vaTrianglePrimitiveGetPosition0(const VATrianglePrimitive* triangle);

// Set the second vertex position.
// Returns VA_INVALID_POINTER if triangle is NULL, VA_INVALID_VALUE if position is NaN or Infinity (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTrianglePrimitiveSetPosition1(VATrianglePrimitive* triangle, VAVector position);

// Get the second vertex position.
// Returns VECTOR_ZERO if triangle is NULL
EXPORT_API VAVector vaTrianglePrimitiveGetPosition1(const VATrianglePrimitive* triangle);

// Set the third vertex position.
// Returns VA_INVALID_POINTER if triangle is NULL, VA_INVALID_VALUE if position is NaN or Infinity (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTrianglePrimitiveSetPosition2(VATrianglePrimitive* triangle, VAVector position);

// Get the third vertex position.
// Returns VECTOR_ZERO if triangle is NULL
EXPORT_API VAVector vaTrianglePrimitiveGetPosition2(const VATrianglePrimitive* triangle);

// Always true - a triangle has zero thickness, so there is no interior for a ray to travel through.
// Returns false if triangle is NULL
EXPORT_API bool vaTrianglePrimitiveGetUseFlatTransmission(const VATrianglePrimitive* triangle);

// Always true for a triangle. Set to false to get VA_INVALID_VALUE - a triangle has zero thickness and must always use flat transmission.
// Returns VA_INVALID_POINTER if triangle is NULL, VA_INVALID_VALUE if useFlatTransmission is false, VA_UNCHANGED if set to true
EXPORT_API VAResult vaTrianglePrimitiveSetUseFlatTransmission(VATrianglePrimitive* triangle, bool useFlatTransmission);

// Create a new TriangularConePrimitive with default values.
EXPORT_API VATriangularConePrimitive* vaTriangularConePrimitiveCreate(void);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if cone is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaTriangularConePrimitiveDestroy(VATriangularConePrimitive* cone);

// Set the material for this triangular cone.
// Returns VA_INVALID_POINTER if cone is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTriangularConePrimitiveSetMaterial(VATriangularConePrimitive* cone, VAMaterialType material);

// Get the material for this triangular cone.
// Returns VAMaterialAir if cone is NULL
EXPORT_API VAMaterialType vaTriangularConePrimitiveGetMaterial(const VATriangularConePrimitive* cone);

// Set the radius of the triangular cone base.
// Returns VA_INVALID_POINTER if cone is NULL, VA_INVALID_VALUE if radius is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTriangularConePrimitiveSetRadius(VATriangularConePrimitive* cone, float radius);

// Get the radius of the triangular cone base.
// Returns 0 if cone is NULL
EXPORT_API float vaTriangularConePrimitiveGetRadius(const VATriangularConePrimitive* cone);

// Set the height of the triangular cone.
// Returns VA_INVALID_POINTER if cone is NULL, VA_INVALID_VALUE if height is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTriangularConePrimitiveSetHeight(VATriangularConePrimitive* cone, float height);

// Get the height of the triangular cone.
// Returns 0 if cone is NULL
EXPORT_API float vaTriangularConePrimitiveGetHeight(const VATriangularConePrimitive* cone);

// Set the world-space transform. Must not contain scale components.
// Returns VA_INVALID_POINTER if cone or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity or contains a scale component (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTriangularConePrimitiveSetTransform(VATriangularConePrimitive* cone, const VAMatrix* transform);

// Get the world-space transform of this triangular cone.
// Returns NULL if cone is NULL
EXPORT_API const VAMatrix* vaTriangularConePrimitiveGetTransform(const VATriangularConePrimitive* cone);

// Set whether rays lose a flat percentage of energy the moment they touch this triangular cone, instead of calculating how long the ray spent inside it. Defaults to false (depth-based transmission). Returns VA_INVALID_POINTER if cone is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTriangularConePrimitiveSetUseFlatTransmission(VATriangularConePrimitive* cone, bool useFlatTransmission);

// Get whether this triangular cone uses flat percentage transmission loss instead of depth-based transmission.
// Returns false if cone is NULL
EXPORT_API bool vaTriangularConePrimitiveGetUseFlatTransmission(const VATriangularConePrimitive* cone);

// Create a new TriangularPrismPrimitive with default values.
EXPORT_API VATriangularPrismPrimitive* vaTriangularPrismPrimitiveCreate(void);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if prism is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaTriangularPrismPrimitiveDestroy(VATriangularPrismPrimitive* prism);

// Set the material for this triangular prism.
// Returns VA_INVALID_POINTER if prism is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTriangularPrismPrimitiveSetMaterial(VATriangularPrismPrimitive* prism, VAMaterialType material);

// Get the material for this triangular prism.
// Returns VAMaterialAir if prism is NULL
EXPORT_API VAMaterialType vaTriangularPrismPrimitiveGetMaterial(const VATriangularPrismPrimitive* prism);

// Set the radius of the triangular prism cross-section.
// Returns VA_INVALID_POINTER if prism is NULL, VA_INVALID_VALUE if radius is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTriangularPrismPrimitiveSetRadius(VATriangularPrismPrimitive* prism, float radius);

// Get the radius of the triangular prism cross-section.
// Returns 0 if prism is NULL
EXPORT_API float vaTriangularPrismPrimitiveGetRadius(const VATriangularPrismPrimitive* prism);

// Set the length of the triangular prism along its axis.
// Returns VA_INVALID_POINTER if prism is NULL, VA_INVALID_VALUE if length is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTriangularPrismPrimitiveSetLength(VATriangularPrismPrimitive* prism, float length);

// Get the length of the triangular prism along its axis.
// Returns 0 if prism is NULL
EXPORT_API float vaTriangularPrismPrimitiveGetLength(const VATriangularPrismPrimitive* prism);

// Set the world-space transform. Must not contain scale components.
// Returns VA_INVALID_POINTER if prism or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity or contains a scale component (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTriangularPrismPrimitiveSetTransform(VATriangularPrismPrimitive* prism, const VAMatrix* transform);

// Get the world-space transform of this triangular prism.
// Returns NULL if prism is NULL
EXPORT_API const VAMatrix* vaTriangularPrismPrimitiveGetTransform(const VATriangularPrismPrimitive* prism);

// Set whether rays lose a flat percentage of energy the moment they touch this triangular prism, instead of calculating how long the ray spent inside it. Defaults to false (depth-based transmission). Returns VA_INVALID_POINTER if prism is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaTriangularPrismPrimitiveSetUseFlatTransmission(VATriangularPrismPrimitive* prism, bool useFlatTransmission);

// Get whether this triangular prism uses flat percentage transmission loss instead of depth-based transmission.
// Returns false if prism is NULL
EXPORT_API bool vaTriangularPrismPrimitiveGetUseFlatTransmission(const VATriangularPrismPrimitive* prism);

// Invoked on the main thread once a removed VAUnsafeVoxelPrimitive is no longer used by the raytracing threads, so its voxel memory can be freed
typedef void (*VAUnsafeVoxelRemovedCallback)(void* userData);

// Create a 3D grid of voxels that reads directly from memory owned by the caller, rather than copying it. Uses the same memory layout as VAVoxelPrimitive: (x * height + y) * depth + z.
// Each voxel is converted to a material by the world's VAUnsafeVoxelMaterialCallback and VAUnsafeVoxelIsSolidCallback, which must be set with vaWorldSetUnsafeVoxelMaterialCallbacks before adding this primitive to a world.
// The memory is read from the raytracing threads without synchronisation, and must remain valid until this primitive is removed from its world and its VAUnsafeVoxelRemovedCallback is invoked (see vaUnsafeVoxelPrimitiveSetRemovedCallback).
// Returns NULL if data is NULL, or width, height, depth or stride are <= 0
EXPORT_API VAUnsafeVoxelPrimitive* vaUnsafeVoxelPrimitiveCreate(void* data, int width, int height, int depth, int stride);

// Same as vaUnsafeVoxelPrimitiveCreate, but with a custom memory layout. The voxel at (x, y, z) is at data + (x * xPitch + y * yPitch + z * zPitch) * stride.
// Returns NULL if data is NULL, or width, height, depth, stride or a pitch are <= 0
EXPORT_API VAUnsafeVoxelPrimitive* vaUnsafeVoxelPrimitiveCreateWithPitch(void* data, int width, int height, int depth, int stride, int xPitch, int yPitch, int zPitch);

// Free all resources owned by this primitive. Does not free the voxel data, as it is owned by the caller.
// Returns VA_INVALID_POINTER if voxel is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaUnsafeVoxelPrimitiveDestroy(VAUnsafeVoxelPrimitive* voxel);

// Set the primitive's material. Not used for raytracing, as the world's VAUnsafeVoxelMaterialCallback provides each voxel's material.
// Returns VA_INVALID_POINTER if voxel is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaUnsafeVoxelPrimitiveSetMaterial(VAUnsafeVoxelPrimitive* voxel, VAMaterialType material);

// Get the primitive's material.
// Returns VAMaterialAir if voxel is NULL
EXPORT_API VAMaterialType vaUnsafeVoxelPrimitiveGetMaterial(const VAUnsafeVoxelPrimitive* voxel);

// Set the scale of the voxels.
// Returns VA_INVALID_POINTER if voxel is NULL, VA_INVALID_VALUE if scale is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaUnsafeVoxelPrimitiveSetScale(VAUnsafeVoxelPrimitive* voxel, float scale);

// Get the scale of the voxels.
// Returns 0 if voxel is NULL
EXPORT_API float vaUnsafeVoxelPrimitiveGetScale(const VAUnsafeVoxelPrimitive* voxel);

// Set the world-space transform. Must only contain rotation and translation components, not scale.
// Returns VA_INVALID_POINTER if voxel or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity or contains a scale component (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaUnsafeVoxelPrimitiveSetTransform(VAUnsafeVoxelPrimitive* voxel, const VAMatrix* transform);

// Get the world-space transform of this voxel primitive.
// Returns NULL if voxel is NULL
EXPORT_API const VAMatrix* vaUnsafeVoxelPrimitiveGetTransform(const VAUnsafeVoxelPrimitive* voxel);

// Get the grid dimensions, set at creation time.
// Returns VA_INVALID_POINTER if voxel, width, height or depth is NULL
EXPORT_API VAResult vaUnsafeVoxelPrimitiveGetSize(const VAUnsafeVoxelPrimitive* voxel, int* width, int* height, int* depth);

// Get the number of voxels between neighbouring voxels along each axis, set at creation time.
// Returns VA_INVALID_POINTER if voxel, xPitch, yPitch or zPitch is NULL
EXPORT_API VAResult vaUnsafeVoxelPrimitiveGetPitch(const VAUnsafeVoxelPrimitive* voxel, int* xPitch, int* yPitch, int* zPitch);

// Get the size of each voxel in bytes, set at creation time.
// Returns 0 if voxel is NULL
EXPORT_API int vaUnsafeVoxelPrimitiveGetStride(const VAUnsafeVoxelPrimitive* voxel);

// Get the pointer to the first voxel, as passed to vaUnsafeVoxelPrimitiveCreate.
// Returns NULL if voxel is NULL
EXPORT_API void* vaUnsafeVoxelPrimitiveGetData(const VAUnsafeVoxelPrimitive* voxel);

// Get the material of a specific voxel, using the world's VAUnsafeVoxelMaterialCallback.
// Returns VAMaterialAir if voxel is NULL, x, y or z is outside the grid bounds, or the primitive hasn't been added to a world
EXPORT_API VAMaterialType vaUnsafeVoxelPrimitiveGetVoxel(const VAUnsafeVoxelPrimitive* voxel, int x, int y, int z);
// Returns true if the voxel at the given grid cell is solid, using the world's VAUnsafeVoxelIsSolidCallback.
// Returns true if the voxel is not VAMaterialAir.
// Returns false if voxel is NULL, x, y or z is outside the grid bounds, or the primitive hasn't been added to a world
EXPORT_API bool vaUnsafeVoxelPrimitiveIsSolid(const VAUnsafeVoxelPrimitive* voxel, int x, int y, int z);

// Call this after editing voxel data.
// Returns VA_INVALID_POINTER if voxel is NULL, VA_UNCHANGED if already dirty
EXPORT_API VAResult vaUnsafeVoxelPrimitiveSetDataDirty(VAUnsafeVoxelPrimitive* voxel);

// Set a callback that's invoked on the main thread once this primitive has been removed from its world and the raytracing threads no longer read its voxel memory, so it's safe to free.
// This is invoked inside vaWorldUpdate or vaWorldWait once the next raytracing results are handled, inside vaWorldRemovePrimitive if the primitive was never sent to the raytracing threads, or inside vaWorldDestroy (also for primitives still in the world).
// The callback and userData are captured when vaWorldRemovePrimitive is called, so this primitive can be destroyed before the callback is invoked. Pass NULL to clear it.
// Returns VA_INVALID_POINTER if voxel is NULL
EXPORT_API VAResult vaUnsafeVoxelPrimitiveSetRemovedCallback(VAUnsafeVoxelPrimitive* voxel, VAUnsafeVoxelRemovedCallback callback, void* userData);

// Create a new VoxelPrimitive with the specified grid size. All voxels default to VAMaterialAir.
EXPORT_API VAVoxelPrimitive* vaVoxelPrimitiveCreate(int width, int height, int depth);

// Free all resources owned by this primitive.
// Returns VA_INVALID_POINTER if voxel is NULL, VA_ERROR_IN_USE if the primitive is still added to a world
EXPORT_API VAResult vaVoxelPrimitiveDestroy(VAVoxelPrimitive* voxel);

// Set the material for this voxel primitive.
// Returns VA_INVALID_POINTER if voxel is NULL, VA_INVALID_MATERIAL if material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the material does not exist in the primitive's world, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaVoxelPrimitiveSetMaterial(VAVoxelPrimitive* voxel, VAMaterialType material);

// Get the material for this voxel primitive.
// Returns VAMaterialAir if voxel is NULL
EXPORT_API VAMaterialType vaVoxelPrimitiveGetMaterial(const VAVoxelPrimitive* voxel);

// Set the scale of the voxels.
// Returns VA_INVALID_POINTER if voxel is NULL, VA_INVALID_VALUE if scale is NaN, infinity or negative (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaVoxelPrimitiveSetScale(VAVoxelPrimitive* voxel, float scale);

// Get the scale of the voxels.
// Returns 0 if voxel is NULL
EXPORT_API float vaVoxelPrimitiveGetScale(const VAVoxelPrimitive* voxel);

// Set the world-space transform. Must only contain rotation and translation components, not scale.
// Returns VA_INVALID_POINTER if voxel or transform is NULL, VA_INVALID_VALUE if transform is NaN/Infinity or contains a scale component (dev builds only), VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaVoxelPrimitiveSetTransform(VAVoxelPrimitive* voxel, const VAMatrix* transform);

// Get the world-space transform of this voxel primitive.
// Returns NULL if voxel is NULL
EXPORT_API const VAMatrix* vaVoxelPrimitiveGetTransform(const VAVoxelPrimitive* voxel);

// Get the grid dimensions, set at creation time.
// Returns VA_INVALID_POINTER if voxel, width, height or depth is NULL
EXPORT_API VAResult vaVoxelPrimitiveGetSize(const VAVoxelPrimitive* voxel, int* width, int* height, int* depth);

// Set the material of the voxel at the given grid cell. Call vaVoxelPrimitiveSetDataDirty after editing.
// Returns VA_INVALID_POINTER if voxel is NULL, VA_OUT_OF_RANGE if x, y or z is outside the grid bounds
EXPORT_API VAResult vaVoxelPrimitiveSetVoxel(VAVoxelPrimitive* voxel, int x, int y, int z, VAMaterialType material);

// Get the material of the voxel at the given grid cell.
// Returns VAMaterialAir if voxel is NULL or x, y or z is outside the grid bounds
EXPORT_API VAMaterialType vaVoxelPrimitiveGetVoxel(const VAVoxelPrimitive* voxel, int x, int y, int z);

// Get a direct pointer to the underlying voxel data, row-major indexed as (x * gridHeight + y) * gridDepth + z. For bulk edits, faster than repeated vaVoxelPrimitiveSetVoxel calls - call vaVoxelPrimitiveSetDataDirty after editing. Returns NULL if voxel is NULL
EXPORT_API VAMaterialType* vaVoxelPrimitiveGetVoxelData(VAVoxelPrimitive* voxel);

// Call this after editing voxel data.
// Returns VA_INVALID_POINTER if voxel is NULL, VA_UNCHANGED if already dirty
EXPORT_API VAResult vaVoxelPrimitiveSetDataDirty(VAVoxelPrimitive* voxel);

// Coordinate system used when calculating listener-relative reverb directionality
typedef enum VACoordinateSystem
{
    // Left-handed. Up = Y+, Forward = Z+, Right = X+. Identical to this SDK's internal space (no conversion applied). Yaw 0 looks down Z+ and positive yaw turns right (towards X+).
    VACoordinateSystemDefault = 0,

    // Right-handed. Up = Z+, Forward = Y+, Right = X+. Matches Blender's Front (Numpad-1) view, which looks down Y+. Yaw 0 looks down Y+ and positive yaw turns left (counter-clockwise around Z+, same as Blender's Z rotation).
    VACoordinateSystemBlender,

    // Right-handed. Up = Y+, Forward = Z-, Right = X+. Matches Godot's camera/light forward convention (not the separate +Z "model front" convention Godot uses for imported assets). Same as OpenGL/glTF. Yaw 0 looks down Z- and positive yaw turns left (counter-clockwise around Y+, same as Godot's Y rotation).
    VACoordinateSystemGodot,

    // Left-handed. Up = Y+, Forward = Z+, Right = X+. Mathematically identical to VACoordinateSystemDefault. Yaw 0 looks down Z+ and positive yaw turns right (towards X+).
    VACoordinateSystemUnity,

    // Left-handed. Up = Z+, Forward = X+, Right = Y+. Yaw 0 looks down X+ and positive yaw turns right (towards Y+, same as FRotator).
    VACoordinateSystemUnreal,

    // Right-handed. Up = Y+, Forward = Z-, Right = X+. Same as OpenGL, glTF and Three.js. Mathematically identical to VACoordinateSystemGodot. Yaw 0 looks down Z- and positive yaw turns left (counter-clockwise around Y+).
    VACoordinateSystemOpenGL,

} VACoordinateSystem;

// Converts a voxel of a VAUnsafeVoxelPrimitive to a material. voxel points to the voxel, which is stride bytes long, and userData is the value passed to vaWorldSetUnsafeVoxelMaterialCallbacks.
// Only called for solid voxels when the material is needed, i.e. when a ray hits or permeates through them. Any material other than VAMaterialAir must exist in the world.
// Called from the raytracing threads without synchronisation, so it must be thread-safe and must not modify shared state.
typedef VAMaterialType (*VAUnsafeVoxelMaterialCallback)(const void* voxel, void* userData);

// Returns true if a voxel of a VAUnsafeVoxelPrimitive is solid. Called for every voxel a ray passes through, so should be faster than VAUnsafeVoxelMaterialCallback. Must return the same result as VAUnsafeVoxelMaterialCallback(voxel, userData) != VAMaterialAir.
// Called from the raytracing threads without synchronisation, so it must be thread-safe and must not modify shared state.
typedef bool (*VAUnsafeVoxelIsSolidCallback)(const void* voxel, void* userData);

// Set the functions that convert the voxels of every VAUnsafeVoxelPrimitive in this world to materials. Both must be set before adding a VAUnsafeVoxelPrimitive to this world.
// Setting them again, even to the same callbacks and userData, re-sends every VAUnsafeVoxelPrimitive to the raytracing threads, e.g. after the mapping changes. Pass NULL for both to clear them.
// Returns VA_INVALID_POINTER if world is NULL or one of getMaterial or isSolid is NULL, VA_ERROR_IN_USE if both are NULL while the world contains a VAUnsafeVoxelPrimitive, or while the raytracing threads still use a removed one (i.e. before its VAUnsafeVoxelRemovedCallback is invoked)
EXPORT_API VAResult vaWorldSetUnsafeVoxelMaterialCallbacks(VAWorld* world, VAUnsafeVoxelMaterialCallback getMaterial, VAUnsafeVoxelIsSolidCallback isSolid, void* userData);

// Get the material callback set by vaWorldSetUnsafeVoxelMaterialCallbacks.
// Returns NULL if world is NULL or no callbacks have been set
EXPORT_API VAUnsafeVoxelMaterialCallback vaWorldGetUnsafeVoxelMaterialCallback(const VAWorld* world);

// Get the is-solid callback set by vaWorldSetUnsafeVoxelMaterialCallbacks.
// Returns NULL if world is NULL or no callbacks have been set
EXPORT_API VAUnsafeVoxelIsSolidCallback vaWorldGetUnsafeVoxelIsSolidCallback(const VAWorld* world);

// Convert a world-space direction (e.g. vaEAXReverbGetRelativeDirection) to a listener-space direction, for use as EFX reflections/late reverb pan. Same as vaWorldConvertWorldToListenerDirection
// Returns (0, 0, 0) if world is NULL
EXPORT_API VAVector vaWorldCalculateListenerRelativePan(const VAWorld* world, VAVector worldVector, float listenerPitch, float listenerYaw);

// Convert a world-space direction (in the world's CoordinateSystem) to a listener-space direction, based on the listener's pitch and yaw (in the world's CoordinateSystem)
// Listener space is always X+ right, Y+ up and Z+ forward relative to the listener, regardless of CoordinateSystem, matching the EFX reflections/late reverb pan convention. Inverse of vaWorldConvertListenerToWorldDirection
// Returns (0, 0, 0) if world is NULL
EXPORT_API VAVector vaWorldConvertWorldToListenerDirection(const VAWorld* world, VAVector worldDirection, float listenerPitch, float listenerYaw);

// Convert a listener-space direction (X+ right, Y+ up, Z+ forward relative to the listener) to a world-space direction (in the world's CoordinateSystem), based on the listener's pitch and yaw (in the world's CoordinateSystem)
// Inverse of vaWorldConvertWorldToListenerDirection
// Returns (0, 0, 0) if world is NULL
EXPORT_API VAVector vaWorldConvertListenerToWorldDirection(const VAWorld* world, VAVector listenerDirection, float listenerPitch, float listenerYaw);

// Set the pitch of the camera in the debug window (dev build only).
// Returns VA_INVALID_POINTER if world is NULL, VA_FEATURE_DISABLED if renderingEnabled is false.
EXPORT_API VAResult vaWorldSetCameraPitch(VAWorld* world, float pitch);

// Get the pitch of the camera in the debug window (dev build only).
// Returns 0 if world is NULL or renderingEnabled is false.
EXPORT_API float vaWorldGetCameraPitch(const VAWorld* world);

// Set the yaw of the camera in the debug window (dev build only).
// Returns VA_INVALID_POINTER if world is NULL, VA_FEATURE_DISABLED if renderingEnabled is false.
EXPORT_API VAResult vaWorldSetCameraYaw(VAWorld* world, float yaw);

// Get the yaw of the camera in the debug window (dev build only).
// Returns 0 if world is NULL or renderingEnabled is false.
EXPORT_API float vaWorldGetCameraYaw(const VAWorld* world);

// Set the vertical field of view (in radians) of the camera in the debug window (dev build only).
// Returns VA_INVALID_POINTER if world is NULL, VA_FEATURE_DISABLED if renderingEnabled is false, VA_OUT_OF_RANGE if fieldOfView is not between 0 and PI.
EXPORT_API VAResult vaWorldSetFieldOfView(VAWorld* world, float fieldOfView);

// Get the vertical field of view (in radians) of the camera in the debug window (dev build only).
// Returns 0 if world is NULL or renderingEnabled is false.
EXPORT_API float vaWorldGetFieldOfView(const VAWorld* world);

#define VA_VERSION_MAJOR 1
#define VA_VERSION_MINOR 10
#define VA_VERSION_PATCH 1

// Get the major, minor and patch version numbers of this SDK.
EXPORT_API void vaGetVersion(int* outMajor, int* outMinor, int* outPatch);

// Returns true if this is a production build.
EXPORT_API bool vaIsProduction();

// Returns the dB/m transmission value that reduces energy to 0.1% after travelling X meters through a material
EXPORT_API float vaMaterialConvertMetersToTransmission(float meters);

// Returns the distance in meters that reduces energy to 0.1% for the provided dB/m transmission value
EXPORT_API float vaMaterialConvertTransmissionToMeters(float transmission);

// Computed EAX reverb parameters
typedef struct VAEAXReverb
{
    float outsidePercent;
    float returnedPercent;
    float materialAbsorptionLF;
    float materialAbsorptionHF;
    float materialRoughness;

    VAEmitter** relativeEmitters;   /* Emitter keys for relativeDirections/relativeGains */
    VAVector* relativeDirections;   /* World-space direction that reverb should be heard from, relative to each emitter with hasRelativeReverb=true. Access via vaEAXReverbGetRelativeDirection */
    float* relativeGains;           /* Gain for this reverb effect, relative to each emitter with hasRelativeReverb=true. Access via vaEAXReverbGetRelativeGain */
    int relativeCount;
    int relativeCapacity;

    float reflectionsDelay;         /* Delay before early reflections are heard, in seconds (0–0.3) */
    float density;                  /* Modal density of the late reverberation (0–1) */
    float diffusion;                /* Echo diffusion of the late reverberation (0–1) */
    float gainLF;                   /* Low-frequency gain of the reverb (0–1) */
    float gainHF;                   /* High-frequency gain of the reverb (0–1) */
    float gain;                     /* Overall linear gain of the reverb (0–1) */
    float decayTime;                /* Reverberation decay time at mid frequencies, in seconds (0.1–20) */
    float decayLFRatio;             /* Ratio of low-frequency decay time to mid-frequency decay time (0.1–2) */
    float decayHFRatio;             /* Ratio of high-frequency decay time to mid-frequency decay time (0.1–2) */
    float reflectionsGain;          /* Linear gain of early reflections (0–3.16) */
    float lateReverbGain;           /* Linear gain of late reverberation (0–10) */
    float lateReverbDelay;          /* Delay of late reverberation relative to early reflections, in seconds (0–0.1) */
    float echoTime;                 /* Cycling time of the echo effect, in seconds (0.075–0.25) */
    float echoDepth;                /* Amplitude of the echo effect (0–1) */
    float modulationTime;           /* Cycling time of the modulation effect, in seconds (0.04–4) */
    float modulationDepth;          /* Amplitude of the modulation effect (0–1) */
    float airAbsorptionGainHF;      /* Linear gain applied per meter of distance for high-frequency air absorption (0.892–1) */
    float hfReference;              /* Reference frequency for high-frequency decay ratio, in Hz (1000–20000) */
    float lfReference;              /* Reference frequency for low-frequency decay ratio, in Hz (20–1000) */
    float roomRolloffFactor;        /* Rolloff factor for reflected sound sources (0–10) */
    int decayHFLimit;               /* Whether to limit high-frequency decay time to the air absorption limit (0 or 1) */

} VAEAXReverb;

// Get the direction that reverb should be heard from for the provided emitter. The emitter must have hasRelativeReverb=true.
// This is a world-space direction (in the world's CoordinateSystem), not listener space, as emitters have no orientation. Use vaWorldConvertWorldToListenerDirection with the listener's rotation to get a listener-space direction for EFX reflections/late reverb pan.
// Returns NULL if eax or emitter is NULL, or no entry exists for this emitter
EXPORT_API VAVector* vaEAXReverbGetRelativeDirection(const VAEAXReverb* eax, const VAEmitter* emitter);

// Get the volume of this reverb preset, relative to the provided emitter. The emitter must have hasRelativeReverb=true.
// Returns NULL if eax or emitter is NULL, or no entry exists for this emitter
EXPORT_API float* vaEAXReverbGetRelativeGain(const VAEAXReverb* eax, const VAEmitter* emitter);

typedef float (*VAEmitterGainFormula)(bool lowFrequency, int occlusionRayCount, int permeationRayCount, int permeationBounceCount, float occlusionEnergy, float permeationEnergy);
typedef void (*VAEmitterOnRaytracingComplete)(VAEmitter* emitter);
typedef void (*VAEmitterOnRaytracedByAnotherEmitter)(struct VAEmitter* source, struct VAEmitter* target);
typedef void (*VAEmitterOnRemoved)(VAEmitter* emitter);
typedef void (*VAEmitterVisualisationCallback)(VAEmitter* emitter, VAVisualisationData* data, int count);

typedef void (*VALogCallback)(const char* message);

// Contains low-frequency and high-frequency gains, in the range 0.0 to 1.0
typedef struct VALowPassFilter
{
    float gainLF;
    float gainHF;
} VALowPassFilter;

// A 32-bit colour with byte-per-channel components. Only used for debug rendering.
typedef struct VAColor
{
    unsigned char r, g, b, a;
} VAColor;

static inline VAColor vaColorCreate(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    VAColor color;
    color.r = r; color.g = g; color.b = b; color.a = a;
    return color;
}

typedef struct VAVisualisationData
{
    VAVector position;
    VAVector normal;
} VAVisualisationData;

// Create a new emitter with default settings
EXPORT_API VAEmitter* vaEmitterCreate(void);

// Free all resources owned by this emitter. After removing an emitter from its world, don't free it until its onRemoved callback has been invoked (see vaEmitterSetOnRemovedCallback), as the raytracing threads may still read it until then.
// Returns VA_INVALID_POINTER if the emitter is NULL, VA_ERROR_IN_USE if the emitter is still added to a world, or was removed but its onRemoved callback hasn't been invoked yet
EXPORT_API VAResult vaEmitterDestroy(VAEmitter* emitter);

// Check if the target has been added to this emitter via vaEmitterAddTarget().
// Returns false if emitter or target is NULL
EXPORT_API bool vaEmitterHasTarget(VAEmitter* emitter, VAEmitter* target);

// Check if the target has been raytraced by this emitter yet.
// Returns false if emitter or target is NULL
EXPORT_API bool vaEmitterHasRaytracedTarget(VAEmitter* emitter, VAEmitter* target);

// Get the low pass filter to be applied to the target emitter.
// Returns NULL if emitter, target is NULL, or if the emitter has not raytraced the target yet
EXPORT_API VALowPassFilter* vaEmitterGetTargetFilter(VAEmitter* emitter, VAEmitter* target);

// Add a target emitter. This emitter will calculate how muffled the target emitter is.
// Returns VA_INVALID_POINTER if emitter or target is NULL, VA_FEATURE_DISABLED if this emitter does not cast occlusion or permeation rays, VA_ALREADY_EXISTS if the target is already a target of this emitter, VA_NOT_ADDED_TO_WORLD if the target was not added to the same world as this emitter
EXPORT_API VAResult vaEmitterAddTarget(VAEmitter* emitter, VAEmitter* target);

// Remove a target emitter.
// Returns VA_INVALID_POINTER if emitter or target is NULL, VA_NOT_FOUND if the emitter does not have this target
EXPORT_API VAResult vaEmitterRemoveTarget(VAEmitter* emitter, VAEmitter* target);

// Invalidate the ray cache. All rays will be re-cast the next time raytracing occurs.
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterResetTrails(VAEmitter* emitter);

// Set the number of reverb rays to cast.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetReverbRayCount(VAEmitter* emitter, int value);

// Get the number of reverb rays to cast.
EXPORT_API int vaEmitterGetReverbRayCount(const VAEmitter* emitter);

// Set the maximum number of bounces per reverb ray. Reverb rays stop bouncing after reaching the edge of the world when vaWorldSetIsIndoors() is false
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetReverbBounceCount(VAEmitter* emitter, int value);

// Get the maximum number of bounces per reverb ray.
EXPORT_API int vaEmitterGetReverbBounceCount(const VAEmitter* emitter);

// Set the number of occlusion rays to cast.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetOcclusionRayCount(VAEmitter* emitter, int value);

// Get the number of occlusion rays to cast.
EXPORT_API int vaEmitterGetOcclusionRayCount(const VAEmitter* emitter);

// Set the maximum number of bounces per occlusion ray. Occlusion rays stop bouncing after they discover all targets
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetOcclusionBounceCount(VAEmitter* emitter, int value);

// Get the maximum number of bounces per occlusion ray.
EXPORT_API int vaEmitterGetOcclusionBounceCount(const VAEmitter* emitter);

// Set the number of permeation rays to cast.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetPermeationRayCount(VAEmitter* emitter, int value);

// Get the number of permeation rays to cast.
EXPORT_API int vaEmitterGetPermeationRayCount(const VAEmitter* emitter);

// Set the number of bounces per permeation ray.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetPermeationBounceCount(VAEmitter* emitter, int value);

// Get the number of bounces per permeation ray.
EXPORT_API int vaEmitterGetPermeationBounceCount(const VAEmitter* emitter);

// Set the number of ambient occlusion rays to cast.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetAmbientOcclusionRayCount(VAEmitter* emitter, int value);

// Get the number of ambient occlusion rays to cast.
EXPORT_API int vaEmitterGetAmbientOcclusionRayCount(const VAEmitter* emitter);

// Set the maximum number of bounces per ambient occlusion ray. Ambient occlusion rays stop bouncing after reaching the edge of the world
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetAmbientOcclusionBounceCount(VAEmitter* emitter, int value);

// Get the maximum number of bounces per ambient occlusion ray.
EXPORT_API int vaEmitterGetAmbientOcclusionBounceCount(const VAEmitter* emitter);

// Set the number of ambient permeation rays to cast.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetAmbientPermeationRayCount(VAEmitter* emitter, int value);

// Get the number of ambient permeation rays to cast.
EXPORT_API int vaEmitterGetAmbientPermeationRayCount(const VAEmitter* emitter);

// Set the number of bounces per ambient permeation ray.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetAmbientPermeationBounceCount(VAEmitter* emitter, int value);

// Get the number of bounces per ambient permeation ray.
EXPORT_API int vaEmitterGetAmbientPermeationBounceCount(const VAEmitter* emitter);

// Set the number of visualisation rays to cast.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetVisualisationRayCount(VAEmitter* emitter, int value);

// Get the number of visualisation rays to cast.
EXPORT_API int vaEmitterGetVisualisationRayCount(const VAEmitter* emitter);

// Set the number of bounces per visualisation ray.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetVisualisationBounceCount(VAEmitter* emitter, int value);

// Get the number of bounces per visualisation ray.
EXPORT_API int vaEmitterGetVisualisationBounceCount(const VAEmitter* emitter);

// Set the interval in milliseconds between visualisation ray updates.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is <= 0
EXPORT_API VAResult vaEmitterSetVisualisationUpdateFrequency(VAEmitter* emitter, int value);

// Get the interval in milliseconds between visualisation ray updates.
// Returns 0 if emitter is NULL
EXPORT_API int vaEmitterGetVisualisationUpdateFrequency(const VAEmitter* emitter);

// Optimisation field - permeation rays will stop bouncing when their energy drops below this threshold.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if value is NaN or infinity, VA_OUT_OF_RANGE if less than 0 or greater than 1
EXPORT_API VAResult vaEmitterSetMinimumPermeationEnergy(VAEmitter* emitter, float value);

// Get the energy threshold below which permeation rays stop bouncing.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetMinimumPermeationEnergy(const VAEmitter* emitter);

// Optimisation field - reverb rays will stop bouncing when their energy drops below this threshold.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if value is NaN or infinity, VA_OUT_OF_RANGE if less than 0 or greater than 1
EXPORT_API VAResult vaEmitterSetMinimumReverbEnergy(VAEmitter* emitter, float value);

// Get the energy threshold below which reverb rays stop bouncing.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetMinimumReverbEnergy(const VAEmitter* emitter);

// Optimisation field - occlusion rays will stop bouncing when their low-frequency energy drops below this threshold.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if value is NaN or infinity, VA_OUT_OF_RANGE if less than 0 or greater than 1
EXPORT_API VAResult vaEmitterSetMinimumOcclusionEnergy(VAEmitter* emitter, float value);

// Get the energy threshold below which occlusion rays stop bouncing.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetMinimumOcclusionEnergy(const VAEmitter* emitter);

// Optimisation field - ambient permeation rays will stop permeating when their energy drops below this threshold.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if value is NaN or infinity, VA_OUT_OF_RANGE if less than 0 or greater than 1
EXPORT_API VAResult vaEmitterSetMinimumAmbientPermeationEnergy(VAEmitter* emitter, float value);

// Get the energy threshold below which ambient permeation rays stop permeating.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetMinimumAmbientPermeationEnergy(const VAEmitter* emitter);

// Optimisation field - ambient occlusion rays will stop bouncing when their low-frequency energy drops below this threshold.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if value is NaN or infinity, VA_OUT_OF_RANGE if less than 0 or greater than 1
EXPORT_API VAResult vaEmitterSetMinimumAmbientOcclusionEnergy(VAEmitter* emitter, float value);

// Get the energy threshold below which ambient occlusion rays stop bouncing.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetMinimumAmbientOcclusionEnergy(const VAEmitter* emitter);

// The loudest volume that this emitter's sound will ever be played at.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if less than 0 or greater than 1
EXPORT_API VAResult vaEmitterSetMaxVolume(VAEmitter* emitter, float value);

// Get the loudest volume that this emitter's sound will ever be played at.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetMaxVolume(const VAEmitter* emitter);

// When defined, rays will be cast in round-robin from these positions rather than the default position.
// Pass NULL or a count of 0 to revert back to using the default position.
// A copy of the provided array will be stored, so you do not need to keep it alive after this call.
// Warning: changing this at runtime will clear the ray cache.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if any position is NaN or infinity (dev build only)
EXPORT_API VAResult vaEmitterSetOverridePositions(VAEmitter* emitter, VAVector* positions, int count);

// When defined, rays will be cast in these directions rather than the default ray directions.
// Pass NULL or a count of 0 to revert back to using the default directions.
// A copy of the provided array will be stored, so you do not need to keep it alive after this call.
// Warning: changing this at runtime will clear the ray cache.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if any direction cannot be normalised (dev build only)
EXPORT_API VAResult vaEmitterSetOverrideRayDirections(VAEmitter* emitter, VAVector* directions, int count);

// Set the custom formula for calculating this emitter's low- and high-frequency gains when it is the target of another emitter. This is invoked for both low-frequency and high-frequency energy. Pass NULL to use the default formula.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if the formula produces NaN or Infinity during validation
EXPORT_API VAResult vaEmitterSetGainFormula(VAEmitter* emitter, VAEmitterGainFormula formula);

// Set the custom formula for calculating the low- and high-frequency gains in for this emitter's ambient filter. This is invoked for both low-frequency and high-frequency energy. Set to NULL to use the default formula.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if the formula produces NaN or Infinity during validation
EXPORT_API VAResult vaEmitterSetAmbientGainFormula(VAEmitter* emitter, VAEmitterGainFormula formula);

// True if reverb ray count and bounce count are both greater than zero. False if emitter is NULL
EXPORT_API bool vaEmitterGetReverbEnabled(const VAEmitter* emitter);

// True if occlusion ray count and bounce count are both greater than zero. False if emitter is NULL
EXPORT_API bool vaEmitterGetOcclusionEnabled(const VAEmitter* emitter);

// True if permeation ray count and bounce count are both greater than zero. False if emitter is NULL
EXPORT_API bool vaEmitterGetPermeationEnabled(const VAEmitter* emitter);

// True if ambient occlusion ray count and bounce count are both greater than zero. False if emitter is NULL
EXPORT_API bool vaEmitterGetAmbientOcclusionEnabled(const VAEmitter* emitter);

// True if ambient permeation ray count and bounce count are both greater than zero. False if emitter is NULL
EXPORT_API bool vaEmitterGetAmbientPermeationEnabled(const VAEmitter* emitter);

// True if this emitter casts any rays (excluding visualisation rays). False if emitter is NULL
EXPORT_API bool vaEmitterGetCreatesTrails(const VAEmitter* emitter);

// True if visualisation ray count and bounce count are both greater than zero. False if emitter is NULL
EXPORT_API bool vaEmitterGetVisualisationEnabled(const VAEmitter* emitter);

// Whether this emitter is within the world bounds.
// Emitters outside the world bounds will not be raytraced. Set vaEmitterSetClampPosition to true to keep this emitter within the world bounds.
// Returns false if emitter is NULL
EXPORT_API bool vaEmitterGetWithinWorldBounds(const VAEmitter* emitter);

// Set whether to render each trail a different colour in the debug window (dev build only).
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetRandomTrailColor(VAEmitter* emitter, bool value);

// Get whether to render each trail a different colour in the debug window (dev build only).
// Returns false if emitter is NULL
EXPORT_API bool vaEmitterGetRandomTrailColor(const VAEmitter* emitter);

// Set the colour of ray trails in the debug window (dev build only).
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetTrailColor(VAEmitter* emitter, VAColor value);

// Get the colour of ray trails in the debug window (dev build only).
// Returns transparent black if emitter is NULL
EXPORT_API VAColor vaEmitterGetTrailColor(const VAEmitter* emitter);

// Set the colour of reverb rays in the debug window (dev build only).
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetReverbColor(VAEmitter* emitter, VAColor value);

// Get the colour of reverb rays in the debug window (dev build only).
// Returns transparent black if emitter is NULL
EXPORT_API VAColor vaEmitterGetReverbColor(const VAEmitter* emitter);

// Set the colour of occlusion rays in the debug window (dev build only).
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetOcclusionColor(VAEmitter* emitter, VAColor value);

// Get the colour of occlusion rays in the debug window (dev build only).
// Returns transparent black if emitter is NULL
EXPORT_API VAColor vaEmitterGetOcclusionColor(const VAEmitter* emitter);

// Set the colour of permeation rays in the debug window (dev build only).
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetPermeationColor(VAEmitter* emitter, VAColor value);

// Get the colour of permeation rays in the debug window (dev build only).
// Returns transparent black if emitter is NULL
EXPORT_API VAColor vaEmitterGetPermeationColor(const VAEmitter* emitter);

// Set the colour of ambient permeation rays in the debug window (dev build only).
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetAmbientPermeationColor(VAEmitter* emitter, VAColor value);

// Get the colour of ambient permeation rays in the debug window (dev build only).
// Returns transparent black if emitter is NULL
EXPORT_API VAColor vaEmitterGetAmbientPermeationColor(const VAEmitter* emitter);

// This callback is invoked when this emitter has cast its reverb and ambient rays for the first time.
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetOnRaytracingCompleteCallback(VAEmitter* emitter, VAEmitterOnRaytracingComplete callback);

// A callback that is invoked when another emitter raytraces this emitter for the first time.
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetOnRaytracedByAnotherEmitterCallback(VAEmitter* emitter, VAEmitterOnRaytracedByAnotherEmitter callback);

// A callback that is invoked once this emitter has been removed from its world and the raytracing threads no longer use it, meaning it's now safe to free via vaEmitterDestroy. Don't free the emitter before this callback is invoked.
// This is invoked inside vaWorldUpdate or vaWorldWait once the next raytracing results are handled, inside vaWorldRemoveEmitter if no raytracing results are pending, or inside vaWorldDestroy.
// If the emitter casts reverb rays and affects grouped EAX, this callback will be delayed until its reverb tail finishes playing (see vaEmitterSetKeepReverbTailAlive).
// The callback is cleared once invoked. If the emitter is re-added to the same world before then, the callback isn't invoked, and is kept for its next removal.
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetOnRemovedCallback(VAEmitter* emitter, VAEmitterOnRemoved callback);

// Invoked with bounce data produced by visualisation rays. Do not modify the data in the visualisation array or access it outside the callback.
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetVisualisationCallback(VAEmitter* emitter, VAEmitterVisualisationCallback callback);

// Invoked for informational log messages from this emitter.
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetLogCallback(VAEmitter* emitter, VALogCallback callback);

// Invoked for error log messages from this emitter.
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetLogErrorCallback(VAEmitter* emitter, VALogCallback callback);

// Set the position of this emitter.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if position is NaN or Infinity
EXPORT_API VAResult vaEmitterSetPosition(VAEmitter* emitter, VAVector position);

// Get the position of this emitter.
// Returns (0, 0, 0) if emitter is NULL
EXPORT_API VAVector vaEmitterGetPosition(const VAEmitter* emitter);

// Returns true if this emitter hasn't cast its own reverb and ambient rays yet.
// Returns false if emitter is NULL
EXPORT_API bool vaEmitterGetInitialising(const VAEmitter* emitter);

// Returns true after vaWorldRemoveEmitter() has been called on this emitter and it is waiting for its reverb tail to finish before actually being removed.
// Returns false if emitter is NULL
EXPORT_API bool vaEmitterGetPendingRemoval(const VAEmitter* emitter);

// Set whether this emitter's EAX is blended into the grouped EAX returned by vaWorldGetGroupedEAX(). Set to false for listener emitters
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetAffectsGroupedEAX(VAEmitter* emitter, bool value);

// Get whether this emitter's EAX is blended into the grouped EAX returned by vaWorldGetGroupedEAX()
EXPORT_API bool vaEmitterGetAffectsGroupedEAX(const VAEmitter* emitter);

// Set whether this emitter is kept alive until its reverb tail finishes playing vaWorldRemoveEmitter() is called.
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetKeepReverbTailAlive(VAEmitter* emitter, bool value);

// Get whether this emitter is kept alive until its reverb tail finishes playing
EXPORT_API bool vaEmitterGetKeepReverbTailAlive(const VAEmitter* emitter);

// Get the index into the world's grouped eax list.
// Returns -1 if emitter is NULL, has affectsGroupedEAX=false, or does not cast reverb rays
EXPORT_API int vaEmitterGetGroupedEAXIndex(const VAEmitter* emitter);

// Get the percentage of energy in ambient occlusion rays that reached the edge of the world.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetOutsidePercent(const VAEmitter* emitter);

// Set whether reverb gain and direction will be calculated relative to this emitter. Set to true for your main listener emitter.
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetHasRelativeReverb(VAEmitter* emitter, bool value);

// Get whether reverb gain and direction will be calculated relative to this emitter.
// Access relative reverb results via vaEAXReverbGetRelativeGain and vaEAXReverbGetRelativeDirection
EXPORT_API bool vaEmitterGetHasRelativeReverb(const VAEmitter* emitter);

// Reverb directionality is calculated in two stages:
// - First the average direction from this emitter to all of another emitter's reverb bounce positions is calculated. This direction is called the 'directional reverb vector', and is returned by vaEAXReverbGetRelativeDirection.
// - Second this vector is normalised using the inner and outer thresholds (see vaEmitterSetRelativeReverbOuterThreshold).
// For reference, a vector of (0, 0, 0) has no direction, so you'll hear the reverb all around you. A vector of (0.5, 0, 0) has reverb mostly coming from the right.
// If the magnitude of the directional reverb vector is lower than this inner threshold, reverb will not be directional (sounds like the reverb is all around you).
// Reduce this value to make it easier to hear directional reverb.
// Increase this value to make it harder to hear directional reverb.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if NaN or infinity
EXPORT_API VAResult vaEmitterSetRelativeReverbInnerThreshold(VAEmitter* emitter, float value);

// Get the inner threshold for directional reverb. See vaEmitterSetRelativeReverbInnerThreshold for more information.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetRelativeReverbInnerThreshold(const VAEmitter* emitter);

// Reverb directionality is calculated in two stages:
// - First the average direction from this emitter to all of another emitter's reverb bounce positions is calculated. This direction is called the 'directional reverb vector', and is returned by vaEAXReverbGetRelativeDirection.
// - Second this vector is normalised using the inner (see vaEmitterSetRelativeReverbInnerThreshold) and outer thresholds.
// For reference, a vector of (0, 0, 0) has no direction, so you'll hear the reverb all around you. A vector of (0.5, 0, 0) has reverb mostly coming from the right.
// If the magnitude of the directional reverb vector is higher than this outer threshold, reverb will be fully directional.
// Reduce this value to make it easier to hear directional reverb.
// Increase this value to make it harder to hear directional reverb.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if NaN or infinity
EXPORT_API VAResult vaEmitterSetRelativeReverbOuterThreshold(VAEmitter* emitter, float value);

// Get the outer threshold for directional reverb. See vaEmitterSetRelativeReverbOuterThreshold for more information.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetRelativeReverbOuterThreshold(const VAEmitter* emitter);

// Set whether to keep this emitter's position inside the world bounds.
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetClampPosition(VAEmitter* emitter, bool value);

// Get whether to keep this emitter's position inside the world bounds.
// Returns false if emitter is NULL
EXPORT_API bool vaEmitterGetClampPosition(const VAEmitter* emitter);

// Set the seed used to randomise scattering vectors.
// Warning: changing this at runtime will clear the ray cache.
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetScatteringSeed(VAEmitter* emitter, int value);

// Get the seed used to randomise scattering vectors.
// Returns 0 if emitter is NULL
EXPORT_API int vaEmitterGetScatteringSeed(const VAEmitter* emitter);

// Set the emitter's name for debugging and logging. The string is copied internally, so the caller does not need to keep it alive after this call.
// Pass NULL to clear the emitter's name.
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetName(VAEmitter* emitter, const char* name);

// Get the emitter's internal copy of the name. The pointer is valid until the next call to vaEmitterSetName or vaEmitterDestroy. Do not free it.
// Returns NULL if emitter is NULL or it has no name set
EXPORT_API const char* vaEmitterGetName(const VAEmitter* emitter);

// Set the user-defined integer tag for categorising emitters
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetType(VAEmitter* emitter, int value);

// Get the user-defined integer tag for categorising emitters
// Returns 0 if emitter is NULL
EXPORT_API int vaEmitterGetType(const VAEmitter* emitter);

// Set the user-defined pointer for associating user data with this emitter. Not used internally and will not be freed by vaEmitterDestroy
// Returns VA_INVALID_POINTER if emitter is NULL
EXPORT_API VAResult vaEmitterSetUserData(VAEmitter* emitter, void* userData);

// Get the user-defined pointer for associating user data with this emitter
// Returns NULL if emitter is NULL or vaEmitterSetUserData was never invoked
EXPORT_API void* vaEmitterGetUserData(const VAEmitter* emitter);

// Get the number of trails that this emitter will create.
// This is the maximum value among all occlusion, permeation, reverb, ambient occlusion and ambient permeation ray counts.
// Returns 0 if emitter is NULL
EXPORT_API int vaEmitterGetTrailCount(const VAEmitter* emitter);

// Get the number of bounces per trail
// This is the maximum value among all occlusion, permeation, reverb, ambient occlusion and ambient permeation bounce counts.
// Returns 0 if emitter is NULL
EXPORT_API int vaEmitterGetTrailBounceCount(const VAEmitter* emitter);

// Set how long (in milliseconds) the echogram records data for. Returning reverb rays after this period will be ignored.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is <= 0
EXPORT_API VAResult vaEmitterSetMaxEchogramTime(VAEmitter* emitter, int value);

// Get how long (in milliseconds) the echogram records data for. 
EXPORT_API int vaEmitterGetMaxEchogramTime(const VAEmitter* emitter);

// Set the length (in milliseconds) of each entry in the echogram.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is <= 0
EXPORT_API VAResult vaEmitterSetEchogramGranularity(VAEmitter* emitter, int value);

// Set the length (in milliseconds) of each entry in the echogram.
EXPORT_API int vaEmitterGetEchogramGranularity(const VAEmitter* emitter);

// Controls the number of trails that are 'refreshed' each frame.
// Refreshing a trail involves re-casting the first ray, and if it hits a different position than last time, the entire trail will be trimmed and recalculated.
// vaEmitterSetRefreshDistanceThreshold controls the allowed distance between new and old bounce positions.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetTrailRefreshCount(VAEmitter* emitter, int value);

// Get the number of trails that are rebuilt from scratch each frame, to prevent staleness when the emitter moves.
// Returns 0 if emitter is NULL
EXPORT_API int vaEmitterGetTrailRefreshCount(const VAEmitter* emitter);

// Set the allowed distance between new and old bounce positions when refreshing trails. See vaEmitterSetTrailRefreshCount for more information.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_OUT_OF_RANGE if value is < 0
EXPORT_API VAResult vaEmitterSetRefreshDistanceThreshold(VAEmitter* emitter, float value);

// Get the allowed distance between new and old bounce positions when refreshing trails. See vaEmitterSetTrailRefreshCount for more information.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetRefreshDistanceThreshold(const VAEmitter* emitter);

// Set the percentage of returning energy required for reverb to be at full volume.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if <= 0 or > 1.0
EXPORT_API VAResult vaEmitterSetReverbEnergyCap(VAEmitter* emitter, float value);

// Get the percentage of returning energy required for reverb to be at full volume
EXPORT_API float vaEmitterGetReverbEnergyCap(const VAEmitter* emitter);

// Set the percentage of occlusion energy required for this emitter to be at full volume. Defaults to 15% of the other emitter's occlusionRayCount.
// This does nothing if a custom gain formula is provided via vaEmitterSetGainFormula.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if < 0
EXPORT_API VAResult vaEmitterSetOcclusionEnergyCap(VAEmitter* emitter, float value);

// Get the percentage of occlusion energy required for this emitter to be at full volume.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetOcclusionEnergyCap(const VAEmitter* emitter);

// Set the percentage of permeation energy required for this emitter to be at full volume. Defaults to 15% of the other emitter's permeationRayCount * permeationBounceCount.
// This does nothing if a custom gain formula is provided via vaEmitterSetGainFormula.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if < 0
EXPORT_API VAResult vaEmitterSetPermeationEnergyCap(VAEmitter* emitter, float value);

// Get the percentage of permeation energy required for this emitter to be at full volume.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetPermeationEnergyCap(const VAEmitter* emitter);

// Set the percentage of ambient occlusion energy required for ambience to be at full volume. Defaults to 15% of this emitter's ambientOcclusionRayCount.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if < 0
EXPORT_API VAResult vaEmitterSetAmbientOcclusionEnergyCap(VAEmitter* emitter, float value);

// Get the percentage of ambient occlusion energy required for ambience to be at full volume.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetAmbientOcclusionEnergyCap(const VAEmitter* emitter);

// Set the percentage of ambient occlusion energy required for ambience to be at full volume. Defaults to 15% of this emitter's ambientPermeationRayCount * ambientPermeationBounceCount.
// Returns VA_INVALID_POINTER if emitter is NULL, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if < 0
EXPORT_API VAResult vaEmitterSetAmbientPermeationEnergyCap(VAEmitter* emitter, float value);

// Get the percentage of ambient permeation energy required for this emitter to be at full volume.
// Returns 0 if emitter is NULL
EXPORT_API float vaEmitterGetAmbientPermeationEnergyCap(const VAEmitter* emitter);

// Get the data gathered by this emitter's reverb rays.
// Returns NULL until this emitter casts its reverb rays for the first time (see vaEmitterGetInitialising), or if emitter is NULL
EXPORT_API VAProcessedReverb* vaEmitterGetProcessedReverb(const VAEmitter* emitter);

// Get this emitter's EAX reverb properties.
// Returns NULL until this emitter casts its reverb rays for the first time (see vaEmitterGetInitialising), or if emitter is NULL
EXPORT_API VAEAXReverb* vaEmitterGetEAX(const VAEmitter* emitter);

// Get the low- and high-frequency volume of ambient sounds.
// Returns NULL until this emitter casts its ambient rays for the first time (see vaEmitterGetInitialising), or if emitter is NULL
EXPORT_API VALowPassFilter* vaEmitterGetAmbientFilter(const VAEmitter* emitter);

// Virtual function table for overriding EAX parameter calculations

// Called once with echogram data before other calculations. Use to pre-compute shared state
typedef void (*CustomEAXFormulas_InitialiseFunc)(VACustomEAXFormulas* formulas, VAProcessedReverb* processed, float totalReturnedEnergyLF, float totalReturnedEnergyHF, float echogramGranularity, float* echogramLF, float* echogramHF, float* echogramAverage, int echogramLength);

// Returns the computed diffusion value (0–1)
typedef float (*CustomEAXFormulas_CalculateDiffusionFunc)(VACustomEAXFormulas* formulas);

// Returns the computed density value (0–1)
typedef float (*CustomEAXFormulas_CalculateDensityFunc)(VACustomEAXFormulas* formulas);

// Returns the reflections delay in seconds. energy_threshold is the peak energy level
typedef float (*CustomEAXFormulas_CalculateReflectionsDelayFunc)(VACustomEAXFormulas* formulas, float energyThreshold);

// Returns the late reverb delay in seconds
typedef float (*CustomEAXFormulas_CalculateLateReverbDelayFunc)(VACustomEAXFormulas* formulas);

// Writes LF and HF gains to out_lf_gain and out_hf_gain. reference_energy is the peak energy
typedef void (*CustomEAXFormulas_CalculateFrequencyGainsFunc)(VACustomEAXFormulas* formulas, float referenceEnergy, float* outLFGain, float* outHFGain);

// Writes early and late gains to out_reflections_gain and out_late_reverb_gain
typedef void (*CustomEAXFormulas_CalculateReflectionsAndLateReverbGainFunc)(VACustomEAXFormulas* formulas, float earlyLateTransitionMs, float referenceEnergy, float* outReflectionsGain, float* outLateReverbGain);

// Returns the RT60 decay time in seconds from the given echogram
typedef float (*CustomEAXFormulas_CalculateRT60Func)(VACustomEAXFormulas* formulas, float* echogram, int echogramLength);

// Estimates RT60 from the time a Schroeder decay curve (in dB) takes to fall from -5dB to -targetDecayDb below peakDb; returns 0 if the span is too short to measure
typedef float (*CustomEAXFormulas_TryCalculateDecayTimeFunc)(VACustomEAXFormulas* formulas, float* echogramDb, int echogramDbLength, int peakIndex, float peakDb, float binDurationMs, float targetDecayDb, float scaleFactor);

struct VACustomEAXFormulas
{
    // Virtual function pointers
    CustomEAXFormulas_InitialiseFunc initialise;
    CustomEAXFormulas_CalculateDiffusionFunc calculateDiffusion;
    CustomEAXFormulas_CalculateDensityFunc calculateDensity;
    CustomEAXFormulas_CalculateReflectionsDelayFunc calculateReflectionsDelay;
    CustomEAXFormulas_CalculateLateReverbDelayFunc calculateLateReverbDelay;
    CustomEAXFormulas_CalculateFrequencyGainsFunc calculateFrequencyGains;
    CustomEAXFormulas_CalculateReflectionsAndLateReverbGainFunc calculateReflectionsAndLateReverbGain;
    CustomEAXFormulas_CalculateRT60Func calculateRt60;
    CustomEAXFormulas_TryCalculateDecayTimeFunc tryCalculateDecayTime;

    // Data members
    float* tempDiffusionCumulative;
    int tempDiffusionCumulativeLength;

    float* echogramDb;
    int echogramDbLength;

    float binDurationMs;

    VAProcessedReverb* processed;
    float totalReturnedEnergyLF;
    float totalReturnedEnergyHF;
    float totalReturnedEnergyAverage;
    float totalReturnedEnergy;
    float* echogramLF;
    float* echogramHF;
    float* echogramAverage;
    int echogramLength;

    float maxEnergy;

    VAWorld* world;
};

// Create a new CustomEAXFormulas with default values.
EXPORT_API VACustomEAXFormulas* vaCustomEAXFormulasCreate(void);

// Free all resources owned by these formulas.
// Returns VA_INVALID_POINTER if formulas is NULL, VA_ERROR_IN_USE if the formulas are still assigned to a world
EXPORT_API VAResult vaCustomEAXFormulasDestroy(VACustomEAXFormulas* formulas);

// Valid value ranges for EAX reverb effect parameters, matching the EFX specification, used when clamping computed values

#define EAX_DENSITY_MIN 0.0f
#define EAX_DENSITY_MAX 1.0f
#define EAX_DIFFUSION_MIN 0.0f
#define EAX_DIFFUSION_MAX 1.0f
#define EAX_GAIN_MIN 0.0f
#define EAX_GAIN_MAX 1.0f
#define EAX_GAIN_LF_MIN 0.0f
#define EAX_GAIN_LF_MAX 1.0f
#define EAX_GAIN_HF_MIN 0.0f
#define EAX_GAIN_HF_MAX 1.0f
#define EAX_DECAY_TIME_MIN 0.1f
#define EAX_DECAY_TIME_MAX 20.0f
#define EAX_DECAY_LF_RATIO_MIN 0.1f
#define EAX_DECAY_LF_RATIO_MAX 2.0f
#define EAX_DECAY_HF_RATIO_MIN 0.1f
#define EAX_DECAY_HF_RATIO_MAX 2.0f
#define EAX_REFLECTIONS_GAIN_MIN 0.0f
#define EAX_REFLECTIONS_GAIN_MAX 3.16f
#define EAX_REFLECTIONS_DELAY_MIN 0.0f
#define EAX_REFLECTIONS_DELAY_MAX 0.3f
#define EAX_LATE_REVERB_GAIN_MIN 0.0f
#define EAX_LATE_REVERB_GAIN_MAX 10.0f
#define EAX_LATE_REVERB_DELAY_MIN 0.0f
#define EAX_LATE_REVERB_DELAY_MAX 0.1f
#define EAX_ECHO_TIME_MIN 0.075f
#define EAX_ECHO_TIME_MAX 0.25f
#define EAX_ECHO_DEPTH_MIN 0.0f
#define EAX_ECHO_DEPTH_MAX 1.0f
#define EAX_MODULATION_TIME_MIN 0.04f
#define EAX_MODULATION_TIME_MAX 4.0f
#define EAX_MODULATION_DEPTH_MIN 0.0f
#define EAX_MODULATION_DEPTH_MAX 1.0f
#define EAX_AIR_ABSORPTION_GAIN_HF_MIN 0.892f
#define EAX_AIR_ABSORPTION_GAIN_HF_MAX 1.0f
#define EAX_LF_REFERENCE_MIN 20.0f
#define EAX_LF_REFERENCE_MAX 1000.0f
#define EAX_HF_REFERENCE_MIN 1000.0f
#define EAX_HF_REFERENCE_MAX 20000.0f
#define EAX_ROOM_ROLLOFF_FACTOR_MIN 0.0f
#define EAX_ROOM_ROLLOFF_FACTOR_MAX 10.0f

#define EAX_DECAY_HF_LIMIT_MIN 0
#define EAX_DECAY_HF_LIMIT_MAX 1

#define REFLECTIONS_PAN_MIN { -1.0f, -1.0f, -1.0f };
#define REFLECTIONS_PAN_MAX { 1.0f, 1.0f, 1.0f };
#define LATE_REVERB_PAN_MIN { -1.0f, -1.0f, -1.0f };
#define LATE_REVERB_PAN_MAX { 1.0f, 1.0f, 1.0f };

// Useful properties produced by reverb rays
typedef struct VAProcessedReverb
{
    float returnedPercent;      // Percentage of energy that returned to the emitter. Calculated as raw.returned_total / (reverb_ray_count * reverb_bounce_count)
    float outsidePercent;       // Percentage of energy that escaped outside. Calculated as raw.outside_total / (reverb_ray_count * reverb_bounce_count)

    float measuredDecayTimeLF;  // Low-frequency reverberation decay time from the echogram, in seconds. Calculated using RT20/RT30/RT60 method
    float measuredDecayTimeHF;  // High-frequency reverberation decay time from the echogram, in seconds. Calculated using RT20/RT30/RT60 method

    float materialRoughness;    // Average roughness/scattering of all surfaces hit by rays

    float materialAbsorptionLF; // Average low-frequency absorption of all surfaces hit by rays
    float materialAbsorptionHF; // Average high-frequency absorption of all surfaces hit by rays
} VAProcessedReverb;

// Get the average of materialAbsorptionLF and materialAbsorptionHF.
// Returns 0 if data is NULL
EXPORT_API float vaProcessedReverbGetMaterialAbsorption(const VAProcessedReverb* data);

// Custom air absorption formula. Takes distance in meters, returns energy loss as a percentage.
typedef float (*VAAirAbsorptionCustomFormula)(float distanceMeters);

// Settings that control how sound energy is absorbed by the air over distance.

// Create a new AirAbsorptionSettings with default values.
EXPORT_API VAAirAbsorptionSettings* vaAirAbsorptionCreate();

// Check if all settings values are within valid ranges.
// Returns VA_INVALID_POINTER if settings is NULL, VA_INVALID_VALUE if any value is NaN or infinity, or VA_OUT_OF_RANGE if any value is out of range
EXPORT_API VAResult vaAirAbsorptionValidate(const VAAirAbsorptionSettings* settings);

// Free all resources owned by this settings object.
// Returns VA_INVALID_POINTER if settings is NULL
EXPORT_API VAResult vaAirAbsorptionDestroy(VAAirAbsorptionSettings* settings);

// Set the world humidity as a percentage.
// Returns VA_INVALID_POINTER if settings is NULL, VA_INVALID_VALUE if value is NaN or infinity, VA_OUT_OF_RANGE if < 0 or > 1, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaAirAbsorptionSetHumidity(VAAirAbsorptionSettings* settings, float value);

// Get the world humidity as a percentage.
// Returns 0 if settings is NULL
EXPORT_API float vaAirAbsorptionGetHumidity(const VAAirAbsorptionSettings* settings);

// Set the world temperature in degrees Celsius (must be > -273.15).
// Returns VA_INVALID_POINTER if settings is NULL, VA_INVALID_VALUE if value is NaN or infinity, VA_OUT_OF_RANGE if <= -273.15, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaAirAbsorptionSetTemperature(VAAirAbsorptionSettings* settings, float value);

// Get the world temperature in degrees Celsius.
// Returns 0 if settings is NULL
EXPORT_API float vaAirAbsorptionGetTemperature(const VAAirAbsorptionSettings* settings);

// Set the atmospheric pressure in Pascals.
// Returns VA_INVALID_POINTER if settings is NULL, VA_INVALID_VALUE if value is NaN or infinity, VA_OUT_OF_RANGE if <= 0, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaAirAbsorptionSetPressure(VAAirAbsorptionSettings* settings, float value);

// Get the atmospheric pressure in Pascals.
// Returns 0 if settings is NULL
EXPORT_API float vaAirAbsorptionGetPressure(const VAAirAbsorptionSettings* settings);

// Set a custom air absorption formula for low frequencies. Takes distance (meters) and returns energy loss as a percentage. Pass NULL to use the default formula
// Returns VA_INVALID_POINTER if settings is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaAirAbsorptionSetCustomFormulaLF(VAAirAbsorptionSettings* settings, VAAirAbsorptionCustomFormula value);

// Get the custom air absorption formula for low frequencies.
// Returns NULL if settings is NULL, or no custom formula was set
EXPORT_API VAAirAbsorptionCustomFormula vaAirAbsorptionGetCustomFormulaLF(const VAAirAbsorptionSettings* settings);

// Set a custom air absorption formula for high frequencies. Takes distance (meters) and returns energy loss as a percentage. Pass NULL to use the default formula
// Returns VA_INVALID_POINTER if settings is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaAirAbsorptionSetCustomFormulaHF(VAAirAbsorptionSettings* settings, VAAirAbsorptionCustomFormula value);

// Get the custom air absorption formula for high frequencies.
// Returns NULL if settings is NULL, or no custom formula was set
EXPORT_API VAAirAbsorptionCustomFormula vaAirAbsorptionGetCustomFormulaHF(const VAAirAbsorptionSettings* settings);

// Finds the candidate most similar to target. outBest is set to the best match.
// Returns the index of the best match, or -1 if target or outBest is NULL, or candidates is NULL or empty (outBest is set to NULL in these cases)
EXPORT_API int vaEAXUtilsFindBestMatch(const VAEAXReverb* target, VAEAXReverb** candidates, int candidateCount, VAEAXReverb** outBest);

// Computes a similarity score between two reverb presets, in the range [0, 1] where 1 means identical.
// Returns 0 if a or b is NULL
EXPORT_API float vaEAXUtilsGetSimilarity(const VAEAXReverb* a, const VAEAXReverb* b);

// Get the number of seconds an emitter's reverb tail remains audible after it stops emitting, used to delay removal from the world.
// maxVolume is the loudest linear volume (0-1) the emitter's dry source is ever played at (see vaEmitterGetMaxVolume).
// Returns 0 if eax is NULL (no reverb has been calculated yet)
EXPORT_API float vaEAXUtilsGetEffectiveTailSeconds(const VAEAXReverb* eax, float maxVolume);

// Create a new world
EXPORT_API VAWorld* vaWorldCreate();

// Create a new world that connects to an already-running debug window on the given host and port (dev build only)
EXPORT_API VAWorld* vaWorldCreateWithNetworking(const char* debugWindowHost, int debugWindowPort);

// Wait for background thread to complete, then free the world. After calling this method, this world cannot be reused.
// Returns VA_INVALID_POINTER if world is NULL, VA_ERROR_IN_USE if called from within a world or emitter callback/
EXPORT_API VAResult vaWorldDestroy(VAWorld* world);

// Update the raytracing simulation. Call this method regularly to process raytracing results and submit new work.
// Does nothing if background raytracing threads are still running. When threads are idle, it:
// - Handles the last raytracing results, updating reverb objects and invoking onRaytracedByAnotherEmitter callbacks
// - Applies new settings and resizes memory buffers if needed
// - Processes new, modified, and removed primitives
// - Starts raytracing again on background threads
// Must be called from the main thread. Calling this more frequently is safe and can reduce latency for emitter updates.
// Returns VA_INVALID_POINTER if world is NULL, VA_STILL_RUNNING if background threads are currently running, VA_SUCCESS if it just handled the results of background threads, applied all new settings/primitives/emitters, then started the background threads again
EXPORT_API VAResult vaWorldUpdate(VAWorld* world);

// Block the calling thread until all background raytracing threads complete, then handles the results (updates reverb objects and invokes onRaytracingComplete and onRaytracedByAnotherEmitter callbacks for each emitter).
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldWait(VAWorld* world);

// When set to true, vaWorldUpdate will stop submitting work to background threads.
// When vaWorldGetThreadsRunning() returns false, it is safe to call vaWorldDestroy()
EXPORT_API VAResult vaWorldSetPendingShutdown(VAWorld* world, bool value);

// Get whether the world will stop submitting work to background threads.
// Returns false if world is NULL
EXPORT_API bool vaWorldGetPendingShutdown(const VAWorld* world);

// Get whether background raytracing threads are currently running.
// Returns false if world is NULL
EXPORT_API bool vaWorldGetThreadsRunning(const VAWorld* world);

// Get the number of rays cast this frame.
// Returns 0 if world is NULL
EXPORT_API int vaWorldGetRaysCastThisFrame(const VAWorld* world);

// Get the list of grouped EAX reverb properties for all emitters. Contains parameters compatible with EAX reverb effects.
// Returns NULL if world is NULL
EXPORT_API const VAEAXReverb** vaWorldGetGroupedEAX(const VAWorld* world);

// Get the current number of grouped EAX effects (may be less than the maximum set via vaWorldSetMaximumGroupedEAXCount).
// Returns 0 if world is NULL
EXPORT_API int vaWorldGetGroupedEAXCount(const VAWorld* world);

// Add a 3D object to the raytracing scene.
// Returns VA_INVALID_POINTER if world or primitive is NULL, VA_INVALID_MATERIAL if the primitive's material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the primitive's material does not exist, VA_ALREADY_EXISTS if the primitive has already been added to this world, VA_WORLD_CONFLICT if it has already been added to another world or is a VAMeshPrimitive created from a VAMesh that's already in use by a different world
EXPORT_API VAResult vaWorldAddPrimitive_(VAWorld* world, void* primitive);

// Remove a 3D object from the raytracing scene.
// Returns VA_INVALID_POINTER if world or primitive is NULL, VA_NOT_ADDED_TO_WORLD if the primitive was not added to this world
EXPORT_API VAResult vaWorldRemovePrimitive_(VAWorld* world, void* primitive);

// Adds a 3D object to the raytracing scene.
// Returns VA_INVALID_POINTER if world or primitive is NULL, VA_INVALID_MATERIAL if the primitive's material is VAMaterialAir, VA_MATERIAL_DOES_NOT_EXIST if the primitive's material does not exist, VA_ALREADY_EXISTS if the primitive has already been added to this world, VA_WORLD_CONFLICT if it has already been added to another world or is a VAMeshPrimitive created from a VAMesh that's already in use by a different world, VA_MISSING_MATERIAL_CALLBACK if it is a VAUnsafeVoxelPrimitive and vaWorldSetUnsafeVoxelMaterialCallbacks hasn't been called
#define vaWorldAddPrimitive(world, p) _Generic((p), \
    VASpherePrimitive*:          vaWorldAddPrimitive_((world), (void*)(p)), \
    VACapsulePrimitive*:         vaWorldAddPrimitive_((world), (void*)(p)), \
    VAConePrimitive*:            vaWorldAddPrimitive_((world), (void*)(p)), \
    VACylinderPrimitive*:        vaWorldAddPrimitive_((world), (void*)(p)), \
    VADiskPrimitive*:            vaWorldAddPrimitive_((world), (void*)(p)), \
    VAHemispherePrimitive*:      vaWorldAddPrimitive_((world), (void*)(p)), \
    VAPlanePrimitive*:           vaWorldAddPrimitive_((world), (void*)(p)), \
    VAPrismPrimitive*:           vaWorldAddPrimitive_((world), (void*)(p)), \
    VARectangularConePrimitive*: vaWorldAddPrimitive_((world), (void*)(p)), \
    VATrianglePrimitive*:        vaWorldAddPrimitive_((world), (void*)(p)), \
    VATriangularConePrimitive*:  vaWorldAddPrimitive_((world), (void*)(p)), \
    VATriangularPrismPrimitive*: vaWorldAddPrimitive_((world), (void*)(p)), \
    VAMeshPrimitive*:            vaWorldAddPrimitive_((world), (void*)(p)), \
    VAVoxelPrimitive*:           vaWorldAddPrimitive_((world), (void*)(p)), \
    VAUnsafeVoxelPrimitive*:     vaWorldAddPrimitive_((world), (void*)(p))  \
)

// Remove a 3D object from the raytracing scene.
// Returns VA_INVALID_POINTER if world or primitive is NULL, VA_NOT_ADDED_TO_WORLD if the primitive was not added to this world
#define vaWorldRemovePrimitive(world, p) _Generic((p), \
    VASpherePrimitive*:          vaWorldRemovePrimitive_((world), (void*)(p)), \
    VACapsulePrimitive*:         vaWorldRemovePrimitive_((world), (void*)(p)), \
    VAConePrimitive*:            vaWorldRemovePrimitive_((world), (void*)(p)), \
    VACylinderPrimitive*:        vaWorldRemovePrimitive_((world), (void*)(p)), \
    VADiskPrimitive*:            vaWorldRemovePrimitive_((world), (void*)(p)), \
    VAHemispherePrimitive*:      vaWorldRemovePrimitive_((world), (void*)(p)), \
    VAPlanePrimitive*:           vaWorldRemovePrimitive_((world), (void*)(p)), \
    VAPrismPrimitive*:           vaWorldRemovePrimitive_((world), (void*)(p)), \
    VARectangularConePrimitive*: vaWorldRemovePrimitive_((world), (void*)(p)), \
    VATrianglePrimitive*:        vaWorldRemovePrimitive_((world), (void*)(p)), \
    VATriangularConePrimitive*:  vaWorldRemovePrimitive_((world), (void*)(p)), \
    VATriangularPrismPrimitive*: vaWorldRemovePrimitive_((world), (void*)(p)), \
    VAMeshPrimitive*:            vaWorldRemovePrimitive_((world), (void*)(p)), \
    VAVoxelPrimitive*:           vaWorldRemovePrimitive_((world), (void*)(p)), \
    VAUnsafeVoxelPrimitive*:     vaWorldRemovePrimitive_((world), (void*)(p))  \
)

// Set the minimum bounds of the world. All rays are confined within the world bounds.
// Emitters outside the world will not be raytraced, and primitives fully outside these bounds will not affect raytracing.
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if position is NaN or Infinity
EXPORT_API VAResult vaWorldSetPosition(VAWorld* world, VAVector position);

// Get the minimum bounds of the world.
// Returns (0, 0, 0) if world is NULL
EXPORT_API VAVector vaWorldGetPosition(const VAWorld* world);

// Set the size of the world bounds. All rays are confined within the world bounds.
// Emitters outside the world will not be raytraced, and primitives fully outside these bounds will not affect raytracing.
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if size is NaN or infinity, VA_OUT_OF_RANGE if size is < 0
EXPORT_API VAResult vaWorldSetSize(VAWorld* world, VAVector size);

// Get the size of the world bounds.
// Returns (0, 0, 0) if world is NULL
EXPORT_API VAVector vaWorldGetSize(const VAWorld* world);

// Set the maximum bounds of the world. All rays are confined within the world bounds.
// Emitters outside the world will not be raytraced, and primitives fully outside these bounds will not affect raytracing.
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if maxBounds is NaN or infinity, VA_OUT_OF_RANGE if maxBounds is less than the world position
EXPORT_API VAResult vaWorldSetMaximumBounds(VAWorld* world, VAVector maxBounds);

// Get the maximum bounds of the world.
// Returns (0, 0, 0) if world is NULL
EXPORT_API VAVector vaWorldGetMaximumBounds(const VAWorld* world);

// Get the number of emitters in this world.
// Returns 0 if world is NULL
EXPORT_API int vaWorldGetEmitterCount(const VAWorld* world);

// Set the world's name, which is displayed in the debug window and log messages. The string is copied internally, so the caller does not need to keep it alive after this call.
// Pass NULL to clear the world's name.
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldSetName(VAWorld* world, const char* name);

// Get the world's internal copy of the name. The pointer is valid until the next call to vaWorldSetName or vaWorldDestroy. Do not free it.
// Returns NULL if world is NULL or it has no name set
EXPORT_API const char* vaWorldGetName(const VAWorld* world);

// Set whether to render the raytracing scene in a debug window (dev build only).
// When enabled, vaWorldUpdate() opens the window (if not already open), polls its input, and renders exactly one frame each call. When disabled, the window is closed.
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldSetRenderingEnabled(VAWorld* world, bool value);

// Get whether the debug render window is enabled.
// Returns false if world is NULL
EXPORT_API bool vaWorldGetRenderingEnabled(const VAWorld* world);

// Get the port used to communicate with the debug window.
// Returns 0 if world is NULL, world was created without a debugWindowPort, or if this is a production builds
EXPORT_API int vaWorldGetDebugWindowPort(const VAWorld* world);

// Set the position of the camera in the debug window (dev build only).
// Returns VA_INVALID_POINTER if world is NULL, VA_FEATURE_DISABLED on a production build (or if the debug debug window failed to spawn).
EXPORT_API VAResult vaWorldSetCameraPosition(VAWorld* world, VAVector position);

// Get the position of the camera in the debug window (dev build only).
// Returns (0, 0, 0) if world is NULL or renderingEnabled is false.
EXPORT_API VAVector vaWorldGetCameraPosition(const VAWorld* world);

// Set the free-fly speed (in units/second) of the camera in the debug window (dev build only).
// Returns VA_INVALID_POINTER if world is NULL, VA_FEATURE_DISABLED on a production build (or if the debug debug window failed to spawn).
EXPORT_API VAResult vaWorldSetCameraSpeed(VAWorld* world, float value);

// Get the free-fly speed (in units/second) of the camera in the debug window (dev build only).
// Returns 0 if world is NULL or renderingEnabled is false.
EXPORT_API float vaWorldGetCameraSpeed(const VAWorld* world);

// Set whether the camera can move freely in the debug window (dev build only).
// Returns VA_INVALID_POINTER if world is NULL, VA_FEATURE_DISABLED on a production build (or if the debug debug window failed to spawn).
EXPORT_API VAResult vaWorldSetManualCamera(VAWorld* world, bool value);

// Get whether the debug window's camera is manually controlled (dev build only).
// Returns false if world is NULL or renderingEnabled is false.
EXPORT_API bool vaWorldGetManualCamera(const VAWorld* world);

// Set whether primitives are rendered in the debug window (dev build only).
// Returns VA_INVALID_POINTER if world is NULL, VA_FEATURE_DISABLED on a production build (or if the debug debug window failed to spawn).
EXPORT_API VAResult vaWorldSetShouldRenderPrimitives(VAWorld* world, bool value);

// Get whether primitives are rendered in the debug window (dev build only).
// Returns false if world is NULL or renderingEnabled is false.
EXPORT_API bool vaWorldGetShouldRenderPrimitives(const VAWorld* world);

// Set whether rays are rendered in the debug window (dev build only).
// Returns VA_INVALID_POINTER if world is NULL, VA_FEATURE_DISABLED on a production build (or if the debug debug window failed to spawn).
EXPORT_API VAResult vaWorldSetShouldRenderRays(VAWorld* world, bool value);

// Get whether rays are rendered in the debug window (dev build only).
// Returns false if world is NULL or renderingEnabled is false.
EXPORT_API bool vaWorldGetShouldRenderRays(const VAWorld* world);

// Set the position of the debug window (dev build only).
// Returns VA_INVALID_POINTER if world is NULL, VA_FEATURE_DISABLED on a production build (or if the debug debug window failed to spawn).
EXPORT_API VAResult vaWorldSetWindowPosition(VAWorld* world, int x, int y);

// Get the position of the debug window (dev build only).
// Returns (0, 0) via outX/outY if world is NULL or renderingEnabled is false.
EXPORT_API void vaWorldGetWindowPosition(const VAWorld* world, int* outX, int* outY);

// Set the size of the debug window (dev build only).
// Returns VA_INVALID_POINTER if world is NULL, VA_FEATURE_DISABLED on a production build (or if the debug debug window failed to spawn).
EXPORT_API VAResult vaWorldSetWindowSize(VAWorld* world, int width, int height);

// Get the size of the debug window (dev build only).
// Returns (0, 0) via outWidth/outHeight if world is NULL or renderingEnabled is false.
EXPORT_API void vaWorldGetWindowSize(const VAWorld* world, int* outWidth, int* outHeight);

// Get the time spent (in milliseconds) taken to render the debug window (dev build only).
// Returns 0 if world is NULL or renderingEnabled is false.
EXPORT_API double vaWorldGetRenderTime(const VAWorld* world);

// Set the maximum number of grouped EAX reverb properties shared across all emitters that affect grouped EAX. Higher values increase accuracy but are more expensive to run.
// Returns VA_INVALID_POINTER if world is NULL, VA_OUT_OF_RANGE if value is less than 1.
EXPORT_API VAResult vaWorldSetMaximumGroupedEAXCount(VAWorld* world, int value);

// Get the maximum number of grouped EAX reverb properties shared across all emitters that affect grouped EAX
// Returns 0 if world is NULL
EXPORT_API int vaWorldGetMaximumGroupedEAXCount(const VAWorld* world);

// Set whether occlusion rays should be affected by the world bounds material. Defaults to false.
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldSetOcclusionRaysLoseEnergyFromWorldBounds(VAWorld* world, bool value);

// Get whether occlusion rays should be affected by the world bounds material
// Returns false if world is NULL
EXPORT_API bool vaWorldGetOcclusionRaysLoseEnergyFromWorldBounds(const VAWorld* world);

// Set the number of work items to distribute raytracing work across. A higher value than you thread count helps evenly distribute work across all threads.
// Returns VA_INVALID_POINTER if world is NULL, VA_OUT_OF_RANGE if less than 1.
EXPORT_API VAResult vaWorldSetWorkItemCount(VAWorld* world, int value);

// Get the number of work items to distribute raytracing work across.
EXPORT_API int vaWorldGetWorkItemCount(const VAWorld* world);

// Set the maximum number of threads that can run in parallel for this world.
// Returns VA_INVALID_POINTER if world is NULL, VA_OUT_OF_RANGE if less than 1
EXPORT_API VAResult vaWorldSetMaximumConcurrencyLevel(VAWorld* world, int value);

// Get the maximum number of threads that can run in parallel for this world.
// Returns 0 if world is NULL
EXPORT_API int vaWorldGetMaximumConcurrencyLevel(const VAWorld* world);

// Set the meters per world unit. Affects air absorption and reverb.
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if <= 0, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetMetersPerUnit(VAWorld* world, float value);

// Get the meters per world unit.
// Returns 0 if world is NULL
EXPORT_API float vaWorldGetMetersPerUnit(const VAWorld* world);

// Set the inverse speed of sound (in seconds per meter, i.e. defaults to 1.0 / 343.0). Affects air absorption and reverb.
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if <= 0, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetInverseSpeedOfSound(VAWorld* world, float value);

// Get the inverse speed of sound.
// Returns 0 if world is NULL
EXPORT_API float vaWorldGetInverseSpeedOfSound(const VAWorld* world);

// Set the low-frequency reference (in Hz). Affects air absorption and reverb.
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if <= 0
EXPORT_API VAResult vaWorldSetReferenceFrequencyLF(VAWorld* world, float value);

// Get the low-frequency reference.
// Returns 0 if world is NULL
EXPORT_API float vaWorldGetReferenceFrequencyLF(const VAWorld* world);

// Set the high-frequency reference (in Hz). Affects air absorption and reverb.
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if <= 0.
EXPORT_API VAResult vaWorldSetReferenceFrequencyHF(VAWorld* world, float value);

// Get the high-frequency reference (in Hz).
// Returns 0 if world is NULL
EXPORT_API float vaWorldGetReferenceFrequencyHF(const VAWorld* world);

// Set the air absorption settings. Pass NULL to disable air absorption.
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if any value is NaN or infinity, VA_OUT_OF_RANGE if humidityPercent is < 0 or > 1, temperatureCelsius is <= -273.15, or pressurePascals is <= 0
EXPORT_API VAResult vaWorldSetAirAbsorption(VAWorld* world, VAAirAbsorptionSettings* value);

// Get the air absorption settings.
// Returns NULL if world is NULL
EXPORT_API void* vaWorldGetAirAbsorption(const VAWorld* world);

// Set the air humidity percentage.
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if NaN/infinity, VA_OUT_OF_RANGE if < 0 or > 1, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetAirAbsorptionHumidity(VAWorld* world, float value);

// Get the air humidity percentage.
// Returns 0 if world is NULL
EXPORT_API float vaWorldGetAirAbsorptionHumidity(const VAWorld* world);

// Set the air temperature (in Celsius).
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if NaN/infinity, VA_OUT_OF_RANGE if <= -273.15, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetAirAbsorptionTemperature(VAWorld* world, float value);

// Get the air temperature.
// Returns 0 if world is NULL
EXPORT_API float vaWorldGetAirAbsorptionTemperature(const VAWorld* world);

// Set the atmospheric pressure (in Pascals).
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if NaN/infinity, VA_OUT_OF_RANGE if <= 0, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetAirAbsorptionPressure(VAWorld* world, float value);

// Get the atmospheric pressure.
// Returns 0 if world is NULL
EXPORT_API float vaWorldGetAirAbsorptionPressure(const VAWorld* world);

// Set a custom air absorption formula for low frequencies. Takes distance (meters) and returns energy loss as a percentage. Pass NULL to use the default formula
// Returns VA_INVALID_POINTER if world is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetAirAbsorptionCustomFormulaLF(VAWorld* world, VAAirAbsorptionCustomFormula value);

// Set a custom air absorption formula for high frequencies. Takes distance (meters) and returns energy loss as a percentage. Pass NULL to use the default formula
// Returns VA_INVALID_POINTER if world is NULL, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetAirAbsorptionCustomFormulaHF(VAWorld* world, VAAirAbsorptionCustomFormula value);

// Set custom formulas for calculating EAX properties (diffusion, density, etc). Pass NULL to use default formulas
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldSetCustomEAXFormulas(VAWorld* world, VACustomEAXFormulas* formulas);

// Set whether emitters outside the world have zero occlusion/permeation energy (true) or maximum energy (false).
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldSetEmittersOutsideTheWorldAreMuffled(VAWorld* world, bool value);

// Get whether emitters outside the world have zero occlusion/permeation energy (true) or maximum energy (false).
// Returns false if world is NULL
EXPORT_API bool vaWorldGetEmittersOutsideTheWorldAreMuffled(const VAWorld* world);

// Set the internal coordinate system. Used when calculating listener-relative reverb directionality with vaWorldCalculateListenerRelativePan().
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldSetCoordinateSystem(VAWorld* world, VACoordinateSystem value);

// Get the internal coordinate system.
// Returns VACoordinateSystemDefault if world is NULL
EXPORT_API VACoordinateSystem vaWorldGetCoordinateSystem(const VAWorld* world);

// Set the multiplier for the size of rendered visualisation rays (dev build only). Defaults to 1.
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if value is NaN or infinity, VA_OUT_OF_RANGE if value is less than 0, VA_FEATURE_DISABLED on non-dev builds
EXPORT_API VAResult vaWorldSetVisualisationScale(VAWorld* world, float value);

// Get the multiplier for the size of rendered visualisation rays.
// Returns 1 if world is NULL or on non-dev builds
EXPORT_API float vaWorldGetVisualisationScale(const VAWorld* world);

// Set the epsilon value used for raytracing and primitive intersections.
// Returns VA_INVALID_POINTER if world is NULL, VA_INVALID_VALUE if value is NaN or Infinity
EXPORT_API VAResult vaWorldSetEpsilon(VAWorld* world, float value);

// Get the epsilon value used for raytracing and primitive intersections.
// Returns 0 if world is NULL
EXPORT_API float vaWorldGetEpsilon(const VAWorld* world);

// Create a new material with default properties.
// Returns VA_INVALID_POINTER if world is NULL, VA_OUT_OF_RANGE if materialId is < 1000 or VA_ALREADY_EXISTS if a material already exists with the provided materialId
EXPORT_API VAResult vaWorldCreateMaterial(VAWorld* world, int materialId);

// Check if this world has a material with the provided id
// Returns false if world is NULL
EXPORT_API bool vaWorldHasMaterial(const VAWorld* world, int materialId);

// Set the percentage of low-frequency energy that is lost when a ray bounces off this material. 
// Returns VA_INVALID_POINTER if world is NULL, VA_MATERIAL_DOES_NOT_EXIST if the material doesn't exist, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if < 0.0 or > 1.0, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetMaterialAbsorptionLF(VAWorld* world, int materialId, float value);

// Get the percentage of low-frequency energy that is lost when a ray bounces off this material.
// Returns 0 if world is NULL
EXPORT_API float vaWorldGetMaterialAbsorptionLF(const VAWorld* world, int materialId);

// Set the percentage of high-frequency energy that is lost when a ray bounces off this material.
// Returns VA_INVALID_POINTER if world is NULL, VA_MATERIAL_DOES_NOT_EXIST if the material doesn't exist, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if < 0.0 or > 1.0, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetMaterialAbsorptionHF(VAWorld* world, int materialId, float value);

// Get the percentage of high-frequency energy that is lost when a ray bounces off this material.
// Returns 0 if world is NULL or the material doesn't exist
EXPORT_API float vaWorldGetMaterialAbsorptionHF(const VAWorld* world, int materialId);

// Set the scattering strength when a ray bounces off this material. 0.0 = no scattering, 1.0 skews the ray reflection direction by up to 90 degrees.
// Returns VA_INVALID_POINTER if world is NULL, VA_MATERIAL_DOES_NOT_EXIST if the material doesn't exist, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if < 0.0 or > 1.0, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetMaterialScattering(VAWorld* world, int materialId, float value);

// Get the scattering strength when a ray bounces off this material.
// Returns 0 if world is NULL or the material doesn't exist
EXPORT_API float vaWorldGetMaterialScattering(const VAWorld* world, int materialId);

// Set how many meters a ray must travel through a primitive before it loses all low-frequency energy.
// Returns VA_INVALID_POINTER if world is NULL, VA_MATERIAL_DOES_NOT_EXIST if the material doesn't exist, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if <= 0, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetMaterialTransmissionLF(VAWorld* world, int materialId, float meters);

// Get how many meters a ray must travel through a primitive before it loses all low-frequency energy.
// Returns 0 if world is NULL or the material doesn't exist
EXPORT_API float vaWorldGetMaterialTransmissionLF(const VAWorld* world, int materialId);

// Set how many meters a ray must travel through a primitive before it loses all high-frequency energy.
// Returns VA_INVALID_POINTER if world is NULL, VA_MATERIAL_DOES_NOT_EXIST if the material doesn't exist, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if <= 0, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetMaterialTransmissionHF(VAWorld* world, int materialId, float meters);

// Get how many meters a ray must travel through a primitive before it loses all high-frequency energy.
// Returns 0 if world is NULL or the material doesn't exist
EXPORT_API float vaWorldGetMaterialTransmissionHF(const VAWorld* world, int materialId);

// Set the percentage of low-frequency energy that is lost when a permeation ray touches a Plane, Disk, Triangle, Line, non-watertight Mesh, non-enclosed Polygon or open Path primitive, instead of calculating how long the ray spent inside it.
// Returns VA_INVALID_POINTER if world is NULL, VA_MATERIAL_DOES_NOT_EXIST if the material doesn't exist, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if < 0.0, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetMaterialFlatTransmissionLF(VAWorld* world, int materialId, float value);

// Get the percentage of low-frequency energy that is lost when a permeation ray touches a Plane, Disk, Triangle, Line, non-watertight Mesh, non-enclosed Polygon or open Path primitive, instead of calculating how long the ray spent inside it.
// Returns 0 if world is NULL or the material doesn't exist
EXPORT_API float vaWorldGetMaterialFlatTransmissionLF(const VAWorld* world, int materialId);

// Set the percentage of high-frequency energy that is lost when a permeation ray touches a Plane, Disk, Triangle, Line, non-watertight Mesh, non-enclosed Polygon or open Path primitive, instead of calculating how long the ray spent inside it.
// Returns VA_INVALID_POINTER if world is NULL, VA_MATERIAL_DOES_NOT_EXIST if the material doesn't exist, VA_INVALID_VALUE if NaN or infinity, VA_OUT_OF_RANGE if < 0.0, VA_UNCHANGED if set to the same value
EXPORT_API VAResult vaWorldSetMaterialFlatTransmissionHF(VAWorld* world, int materialId, float value);

// Get the percentage of high-frequency energy that is lost when a permeation ray touches a Plane, Disk, Triangle, Line, non-watertight Mesh, non-enclosed Polygon or open Path primitive, instead of calculating how long the ray spent inside it.
// Returns 0 if world is NULL
EXPORT_API float vaWorldGetMaterialFlatTransmissionHF(const VAWorld* world, int materialId);

// Get the debug rendering colour for a specific material type (dev build only).
// Returns transparent black (0,0,0,0) if world is NULL, or the material doesn't exist
EXPORT_API VAColor vaWorldGetMaterialColor(const VAWorld* world, int materialId);

// Set the debug rendering colour for a specific material type (dev build only).
// Returns VA_INVALID_POINTER if world is NULL, VA_MATERIAL_DOES_NOT_EXIST if the material doesn't exist
EXPORT_API VAResult vaWorldSetMaterialColor(VAWorld* world, int materialId, VAColor color);

// Get the average time (in milliseconds) spent by vaWorldUpdate() on the main thread.
// Returns 0 if world is NULL
EXPORT_API double vaWorldGetMainThreadTime(const VAWorld* world);

// Get the average time (in milliseconds) spent in background raytracing threads.
// Returns 0 if world is NULL
EXPORT_API double vaWorldGetRaytracingTime(const VAWorld* world);

// Get the average time (in milliseconds) spent in the preparation thread.
// Returns 0 if world is NULL
EXPORT_API double vaWorldGetPreparationTime(const VAWorld* world);

// Get the average time (in milliseconds) spent in the analysis thread.
// Returns 0 if world is NULL
EXPORT_API double vaWorldGetAnalysisTime(const VAWorld* world);

// Get the average time (in milliseconds) between when vaWorldUpdate() is invoked, and when the onReverbUpdated callback is invoked.
// Returns 0 if world is NULL
EXPORT_API double vaWorldGetLatency(const VAWorld* world);

// Get the average time (in milliseconds) between submitting work to the background thread pool and the first worker thread waking up.
// High values indicate thread wake-up / scheduling latency rather than raytracing work itself.
// Returns 0 if world is NULL
EXPORT_API double vaWorldGetSubmitToWakeTime(const VAWorld* world);

// Get the average time (in milliseconds) between the first worker thread waking up and the preparation work item (BVH build) finishing.
// Returns 0 if world is NULL
EXPORT_API double vaWorldGetWakeToFanoutTime(const VAWorld* world);

// Get the average time (in milliseconds) between the preparation work item fanning out emitter work, and the last fanned-out worker thread actually waking up to start it.
// High values indicate OS scheduling/wake latency across multiple threads, not time spent doing raytracing work.
// Returns 0 if world is NULL
EXPORT_API double vaWorldGetFanoutToLastWakeTime(const VAWorld* world);

// Get the average time (in milliseconds) between the last fanned-out worker thread waking up and the last emitter/visualisation work item completing.
// This isolates actual raytracing/visualisation work and work-queue contention from thread wake-up latency.
// Returns 0 if world is NULL
EXPORT_API double vaWorldGetLastWakeToWorkTime(const VAWorld* world);

// Get the average time (in milliseconds) spent executing the completion work item (reverb analysis) itself.
// Returns 0 if world is NULL
EXPORT_API double vaWorldGetCompletionWorkTime(const VAWorld* world);

// Add an emitter to this world.
// Returns VA_INVALID_POINTER if world or emitter is NULL, VA_ALREADY_EXISTS if already added to this world, VA_WORLD_CONFLICT if already added to another world, or removed from another world but its onRemoved callback hasn't been invoked yet
EXPORT_API VAResult vaWorldAddEmitter(VAWorld* world, VAEmitter* emitter);

// Remove an emitter from this world. This emitter's OnRaytracingComplete callback will not be invoked. Don't free the emitter with vaEmitterDestroy until its onRemoved callback has been invoked (see vaEmitterSetOnRemovedCallback).
// Returns VA_INVALID_POINTER if world or emitter is NULL, VA_NOT_FOUND if not found, VA_PENDING_REMOVAL if it will be removed when its reverb tail has finished playing, or if called from within a world or emitter callback (e.g. during vaWorldUpdate or onRaytracedByAnotherEmitter)
EXPORT_API VAResult vaWorldRemoveEmitter(VAWorld* world, VAEmitter* emitter);

// Checks if a world has an emitter
// Returns VA_INVALID_POINTER if world or emitter is NULL, VA_PENDING_REMOVAL if the emitter will be removed once its reverb tail has finished playing, VA_TRUE if it has the emitter, VA_FALSE if it does not have the emitter
EXPORT_API VAResult vaWorldHasEmitter(const VAWorld* world, const VAEmitter* emitter);

// Set custom user data for this world. Not used internally.
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldSetUserData(VAWorld* world, void* userData);

// Get the custom user data for this world.
// Returns NULL if world is NULL
EXPORT_API void* vaWorldGetUserData(const VAWorld* world);

// Set a callback to be invoked right after the background threads finish running.
// At this point, the results of the background threads have not been processed yet (EAX is not updated yet, low pass filters not updated, ambience not updated, etc).
// After this, onReverbUpdated will be invoked next.
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldSetOnRaytracingCompletedCallback(VAWorld* world, void (*callback)(VAWorld* world));

// Set a callback to be invoked after EAX reverb results are updated.
// After this, each emitter's callbacks will be invoked, and then onRaytracingResultsHandled will be invoked next.
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldSetOnReverbUpdatedCallback(VAWorld* world, void (*callback)(VAWorld* world));

// Set a callback to be invoked after all raytracing results (EAX, filters, etc) have been processed.
// After this, new emitter/primitive settings will be applied, and the background threads will start again.
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldSetOnRaytracingResultsHandledCallback(VAWorld* world, void (*callback)(VAWorld* world));

// Set a custom callback for text logs
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldSetLogCallback(VAWorld* world, VALogCallback callback);

// Set whether vaWorldSetLogCallback is invoked each time memory is allocated
// Returns VA_INVALID_POINTER if world is NULL
EXPORT_API VAResult vaWorldSetLogMemoryAllocationWarnings(VAWorld* world, bool value);

// Get whether to vaWorldSetLogCallback is invoked each time memory is allocated
// Returns false if world is NULL
EXPORT_API bool vaWorldGetLogMemoryAllocationWarnings(const VAWorld* world);

// Export all world settings, materials, primitives, and emitters to a binary file (dev build only).
// Returns VA_INVALID_POINTER if world or fileName is NULL, VA_ERROR_FILE_OPEN if the file could not be opened, VA_ERROR_FILE_WRITE if the file could not be written
EXPORT_API VAResult vaWorldExport(VAWorld* world, const char* fileName);

// Import world settings, materials, primitives, and emitters from a binary file produced by vaWorldExport() (dev build only).
// Existing primitives and emitters are not removed before importing.
// outEmitters: set to a malloc'd array of pointers to each added emitter — free with vaWorldImportFreeEmitters() (may be NULL).
// outEmitterCount: number of emitters added (may be NULL).
// Returns VA_INVALID_POINTER if world or fileName is NULL, VA_ERROR_FILE_OPEN if the file could not be opened, VA_ERROR_FILE_VERSION or VA_WRONG_DIMENSION if the file was created by another version of Vercidium Audio, VA_ERROR_FILE_CORRUPT if it failed to load any part of the file
EXPORT_API VAResult vaWorldImport(VAWorld* world, const char* fileName, VAEmitter*** outEmitters, int* outEmitterCount);

// Frees the emitter array allocated by vaWorldImport() via its outEmitters parameter. Does not free the emitters themselves, which are owned by the world (dev build only).
// Returns VA_INVALID_POINTER if emitters is NULL
EXPORT_API VAResult vaWorldImportFreeEmitters(VAEmitter** emitters);

#ifdef __cplusplus
}
#endif
