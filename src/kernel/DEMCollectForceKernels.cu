// DEM force computation related custom kernels
#include <granular/DataStructs.h>
#include <granular/GranularDefines.h>
#include <kernel/DEMHelperKernels.cu>

__global__ void cashInOwnerIndex(sgps::bodyID_t* idOwner,
                                 sgps::bodyID_t* id,
                                 sgps::bodyID_t* ownerClumpBody,
                                 size_t nContactPairs) {
    size_t myID = blockIdx.x * blockDim.x + threadIdx.x;
    if (myID < nContactPairs) {
        sgps::bodyID_t thisBodyID = id[myID];
        idOwner[myID] = ownerClumpBody[thisBodyID];
    }
}

__global__ void cashInMassMoiIndex(float* massOwner,
                                   float3* moiOwner,
                                   sgps::clumpBodyInertiaOffset_t* inertiaPropOffsets,
                                   sgps::bodyID_t* idOwner,
                                   size_t nContactPairs,
                                   float* massClumpBody,
                                   float* mmiXX,
                                   float* mmiYY,
                                   float* mmiZZ,
                                   sgps::clumpBodyInertiaOffset_t nDistinctClumpBodyTopologies) {
    extern __shared__ float ClumpMasses[];
    float* moiX = ClumpMasses + TEST_SHARED_SIZE;
    float* moiY = ClumpMasses + 2 * TEST_SHARED_SIZE;
    float* moiZ = ClumpMasses + 3 * TEST_SHARED_SIZE;
    if (threadIdx.x == 0) {
        for (unsigned int i = 0; i < nDistinctClumpBodyTopologies; i++) {
            ClumpMasses[i] = massClumpBody[i];
            moiX[i] = mmiXX[i];
            moiY[i] = mmiYY[i];
            moiZ[i] = mmiZZ[i];
        }
    }
    __syncthreads();

    size_t myID = blockIdx.x * blockDim.x + threadIdx.x;
    if (myID < nContactPairs) {
        sgps::bodyID_t thisOwnerID = idOwner[myID];
        sgps::clumpBodyInertiaOffset_t myMassOffset = inertiaPropOffsets[thisOwnerID];
        float3 moi;
        moi.x = moiX[myMassOffset];
        moi.y = moiY[myMassOffset];
        moi.z = moiZ[myMassOffset];
        massOwner[myID] = ClumpMasses[myMassOffset];
        moiOwner[myID] = moi;
    }
}

// TODO: make it a template
// computes a ./ b
__global__ void elemDivide(float3* out, float3* a, float* b, double modifier, size_t n) {
    size_t myID = blockIdx.x * blockDim.x + threadIdx.x;
    if (myID < n) {
        auto num = a[myID] * modifier;
        auto den = b[myID];
        out[myID] = num / den;
    }
}

// TODO: make it a template
// computes cross(a, b) ./ c
__global__ void elemCrossDivide(float3* out, float3* a, float3* b, float3* c, double modifier, size_t n) {
    size_t myID = blockIdx.x * blockDim.x + threadIdx.x;
    if (myID < n) {
        auto num1 = a[myID];
        auto num2 = b[myID] * modifier;
        auto den = c[myID];
        out[myID] = cross(num1, num2) / den;
    }
}

// Place information to an array based on an index array and a value array
__global__ void stashElem(float* out1, float* out2, float* out3, sgps::bodyID_t* index, float3* value, size_t n) {
    size_t myID = blockIdx.x * blockDim.x + threadIdx.x;
    if (myID < n) {
        // my_index is unique, no race condition
        sgps::bodyID_t my_index = index[myID];
        float3 my_value = value[myID];
        out1[my_index] += my_value.x;
        out2[my_index] += my_value.y;
        out3[my_index] += my_value.z;
    }
}