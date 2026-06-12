// BVH.cpp

#include "BVH.h"
#include "Scene.h"
#include <algorithm>
#include <cfloat>

namespace RYRayTracing {

namespace {

constexpr int N_BINS = 16;
constexpr int LEAF_MAX_TRIANGLES = 4;
constexpr float SAH_TRAVERSAL_COST = 1.0f;
constexpr float SAH_INTERSECTION_COST = 1.0f;

struct BuildTriangle {
    glm::vec3 centroid;
    glm::vec3 bboxMin;
    glm::vec3 bboxMax;
    uint32_t triIdx;
};

struct BinData {
    int count = 0;
    glm::vec3 bboxMin{FLT_MAX};
    glm::vec3 bboxMax{-FLT_MAX};

    void extend(const glm::vec3& triMin, const glm::vec3& triMax) {
        count++;
        bboxMin = glm::min(bboxMin, triMin);
        bboxMax = glm::max(bboxMax, triMax);
    }
};

inline float surfaceArea(const glm::vec3& bmin, const glm::vec3& bmax) {
    glm::vec3 d = bmax - bmin;
    return 2.0f * (d.x * d.y + d.x * d.z + d.y * d.z);
}

// triIndices[begin...end) are positions into the BuildTriangle array.
int32_t buildBVH(const std::vector<BuildTriangle>& tris,
                 std::vector<uint32_t>& triIndices,
                 int begin, int end,
                 std::vector<BVHNode>& nodes)
{
    int32_t nodeIdx = static_cast<int32_t>(nodes.size());
    nodes.emplace_back();

    glm::vec3 centroidMin(FLT_MAX), centroidMax(-FLT_MAX);
    glm::vec3 geomMin(FLT_MAX), geomMax(-FLT_MAX);
    for (int i = begin; i < end; ++i) {
        const auto& t = tris[triIndices[i]];
        centroidMin = glm::min(centroidMin, t.centroid);
        centroidMax = glm::max(centroidMax, t.centroid);
        geomMin = glm::min(geomMin, t.bboxMin);
        geomMax = glm::max(geomMax, t.bboxMax);
    }
    nodes[nodeIdx].bboxMin = geomMin;
    nodes[nodeIdx].bboxMax = geomMax;

    int count = end - begin;
    if (count <= LEAF_MAX_TRIANGLES) {
        nodes[nodeIdx].splitAxis = -1;
        nodes[nodeIdx].leftOrFirst = begin; // position in triIndices
        nodes[nodeIdx].rightOrCount = count;
        return nodeIdx;
    }

    glm::vec3 extent = centroidMax - centroidMin;
    int axis = (extent.x >= extent.y && extent.x >= extent.z) ? 0 :
               (extent.y >= extent.z) ? 1 : 2;

    float k1 = centroidMin[axis];
    float binScale = (extent[axis] > 1e-12f) ? N_BINS / extent[axis] : 0.0f;
    if (binScale <= 0.0f) {
        int mid = (begin + end) / 2;
        if (mid == begin) mid = begin + 1;
        int32_t left = buildBVH(tris, triIndices, begin, mid, nodes);
        int32_t right = buildBVH(tris, triIndices, mid, end, nodes);
        nodes[nodeIdx].leftOrFirst = left;
        nodes[nodeIdx].rightOrCount = right;
        nodes[nodeIdx].splitAxis = static_cast<int32_t>(axis);
        return nodeIdx;
    }

    BinData bins[N_BINS];
    for (int i = begin; i < end; ++i) {
        const auto& t = tris[triIndices[i]];
        int b = static_cast<int>((t.centroid[axis] - k1) * binScale);
        if (b >= N_BINS) b = N_BINS - 1;
        bins[b].extend(t.bboxMin, t.bboxMax);
    }

    int countLeft[N_BINS - 1];
    glm::vec3 leftMin[N_BINS - 1], leftMax[N_BINS - 1];
    {
        BinData accum;
        for (int s = 0; s < N_BINS - 1; ++s) {
            accum.count += bins[s].count;
            accum.bboxMin = glm::min(accum.bboxMin, bins[s].bboxMin);
            accum.bboxMax = glm::max(accum.bboxMax, bins[s].bboxMax);
            countLeft[s] = accum.count;
            leftMin[s] = accum.bboxMin;
            leftMax[s] = accum.bboxMax;
        }
    }

    int countRight[N_BINS - 1];
    glm::vec3 rightMin[N_BINS - 1], rightMax[N_BINS - 1];
    {
        BinData accum;
        for (int s = N_BINS - 1; s > 0; --s) {
            accum.count += bins[s].count;
            accum.bboxMin = glm::min(accum.bboxMin, bins[s].bboxMin);
            accum.bboxMax = glm::max(accum.bboxMax, bins[s].bboxMax);
            countRight[s - 1] = accum.count;
            rightMin[s - 1] = accum.bboxMin;
            rightMax[s - 1] = accum.bboxMax;
        }
    }

    float bestCost = FLT_MAX;
    int bestSplit = -1;
    float rootSA = surfaceArea(geomMin, geomMax);
    if (rootSA < 1e-12f) rootSA = 1.0f;

    for (int s = 0; s < N_BINS - 1; ++s) {
        if (countLeft[s] == 0 || countRight[s] == 0) continue;
        float saL = surfaceArea(leftMin[s], leftMax[s]);
        float saR = surfaceArea(rightMin[s], rightMax[s]);
        float cost = SAH_TRAVERSAL_COST +
                     (saL * countLeft[s] + saR * countRight[s]) / rootSA * SAH_INTERSECTION_COST;
        if (cost < bestCost) { bestCost = cost; bestSplit = s; }
    }

    int mid;
    if (bestSplit < 0) {
        mid = (begin + end) / 2;
    } else {
        auto pred = [&](uint32_t triPos) {
            const auto& t = tris[triPos];
            int b = static_cast<int>((t.centroid[axis] - k1) * binScale);
            if (b >= N_BINS) b = N_BINS - 1;
            return b <= bestSplit;
        };
        auto it = std::partition(triIndices.begin() + begin, triIndices.begin() + end, pred);
        mid = static_cast<int>(std::distance(triIndices.begin(), it));
        if (mid == begin || mid == end) mid = (begin + end) / 2;
    }

    int32_t left = buildBVH(tris, triIndices, begin, mid, nodes);
    int32_t right = buildBVH(tris, triIndices, mid, end, nodes);

    nodes[nodeIdx].leftOrFirst = left;
    nodes[nodeIdx].rightOrCount = right;
    nodes[nodeIdx].splitAxis = static_cast<int32_t>(axis);
    return nodeIdx;
}

} // anonymous namespace

int32_t buildSAHBVH(const ModelSource& src, ModelSourceBVH& outBVH)
{
    const auto& idx = src.indices;
    const auto& pos = src.positions;
    size_t triCount = idx.size() / 3;
    if (triCount == 0) return -1;

    // Build triangle metadata array
    std::vector<BuildTriangle> tris(triCount);
    for (size_t t = 0; t < triCount; ++t) {
        uint32_t i0 = idx[t * 3 + 0];
        uint32_t i1 = idx[t * 3 + 1];
        uint32_t i2 = idx[t * 3 + 2];
        const glm::vec3& v0 = pos[i0];
        const glm::vec3& v1 = pos[i1];
        const glm::vec3& v2 = pos[i2];
        tris[t] = {
            (v0 + v1 + v2) / 3.0f,
            glm::min(glm::min(v0, v1), v2),
            glm::max(glm::max(v0, v1), v2),
            static_cast<uint32_t>(t)
        };
    }

    // Permutation array: initially identity
    std::vector<uint32_t> triIndices(triCount);
    for (size_t i = 0; i < triCount; ++i) triIndices[i] = static_cast<uint32_t>(i);

    outBVH.nodes.clear();
    outBVH.nodes.reserve(triCount * 2);

    outBVH.rootIndex = buildBVH(tris, triIndices, 0, static_cast<int>(triCount), outBVH.nodes);

    // Build triRemap: for each position in the permuted order, store the
    // local triangle index. The global offset is filled later.
    outBVH.triRemap.resize(triCount);
    for (size_t i = 0; i < triCount; ++i)
        outBVH.triRemap[i] = triIndices[i];

    return outBVH.rootIndex;
}

} // namespace RYRayTracing
