// -----------------------------------------------------------------------------
// Scene Inputs And Shared Types
// -----------------------------------------------------------------------------

cbuffer SceneConstants : register(b0)
{
    float4x4 inverseViewProjection;
    float4 cameraWorldPosition;
    float4 fieldOriginAndVoxelSize;
    uint4 fieldInfo;
    uint4 selectionInfo;
    uint4 refinementInfo;
};

struct FieldCell
{
    float columnHeightVoxels;
    uint materialId;
    uint isHomesteadPad;
    uint isGardenBed;
    float soilMoisture;
    float fertility;
    float sunlight;
    float weedPressure;
    float coarseFullHeightVoxels;
};

StructuredBuffer<FieldCell> fieldCells : register(t0);
StructuredBuffer<int> refinedPatchLookup : register(t1);

struct RefinedPatchMetadata
{
    uint heightOffset;
    uint materialId;
    uint isHomesteadPad;
    uint isGardenBed;
    float soilMoisture;
    float fertility;
    float sunlight;
    float weedPressure;
    float coarseFullHeightVoxels;
    float patchMaxHeightVoxels;
    float padding0;
    float padding1;
};

StructuredBuffer<RefinedPatchMetadata> refinedPatchMetadata : register(t2);
StructuredBuffer<uint> refinedPatchHeights : register(t3);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

struct TraceHitInfo
{
    uint hit;
    float3 position;
    float3 normal;
    float distance;
    int cellX;
    int cellZ;
};

static const uint display_mode_coarse = 0u;
static const uint display_mode_refined = 1u;
static const uint display_mode_hybrid = 2u;

struct CameraRay
{
    float3 origin;
    float3 direction;
};

// -----------------------------------------------------------------------------
// Vertex Stage
// -----------------------------------------------------------------------------

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    const float2 clip = float2(
        vertexId == 2 ? 3.0 : -1.0,
        vertexId == 1 ? 3.0 : -1.0);

    VSOutput output;
    output.position = float4(clip, 0.0, 1.0);
    output.uv = float2((clip.x + 1.0) * 0.5, (1.0 - clip.y) * 0.5);
    return output;
}

// -----------------------------------------------------------------------------
// Ray Setup
// -----------------------------------------------------------------------------

CameraRay BuildCameraRay(VSOutput input)
{
    const float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    float4 farWorld = mul(float4(ndc, 1.0, 1.0), inverseViewProjection);
    farWorld.xyz /= farWorld.w;

    CameraRay ray;
    ray.origin = cameraWorldPosition.xyz;
    ray.direction = normalize(farWorld.xyz - ray.origin);
    return ray;
}

float3 SkyColor(float3 rayDirection)
{
    const float3 horizonColor = float3(0.61, 0.71, 0.79);
    const float3 skyColor = float3(0.82, 0.90, 0.98);
    const float skyBlend = saturate(rayDirection.y * 0.5 + 0.5);
    return lerp(horizonColor, skyColor, skyBlend);
}

bool IntersectAabb(float3 origin, float3 direction, float3 minimum, float3 maximum, out float tEnter, out float tExit)
{
    const float3 t0 = (minimum - origin) / direction;
    const float3 t1 = (maximum - origin) / direction;
    const float3 tSmall = min(t0, t1);
    const float3 tLarge = max(t0, t1);

    tEnter = max(max(tSmall.x, tSmall.y), tSmall.z);
    tExit = min(min(tLarge.x, tLarge.y), tLarge.z);
    return tExit >= max(tEnter, 0.0);
}

// -----------------------------------------------------------------------------
// Field Data Helpers
// -----------------------------------------------------------------------------

TraceHitInfo MakeTraceMiss()
{
    TraceHitInfo miss;
    miss.hit = 0;
    miss.position = float3(0.0, 0.0, 0.0);
    miss.normal = float3(0.0, 1.0, 0.0);
    miss.distance = 0.0;
    miss.cellX = 0;
    miss.cellZ = 0;
    return miss;
}

TraceHitInfo MakeTraceHit(float3 hitPosition, float3 hitNormal, float hitDistance, int cellX, int cellZ)
{
    TraceHitInfo hit;
    hit.hit = 1;
    hit.position = hitPosition;
    hit.normal = hitNormal;
    hit.distance = hitDistance;
    hit.cellX = cellX;
    hit.cellZ = cellZ;
    return hit;
}

uint PatchResolution()
{
    return max(refinementInfo.x, 1u);
}

float FineVoxelSize(float coarseVoxelSize)
{
    return coarseVoxelSize / PatchResolution();
}

float3 FieldMinimum(float voxelSize)
{
    return float3(fieldOriginAndVoxelSize.x, 0.0, fieldOriginAndVoxelSize.y);
}

float3 FieldMaximum(float voxelSize, float maxColumnHeight)
{
    return float3(
        fieldOriginAndVoxelSize.x + fieldInfo.x * voxelSize,
        maxColumnHeight,
        fieldOriginAndVoxelSize.y + fieldInfo.y * voxelSize);
}

float ColumnBaseHeight(FieldCell cell, float voxelSize, uint displayMode)
{
    return displayMode == display_mode_refined ? cell.coarseFullHeightVoxels * voxelSize : 0.0;
}

float3 MaterialColor(FieldCell cell, int cellX, int cellZ);
float3 ApplyDisplayModeStyling(float3 color, FieldCell cell, float3 hitPosition, float3 normal, float voxelSize, uint displayMode);

float LocalPatchColumnTop(RefinedPatchMetadata patch, int localX, int localZ)
{
    const uint patchResolution = PatchResolution();
    const uint heightIndex = patch.heightOffset + (uint)localX + (uint)localZ * patchResolution;
    return refinedPatchHeights[heightIndex] * FineVoxelSize(fieldOriginAndVoxelSize.z);
}

// -----------------------------------------------------------------------------
// Traversal
// -----------------------------------------------------------------------------

float GridEdgeFactor(float coordinate, float cellSize, float lineWidth)
{
    const float cellCoordinate = frac(coordinate / cellSize);
    const float edgeDistance = min(cellCoordinate, 1.0 - cellCoordinate) * cellSize;
    return 1.0 - smoothstep(0.0, lineWidth, edgeDistance);
}

float3 EstimateFinePatchNormal(int coarseCellX, int coarseCellZ, int localX, int localZ, float coarseVoxelSize)
{
    const uint fieldWidth = fieldInfo.x;
    const uint patchResolution = PatchResolution();
    const float fineVoxelSize = FineVoxelSize(coarseVoxelSize);
    const int patchIndex = refinedPatchLookup[coarseCellX + coarseCellZ * fieldWidth];
    const RefinedPatchMetadata patch = refinedPatchMetadata[patchIndex];
    const float patchBase = patch.coarseFullHeightVoxels * fineVoxelSize;

    const int leftX = max(localX - 1, 0);
    const int rightX = min(localX + 1, (int)patchResolution - 1);
    const int backZ = max(localZ - 1, 0);
    const int frontZ = min(localZ + 1, (int)patchResolution - 1);

    const float leftHeight = LocalPatchColumnTop(patch, leftX, localZ);
    const float rightHeight = LocalPatchColumnTop(patch, rightX, localZ);
    const float backHeight = LocalPatchColumnTop(patch, localX, backZ);
    const float frontHeight = LocalPatchColumnTop(patch, localX, frontZ);

    const float normalizedLeftHeight = leftHeight > patchBase ? leftHeight : patchBase;
    const float normalizedRightHeight = rightHeight > patchBase ? rightHeight : patchBase;
    const float normalizedBackHeight = backHeight > patchBase ? backHeight : patchBase;
    const float normalizedFrontHeight = frontHeight > patchBase ? frontHeight : patchBase;
    return normalize(float3(
        normalizedLeftHeight - normalizedRightHeight,
        fineVoxelSize * 2.0,
        normalizedBackHeight - normalizedFrontHeight));
}

bool TraceRefinedPatchClosestHit(
    int coarseCellX,
    int coarseCellZ,
    float3 rayOrigin,
    float3 rayDirection,
    float coarseVoxelSize,
    float coarseCellEntryT,
    float coarseCellExitT,
    out TraceHitInfo hit)
{
    hit = MakeTraceMiss();
    const uint fieldWidth = fieldInfo.x;
    const int patchIndex = refinedPatchLookup[coarseCellX + coarseCellZ * fieldWidth];
    if (patchIndex < 0)
    {
        return false;
    }

    const RefinedPatchMetadata patch = refinedPatchMetadata[patchIndex];
    const uint patchResolution = PatchResolution();
    const float fineVoxelSize = FineVoxelSize(coarseVoxelSize);
    const float patchBase = patch.coarseFullHeightVoxels * fineVoxelSize;
    const float patchTop = patch.patchMaxHeightVoxels * fineVoxelSize;

    const float yAtEntry = rayOrigin.y + coarseCellEntryT * rayDirection.y;
    const float yAtExit = rayOrigin.y + coarseCellExitT * rayDirection.y;
    const float segmentMinY = min(yAtEntry, yAtExit);
    const float segmentMaxY = max(yAtEntry, yAtExit);
    if (patchTop <= patchBase + 0.0001 || segmentMaxY < patchBase || segmentMinY > patchTop)
    {
        return false;
    }

    const float cellMinX = fieldOriginAndVoxelSize.x + coarseCellX * coarseVoxelSize;
    const float cellMinZ = fieldOriginAndVoxelSize.y + coarseCellZ * coarseVoxelSize;
    float currentT = max(coarseCellEntryT, 0.0);
    const float3 currentPosition = rayOrigin + rayDirection * (currentT + 0.0005);
    int localX = clamp((int)floor((currentPosition.x - cellMinX) / fineVoxelSize), 0, (int)patchResolution - 1);
    int localZ = clamp((int)floor((currentPosition.z - cellMinZ) / fineVoxelSize), 0, (int)patchResolution - 1);

    const int stepX = rayDirection.x > 0.0 ? 1 : (rayDirection.x < 0.0 ? -1 : 0);
    const int stepZ = rayDirection.z > 0.0 ? 1 : (rayDirection.z < 0.0 ? -1 : 0);
    const float largeT = 1e20;
    float tMaxX = largeT;
    float tMaxZ = largeT;
    float tDeltaX = largeT;
    float tDeltaZ = largeT;

    if (stepX != 0)
    {
        const float nextBoundaryX = cellMinX + (stepX > 0 ? (localX + 1) : localX) * fineVoxelSize;
        tMaxX = (nextBoundaryX - rayOrigin.x) / rayDirection.x;
        tDeltaX = fineVoxelSize / abs(rayDirection.x);
    }

    if (stepZ != 0)
    {
        const float nextBoundaryZ = cellMinZ + (stepZ > 0 ? (localZ + 1) : localZ) * fineVoxelSize;
        tMaxZ = (nextBoundaryZ - rayOrigin.z) / rayDirection.z;
        tDeltaZ = fineVoxelSize / abs(rayDirection.z);
    }

    const uint maxSteps = patchResolution * 2 + 8;
    [loop]
    for (uint iteration = 0; iteration < maxSteps; ++iteration)
    {
        if (localX < 0 || localX >= (int)patchResolution || localZ < 0 || localZ >= (int)patchResolution)
        {
            break;
        }

        const float cellExitT = min(min(tMaxX, tMaxZ), coarseCellExitT);
        const float columnTop = LocalPatchColumnTop(patch, localX, localZ);

        if (columnTop > patchBase + 0.0001)
        {
            const float localYAtEntry = rayOrigin.y + currentT * rayDirection.y;
            const float localYAtExit = rayOrigin.y + cellExitT * rayDirection.y;
            const float localSegmentMinY = min(localYAtEntry, localYAtExit);
            const float localSegmentMaxY = max(localYAtEntry, localYAtExit);

            if (localSegmentMaxY >= patchBase && localSegmentMinY <= columnTop)
            {
                float hitT = currentT;

                if (rayDirection.y > 0.0 && localYAtEntry < patchBase)
                {
                    hitT = max(hitT, (patchBase - rayOrigin.y) / rayDirection.y);
                }

                if (rayDirection.y < 0.0 && localYAtEntry > columnTop)
                {
                    hitT = max(hitT, (columnTop - rayOrigin.y) / rayDirection.y);
                }

                if (hitT <= cellExitT + 0.0001)
                {
                    const float3 hitPosition = rayOrigin + rayDirection * hitT;
                    const float topEpsilon = fineVoxelSize * 0.08;
                    float3 hitNormal = float3(0.0, 1.0, 0.0);
                    if (abs(hitPosition.y - columnTop) <= topEpsilon)
                    {
                        hitNormal = EstimateFinePatchNormal(coarseCellX, coarseCellZ, localX, localZ, coarseVoxelSize);
                    }
                    else
                    {
                        const float localMinX = cellMinX + localX * fineVoxelSize;
                        const float localMaxX = localMinX + fineVoxelSize;
                        const float localMinZ = cellMinZ + localZ * fineVoxelSize;
                        const float localMaxZ = localMinZ + fineVoxelSize;
                        const float leftDistance = abs(hitPosition.x - localMinX);
                        const float rightDistance = abs(hitPosition.x - localMaxX);
                        const float backDistance = abs(hitPosition.z - localMinZ);
                        const float frontDistance = abs(hitPosition.z - localMaxZ);

                        float smallestDistance = leftDistance;
                        hitNormal = float3(-1.0, 0.0, 0.0);

                        if (rightDistance < smallestDistance)
                        {
                            smallestDistance = rightDistance;
                            hitNormal = float3(1.0, 0.0, 0.0);
                        }

                        if (backDistance < smallestDistance)
                        {
                            smallestDistance = backDistance;
                            hitNormal = float3(0.0, 0.0, -1.0);
                        }

                        if (frontDistance < smallestDistance)
                        {
                            hitNormal = float3(0.0, 0.0, 1.0);
                        }
                    }

                    hit = MakeTraceHit(hitPosition, hitNormal, hitT, coarseCellX, coarseCellZ);
                    return true;
                }
            }
        }

        if (tMaxX < tMaxZ)
        {
            currentT = tMaxX;
            tMaxX += tDeltaX;
            localX += stepX;
        }
        else
        {
            currentT = tMaxZ;
            tMaxZ += tDeltaZ;
            localZ += stepZ;
        }

        if (currentT > coarseCellExitT)
        {
            break;
        }
    }

    return false;
}

float3 EstimateSurfaceNormal(int cellX, int cellZ, float3 hitPosition, float columnTop, float voxelSize)
{
    const uint fieldWidth = fieldInfo.x;
    const uint fieldDepth = fieldInfo.y;
    const float topEpsilon = voxelSize * 0.08;
    float3 normal = float3(0.0, 1.0, 0.0);

    if (abs(hitPosition.y - columnTop) <= topEpsilon)
    {
        const int leftX = max(cellX - 1, 0);
        const int rightX = min(cellX + 1, (int)fieldWidth - 1);
        const int backZ = max(cellZ - 1, 0);
        const int frontZ = min(cellZ + 1, (int)fieldDepth - 1);

        const float leftHeight = fieldCells[leftX + cellZ * fieldWidth].columnHeightVoxels * voxelSize;
        const float rightHeight = fieldCells[rightX + cellZ * fieldWidth].columnHeightVoxels * voxelSize;
        const float backHeight = fieldCells[cellX + backZ * fieldWidth].columnHeightVoxels * voxelSize;
        const float frontHeight = fieldCells[cellX + frontZ * fieldWidth].columnHeightVoxels * voxelSize;
        normal = normalize(float3(leftHeight - rightHeight, voxelSize * 2.0, backHeight - frontHeight));
    }

    else
    {
        const float cellMinX = fieldOriginAndVoxelSize.x + cellX * voxelSize;
        const float cellMaxX = cellMinX + voxelSize;
        const float cellMinZ = fieldOriginAndVoxelSize.y + cellZ * voxelSize;
        const float cellMaxZ = cellMinZ + voxelSize;

        const float leftDistance = abs(hitPosition.x - cellMinX);
        const float rightDistance = abs(hitPosition.x - cellMaxX);
        const float backDistance = abs(hitPosition.z - cellMinZ);
        const float frontDistance = abs(hitPosition.z - cellMaxZ);

        float smallestDistance = leftDistance;
        normal = float3(-1.0, 0.0, 0.0);

        if (rightDistance < smallestDistance)
        {
            smallestDistance = rightDistance;
            normal = float3(1.0, 0.0, 0.0);
        }

        if (backDistance < smallestDistance)
        {
            smallestDistance = backDistance;
            normal = float3(0.0, 0.0, -1.0);
        }

        if (frontDistance < smallestDistance)
        {
            normal = float3(0.0, 0.0, 1.0);
        }
    }

    return normal;
}

TraceHitInfo TraceFieldClosestHit(float3 rayOrigin, float3 rayDirection, float voxelSize, float maxDistance, uint displayMode)
{
    TraceHitInfo miss = MakeTraceMiss();
    const uint fieldWidth = fieldInfo.x;
    const uint fieldDepth = fieldInfo.y;
    const float maxColumnHeight = fieldOriginAndVoxelSize.w * voxelSize;
    const float3 fieldMin = FieldMinimum(voxelSize);
    const float3 fieldMax = FieldMaximum(voxelSize, maxColumnHeight);

    float tEnter = 0.0;
    float tExit = 0.0;
    if (!IntersectAabb(rayOrigin, rayDirection, fieldMin, fieldMax, tEnter, tExit))
    {
        return miss;
    }

    float currentT = max(tEnter, 0.0);
    const float clippedExit = min(tExit, maxDistance);
    if (currentT > clippedExit)
    {
        return miss;
    }

    float3 currentPosition = rayOrigin + rayDirection * (currentT + 0.0005);
    int cellX = clamp((int)floor((currentPosition.x - fieldOriginAndVoxelSize.x) / voxelSize), 0, (int)fieldWidth - 1);
    int cellZ = clamp((int)floor((currentPosition.z - fieldOriginAndVoxelSize.y) / voxelSize), 0, (int)fieldDepth - 1);

    const int stepX = rayDirection.x > 0.0 ? 1 : (rayDirection.x < 0.0 ? -1 : 0);
    const int stepZ = rayDirection.z > 0.0 ? 1 : (rayDirection.z < 0.0 ? -1 : 0);
    const float largeT = 1e20;
    float tMaxX = largeT;
    float tMaxZ = largeT;
    float tDeltaX = largeT;
    float tDeltaZ = largeT;

    if (stepX != 0)
    {
        const float nextBoundaryX = fieldOriginAndVoxelSize.x + (stepX > 0 ? (cellX + 1) : cellX) * voxelSize;
        tMaxX = (nextBoundaryX - rayOrigin.x) / rayDirection.x;
        tDeltaX = voxelSize / abs(rayDirection.x);
    }

    if (stepZ != 0)
    {
        const float nextBoundaryZ = fieldOriginAndVoxelSize.y + (stepZ > 0 ? (cellZ + 1) : cellZ) * voxelSize;
        tMaxZ = (nextBoundaryZ - rayOrigin.z) / rayDirection.z;
        tDeltaZ = voxelSize / abs(rayDirection.z);
    }

    const uint maxSteps = fieldWidth + fieldDepth + 8;

    [loop]
    for (uint iteration = 0; iteration < maxSteps; ++iteration)
    {
        if (cellX < 0 || cellX >= (int)fieldWidth || cellZ < 0 || cellZ >= (int)fieldDepth)
        {
            break;
        }

        const FieldCell cell = fieldCells[cellX + cellZ * fieldWidth];
        const float cellExitT = min(min(tMaxX, tMaxZ), clippedExit);

        if (displayMode == display_mode_refined)
        {
            TraceHitInfo refinedHit;
            if (TraceRefinedPatchClosestHit(
                    cellX,
                    cellZ,
                    rayOrigin,
                    rayDirection,
                    voxelSize,
                    currentT,
                    cellExitT,
                    refinedHit))
            {
                return refinedHit;
            }
        }
        else if (displayMode == display_mode_hybrid)
        {
            TraceHitInfo refinedHit = MakeTraceMiss();
            const bool hasRefinedHit = TraceRefinedPatchClosestHit(
                cellX,
                cellZ,
                rayOrigin,
                rayDirection,
                voxelSize,
                currentT,
                cellExitT,
                refinedHit);

            const float coarseColumnTop = cell.coarseFullHeightVoxels * voxelSize;
            TraceHitInfo coarseHit = MakeTraceMiss();
            if (coarseColumnTop > 0.0001)
            {
                const float yAtEntry = rayOrigin.y + currentT * rayDirection.y;
                const float yAtExit = rayOrigin.y + cellExitT * rayDirection.y;
                const float segmentMinY = min(yAtEntry, yAtExit);
                const float segmentMaxY = max(yAtEntry, yAtExit);

                if (segmentMaxY >= 0.0 && segmentMinY <= coarseColumnTop)
                {
                    float hitT = currentT;

                    if (rayDirection.y > 0.0 && yAtEntry < 0.0)
                    {
                        hitT = max(hitT, (0.0 - rayOrigin.y) / rayDirection.y);
                    }

                    if (rayDirection.y < 0.0 && yAtEntry > coarseColumnTop)
                    {
                        hitT = max(hitT, (coarseColumnTop - rayOrigin.y) / rayDirection.y);
                    }

                    if (hitT <= cellExitT + 0.0001)
                    {
                        const float3 hitPosition = rayOrigin + rayDirection * hitT;
                        const float3 hitNormal = EstimateSurfaceNormal(cellX, cellZ, hitPosition, coarseColumnTop, voxelSize);
                        coarseHit = MakeTraceHit(hitPosition, hitNormal, hitT, cellX, cellZ);
                    }
                }
            }

            if (hasRefinedHit && coarseHit.hit != 0)
            {
                if (refinedHit.distance <= coarseHit.distance)
                {
                    return refinedHit;
                }

                return coarseHit;
            }

            if (hasRefinedHit)
            {
                return refinedHit;
            }

            if (coarseHit.hit != 0)
            {
                return coarseHit;
            }
        }
        else
        {
            const float columnBase = ColumnBaseHeight(cell, voxelSize, displayMode);
            const float columnTop = cell.columnHeightVoxels * voxelSize;

            if (columnTop > columnBase + 0.0001)
            {
                const float yAtEntry = rayOrigin.y + currentT * rayDirection.y;
                const float yAtExit = rayOrigin.y + cellExitT * rayDirection.y;
                const float segmentMinY = min(yAtEntry, yAtExit);
                const float segmentMaxY = max(yAtEntry, yAtExit);

                if (segmentMaxY >= columnBase && segmentMinY <= columnTop)
                {
                    float hitT = currentT;

                    if (rayDirection.y > 0.0 && yAtEntry < columnBase)
                    {
                        hitT = max(hitT, (columnBase - rayOrigin.y) / rayDirection.y);
                    }

                    if (rayDirection.y < 0.0 && yAtEntry > columnTop)
                    {
                        hitT = max(hitT, (columnTop - rayOrigin.y) / rayDirection.y);
                    }

                    if (hitT <= cellExitT + 0.0001)
                    {
                        const float3 hitPosition = rayOrigin + rayDirection * hitT;
                        const float3 hitNormal = EstimateSurfaceNormal(cellX, cellZ, hitPosition, columnTop, voxelSize);
                        return MakeTraceHit(hitPosition, hitNormal, hitT, cellX, cellZ);
                    }
                }
            }
        }

        if (tMaxX < tMaxZ)
        {
            currentT = tMaxX;
            tMaxX += tDeltaX;
            cellX += stepX;
        }
        else
        {
            currentT = tMaxZ;
            tMaxZ += tDeltaZ;
            cellZ += stepZ;
        }

        if (currentT > clippedExit)
        {
            break;
        }
    }

    return miss;
}

bool TraceFieldAnyHit(float3 rayOrigin, float3 rayDirection, float voxelSize, float maxDistance, uint displayMode)
{
    return TraceFieldClosestHit(rayOrigin, rayDirection, voxelSize, maxDistance, displayMode).hit != 0;
}

// -----------------------------------------------------------------------------
// Lighting
// -----------------------------------------------------------------------------

float ComputeSunVisibility(float3 hitPosition, float3 normal, float3 sunDirection, float voxelSize, uint displayMode)
{
    const float3 shadowOrigin = hitPosition + normal * (voxelSize * 0.10) + sunDirection * (voxelSize * 0.05);
    const bool occluded = TraceFieldAnyHit(shadowOrigin, sunDirection, voxelSize, voxelSize * 180.0, displayMode);
    return occluded ? 0.16 : 1.0;
}

float ComputeAmbientOcclusion(float3 hitPosition, float3 normal, float voxelSize, uint displayMode)
{
    const float3 tangentSeed = abs(normal.y) < 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(tangentSeed, normal));
    const float3 bitangent = normalize(cross(normal, tangent));
    const float3 aoDirections[5] = {
        normalize(normal + tangent * 0.65),
        normalize(normal - tangent * 0.65),
        normalize(normal + bitangent * 0.65),
        normalize(normal - bitangent * 0.65),
        normalize(normal + (tangent + bitangent) * 0.45)
    };

    float visibility = 0.0;

    [unroll]
    for (int index = 0; index < 5; ++index)
    {
        const float3 direction = aoDirections[index];
        const float3 aoOrigin = hitPosition + normal * (voxelSize * 0.08) + direction * (voxelSize * 0.05);
        visibility += TraceFieldAnyHit(aoOrigin, direction, voxelSize, voxelSize * 5.0, displayMode) ? 0.0 : 1.0;
    }

    return lerp(0.26, 1.0, visibility / 5.0);
}

float3 ComputeBounceLighting(float3 hitPosition, float3 normal, float voxelSize, uint displayMode)
{
    const uint fieldWidth = fieldInfo.x;
    const float3 tangentSeed = abs(normal.y) < 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(tangentSeed, normal));
    const float3 bitangent = normalize(cross(normal, tangent));
    const float3 bounceDirections[4] = {
        normalize(normal + tangent * 0.70),
        normalize(normal - tangent * 0.70),
        normalize(normal + bitangent * 0.70),
        normalize(normal - bitangent * 0.70)
    };

    float3 bouncedLight = float3(0.0, 0.0, 0.0);

    [unroll]
    for (int index = 0; index < 4; ++index)
    {
        const float3 direction = bounceDirections[index];
        const float3 bounceOrigin = hitPosition + normal * (voxelSize * 0.08) + direction * (voxelSize * 0.05);
        const TraceHitInfo bounceHit = TraceFieldClosestHit(bounceOrigin, direction, voxelSize, voxelSize * 12.0, displayMode);

        if (bounceHit.hit == 0)
        {
            continue;
        }

        const FieldCell bounceCell = fieldCells[bounceHit.cellX + bounceHit.cellZ * fieldWidth];
        const float3 bounceColor = MaterialColor(bounceCell, bounceHit.cellX, bounceHit.cellZ);
        const float emitterFacing = saturate(dot(bounceHit.normal, -direction));
        const float receiverFacing = saturate(dot(normal, direction));
        const float distanceFalloff = 1.0 / (1.0 + bounceHit.distance * 0.35);
        bouncedLight += bounceColor * emitterFacing * receiverFacing * distanceFalloff;
    }

    return bouncedLight * 0.10;
}

// -----------------------------------------------------------------------------
// Presentation / Styling
// -----------------------------------------------------------------------------

float3 MaterialColor(FieldCell cell, int cellX, int cellZ)
{
    const float tint = 0.92 + (float)((cellX * 17 + cellZ * 31) & 3) * 0.03;
    float3 color = float3(0.75, 0.25, 0.85);

    switch (cell.materialId)
    {
    case 0:
        color = float3(0.23, 0.63, 0.19);
        break;
    case 1:
        color = float3(0.43, 0.36, 0.22);
        break;
    case 2:
        color = float3(0.49, 0.41, 0.27);
        break;
    case 3:
        color = float3(0.62, 0.63, 0.59);
        break;
    case 4:
        color = float3(0.24, 0.29, 0.34);
        break;
    case 5:
        color = float3(0.57, 0.31, 0.24);
        break;
    case 6:
        color = float3(0.18, 0.43, 0.68);
        break;
    }

    color *= tint;
    if (cell.materialId != 6)
    {
        color = lerp(color, color * 0.70, saturate(cell.soilMoisture * 0.65));
    }
    else
    {
        color = lerp(color, float3(0.52, 0.73, 0.94), 0.22);
    }
    color += float3(0.02, 0.04, 0.01) * cell.fertility;
    return saturate(color);
}

float SurfaceGridLine(float3 hitPosition, float3 normal, float cellSize, float lineWidth)
{
    if (abs(normal.y) > 0.5)
    {
        return max(
            GridEdgeFactor(hitPosition.x - fieldOriginAndVoxelSize.x, cellSize, lineWidth),
            GridEdgeFactor(hitPosition.z - fieldOriginAndVoxelSize.y, cellSize, lineWidth));
    }

    if (abs(normal.x) > 0.5)
    {
        return max(
            GridEdgeFactor(hitPosition.z - fieldOriginAndVoxelSize.y, cellSize, lineWidth),
            GridEdgeFactor(hitPosition.y, cellSize, lineWidth));
    }

    return max(
        GridEdgeFactor(hitPosition.x - fieldOriginAndVoxelSize.x, cellSize, lineWidth),
        GridEdgeFactor(hitPosition.y, cellSize, lineWidth));
}

float3 ApplyDisplayModeStyling(float3 color, FieldCell cell, float3 hitPosition, float3 normal, float voxelSize, uint displayMode)
{
    const float fineCellSize = displayMode == display_mode_coarse ? voxelSize : FineVoxelSize(voxelSize);
    const float coarseCellSize = voxelSize;
    float3 styledColor = saturate(color);

    if (displayMode == display_mode_coarse)
    {
        return styledColor;
    }

    if (displayMode == display_mode_refined)
    {
        const float fineLine = SurfaceGridLine(
            hitPosition,
            normal,
            fineCellSize,
            max(fineCellSize * 0.10, 0.0005));
        styledColor = lerp(color, color * 0.78 + float3(0.03, 0.03, 0.03), saturate(fineLine * 0.45));
        return saturate(styledColor);
    }

    const float coarseFullHeight = cell.coarseFullHeightVoxels * voxelSize;
    const bool refinedTopExists = cell.columnHeightVoxels > cell.coarseFullHeightVoxels + 0.001;
    const bool hitsCoarseSide = abs(normal.y) <= 0.5
        && hitPosition.y < coarseFullHeight - voxelSize * 0.05;
    const bool coarseOwnedHit = !refinedTopExists || hitsCoarseSide;

    if (coarseOwnedHit)
    {
        const float coarseLine = SurfaceGridLine(hitPosition, normal, coarseCellSize, max(voxelSize * 0.80, coarseCellSize * 0.06));
        styledColor = lerp(color, color * 0.70, saturate(coarseLine * 0.62));
        styledColor = lerp(styledColor, styledColor + float3(0.05, 0.03, 0.00), 0.08);
    }
    else
    {
        const float fineLine = SurfaceGridLine(hitPosition, normal, fineCellSize, max(fineCellSize * 0.12, 0.0005));
        const float coarseLine = SurfaceGridLine(hitPosition, normal, coarseCellSize, max(voxelSize * 0.45, coarseCellSize * 0.04));
        styledColor = lerp(color, color * 0.80 + float3(0.02, 0.02, 0.02), saturate(fineLine * 0.42));
        styledColor = lerp(styledColor, styledColor * 0.74, saturate(coarseLine * 0.18));
        styledColor = lerp(styledColor, styledColor + float3(0.03, 0.02, 0.00), 0.10);
    }
    return saturate(styledColor);
}

// -----------------------------------------------------------------------------
// Pixel Stage
// -----------------------------------------------------------------------------

float4 ShadePixel(VSOutput input, uint displayMode)
{
    const float voxelSize = fieldOriginAndVoxelSize.z;
    const float maxColumnHeight = fieldOriginAndVoxelSize.w * voxelSize;
    const uint fieldWidth = fieldInfo.x;
    const uint fieldDepth = fieldInfo.y;
    const bool highlightSelection = fieldInfo.z != 0;
    const bool hasSelection = fieldInfo.w != 0;

    const CameraRay ray = BuildCameraRay(input);
    const float3 skyColor = SkyColor(ray.direction);

    float tEnter = 0.0;
    float tExit = 0.0;
    const float3 fieldMin = FieldMinimum(voxelSize);
    const float3 fieldMax = FieldMaximum(voxelSize, maxColumnHeight);

    if (!IntersectAabb(ray.origin, ray.direction, fieldMin, fieldMax, tEnter, tExit))
    {
        return float4(skyColor, 1.0);
    }

    const TraceHitInfo hit = TraceFieldClosestHit(ray.origin, ray.direction, voxelSize, tExit, displayMode);
    if (hit.hit == 0)
    {
        return float4(skyColor, 1.0);
    }

    const FieldCell cell = fieldCells[hit.cellX + hit.cellZ * fieldWidth];
    const float3 sunDirection = normalize(float3(-0.45, 1.0, -0.25));
    const float ambientOcclusion = ComputeAmbientOcclusion(hit.position, hit.normal, voxelSize, displayMode);
    const float sunVisibility = ComputeSunVisibility(hit.position, hit.normal, sunDirection, voxelSize, displayMode);
    const float3 bounceLighting = ComputeBounceLighting(hit.position, hit.normal, voxelSize, displayMode);
    const float diffuse = saturate(dot(hit.normal, sunDirection));
    const float skyFill = lerp(0.22, 0.46, saturate(hit.normal.y * 0.5 + 0.5));
    const float lighting = skyFill * ambientOcclusion + diffuse * sunVisibility * 0.72;
    float3 color = MaterialColor(cell, hit.cellX, hit.cellZ) * lighting + bounceLighting;
    color = ApplyDisplayModeStyling(color, cell, hit.position, hit.normal, voxelSize, displayMode);

    if (highlightSelection && hasSelection)
    {
        uint hitVoxelY = 0;
        bool sameColumn = false;

        if (displayMode != display_mode_coarse)
        {
            const uint patchResolution = PatchResolution();
            const float fineVoxelSize = FineVoxelSize(voxelSize);
            const int localX = clamp(
                (int)floor((hit.position.x - (fieldOriginAndVoxelSize.x + hit.cellX * voxelSize)) / fineVoxelSize),
                0,
                (int)patchResolution - 1);
            const int localZ = clamp(
                (int)floor((hit.position.z - (fieldOriginAndVoxelSize.y + hit.cellZ * voxelSize)) / fineVoxelSize),
                0,
                (int)patchResolution - 1);
            const uint hitDisplayX = (uint)hit.cellX * patchResolution + (uint)localX;
            const uint hitDisplayZ = (uint)hit.cellZ * patchResolution + (uint)localZ;
            const int patchIndex = refinedPatchLookup[hit.cellX + hit.cellZ * fieldWidth];
            const bool hasPatch = patchIndex >= 0;
            RefinedPatchMetadata patch = refinedPatchMetadata[0];
            if (hasPatch)
            {
                patch = refinedPatchMetadata[patchIndex];
            }
            const float columnBase = hasPatch ? patch.coarseFullHeightVoxels * fineVoxelSize : 0.0;
            const float columnTop = hasPatch
                ? LocalPatchColumnTop(patch, localX, localZ)
                : cell.columnHeightVoxels * voxelSize;
            hitVoxelY = clamp(
                (uint)floor(max(hit.position.y - 0.001, 0.0) / fineVoxelSize),
                0u,
                (uint)max(columnTop / fineVoxelSize - 1.0, 0.0));
            sameColumn = hitDisplayX == selectionInfo.x
                && hitDisplayZ == selectionInfo.z
                && columnTop > columnBase + 0.0001;
        }
        else
        {
            const float columnBase = ColumnBaseHeight(cell, voxelSize, displayMode);
            hitVoxelY = clamp(
                (uint)floor(max(hit.position.y - 0.001, columnBase) / voxelSize),
                0u,
                (uint)cell.columnHeightVoxels - 1);
            sameColumn = selectionInfo.x == (uint)hit.cellX && selectionInfo.z == (uint)hit.cellZ;
        }

        if (sameColumn && hitVoxelY == selectionInfo.y)
        {
            color = lerp(color, float3(0.98, 0.85, 0.36), 0.85);
        }
    }

    const float haze = saturate((hit.distance - 85.0) / 130.0);
    return float4(lerp(color, skyColor, haze), 1.0);
}

float4 PSCoarse(VSOutput input) : SV_TARGET
{
    return ShadePixel(input, display_mode_coarse);
}

float4 PSRefined(VSOutput input) : SV_TARGET
{
    return ShadePixel(input, display_mode_refined);
}

float4 PSHybrid(VSOutput input) : SV_TARGET
{
    return ShadePixel(input, display_mode_hybrid);
}
