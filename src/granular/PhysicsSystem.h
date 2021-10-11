//  Copyright (c) 2021, SBEL GPU Development Team
//  Copyright (c) 2021, University of Wisconsin - Madison
//  All rights reserved.

#pragma once

#include <mutex>
#include <vector>
#include <thread>
// #include <set>

#include <core/ApiVersion.h>
#include <core/utils/ManagedAllocator.hpp>
#include <core/utils/ThreadManager.h>
#include <core/utils/GpuManager.h>
#include <helper_math.cuh>
#include <core/utils/GpuError.h>

#include <granular/GranularDefines.h>
#include <granular/DataStructs.h>

namespace sgps {

class kinematicThread {
  protected:
    ThreadManager* pSchedSupport;
    GpuManager* pGpuDistributor;

    // The std::thread that binds to this instance
    std::thread th;

    // Object which stores the device and stream IDs for this thread
    GpuManager::StreamInfo streamInfo;

    // this is where the dynamic thread stores data that needs to be produced
    // herein
    voxelID_default_t* pDynamicOwnedBuffer_voxelID;
    int kinematicAverageTime;
    int costlyProductionStep(int) const;

    size_t m_approx_bytes_used = 0;

    // Buffer arrays for storing info from the dT side.
    // dT modifies these arrays; kT uses them only.

    // buffer array for voxelID
    std::vector<voxelID_default_t, ManagedAllocator<voxelID_default_t>> transferBuffer_voxelID;

    // The voxel ID (split into 3 parts, representing XYZ location)
    std::vector<voxelID_default_t, ManagedAllocator<voxelID_default_t>> voxelID;
    
  public:
    friend class SGPS;

    kinematicThread(ThreadManager* pSchedSup, GpuManager* pGpuDist)
        : pSchedSupport(pSchedSup), pGpuDistributor(pGpuDist) {
        kinematicAverageTime = 0;
        pDynamicOwnedBuffer_voxelID = NULL;

        transferBuffer_voxelID.resize(N_MANUFACTURED_ITEMS, 0);
        voxelID.resize(N_MANUFACTURED_ITEMS, 1);

        // Get a device/stream ID to use from the GPU Manager
        streamInfo = pGpuDistributor->getAvailableStream();

        pSchedSupport->kinematicShouldJoin = false;
        pSchedSupport->kinematicStarted = false;

        // Launch a worker thread bound to this instance
        th = std::move(std::thread([this](){
            this->workerThread();
        }));
    }
    ~kinematicThread() {
        pSchedSupport->kinematicShouldJoin = true;
        th.join();
    }

    void setKinematicAverageTime(int val) { kinematicAverageTime = val; }

    // buffer exchange methods
    void setDestinationBuffer_voxelID(voxelID_default_t* pCB) { pDynamicOwnedBuffer_voxelID = pCB; }
    voxelID_default_t* pBuffer_voxelID() { return transferBuffer_voxelID.data(); }

    void primeDynamic();

    // Called each time when the user calls LaunchThreads.
    void startThread();

    // The actual kernel things go here. 
    // It is called upon construction.
    void workerThread();
};

class dynamicThread {
  protected:
    ThreadManager* pSchedSupport;
    GpuManager* pGpuDistributor;

    // The std::thread that binds to this instance
    std::thread th;

    // Object which stores the device and stream IDs for this thread
    GpuManager::StreamInfo streamInfo;

    // pointer to remote buffer where kinematic thread stores work-order data
    // provided by the dynamic thread
    voxelID_default_t* pKinematicOwnedBuffer_voxelID;

    int dynamicAverageTime;  // time required in the consumption process; fake lag
    int nDynamicCycles;

    // Buffer arrays for storing info from the dT side.
    // kT modifies these arrays; dT uses them only.

    // buffer array for voxelID
    std::vector<voxelID_default_t, ManagedAllocator<voxelID_default_t>> transferBuffer_voxelID;

    // Pointers to simulation params-related arrays
    // Check DataStructs.h for details
    GranSimParams* simParams;

    // Pointers to those data arrays defined below, stored in a struct
    GranDataDT* granData;

    // Body-related arrays in managed memory

    // Those are the smaller ones, the unique, template ones
    // The mass values
    std::vector<float, ManagedAllocator<float>> massClumpBody;

    // The components of MOI values
    std::vector<float, ManagedAllocator<float>> mmiXX;
    std::vector<float, ManagedAllocator<float>> mmiYY;
    std::vector<float, ManagedAllocator<float>> mmiZZ;

    // The distinct sphere radii values
    std::vector<float, ManagedAllocator<float>> radiiSphere;

    // The distinct sphere local position (wrt CoM) values
    std::vector<float, ManagedAllocator<float>> relPosSphereX;
    std::vector<float, ManagedAllocator<float>> relPosSphereY;
    std::vector<float, ManagedAllocator<float>> relPosSphereZ;

    // Those are the large ones, ones that have the same length as the number of clumps
    // The mass offsets
    std::vector<clumpBodyInertiaOffset_default_t, ManagedAllocator<clumpBodyInertiaOffset_default_t>>
        inertiaPropOffsets;

    // The voxel ID (split into 3 parts, representing XYZ location)
    std::vector<voxelID_default_t, ManagedAllocator<voxelID_default_t>> voxelID;

    // The XYZ local location inside a voxel
    std::vector<unsigned int, ManagedAllocator<unsigned int>> locX;
    std::vector<unsigned int, ManagedAllocator<unsigned int>> locY;
    std::vector<unsigned int, ManagedAllocator<unsigned int>> locZ;

    // The clump quaternion
    std::vector<int, ManagedAllocator<int>> oriQ0;
    std::vector<int, ManagedAllocator<int>> oriQ1;
    std::vector<int, ManagedAllocator<int>> oriQ2;
    std::vector<int, ManagedAllocator<int>> oriQ3;

    // Linear velocity times ts size: hv
    std::vector<int, ManagedAllocator<int>> hvX;
    std::vector<int, ManagedAllocator<int>> hvY;
    std::vector<int, ManagedAllocator<int>> hvZ;

    // The angular velocity times ts size: h*omega
    std::vector<int, ManagedAllocator<int>> hOmgBarX;
    std::vector<int, ManagedAllocator<int>> hOmgBarY;
    std::vector<int, ManagedAllocator<int>> hOmgBarZ;

    // Linear acceleration times h^2
    std::vector<int, ManagedAllocator<int>> h2aX;
    std::vector<int, ManagedAllocator<int>> h2aY;
    std::vector<int, ManagedAllocator<int>> h2aZ;

    // Angular acceleration times h^2
    std::vector<int, ManagedAllocator<int>> h2AlphaX;
    std::vector<int, ManagedAllocator<int>> h2AlphaY;
    std::vector<int, ManagedAllocator<int>> h2AlphaZ;

    size_t m_approx_bytes_used = 0;

    // Sphere-related arrays in managed memory
    // Belonged-body ID (default unsigned int type)
    std::vector<bodyID_default_t, ManagedAllocator<bodyID_default_t>> ownerClumpBody;

    // The ID that maps this sphere's radius and relPos
    std::vector<clumpComponentOffset_t, ManagedAllocator<clumpComponentOffset_t>> clumpComponentOffset;

    // The ID that maps this sphere's material
    std::vector<materialsOffset_default_t, ManagedAllocator<materialsOffset_default_t>> materialTupleOffset;

    int localUse(int val);

  public:
    friend class SGPS;

    dynamicThread(ThreadManager* pSchedSup, GpuManager* pGpuDist)
        : pSchedSupport(pSchedSup), pGpuDistributor(pGpuDist) {
        GPU_CALL(cudaMallocManaged(&simParams, sizeof(GranSimParams), cudaMemAttachGlobal));
        GPU_CALL(cudaMallocManaged(&granData, sizeof(GranDataDT), cudaMemAttachGlobal));

        pKinematicOwnedBuffer_voxelID = NULL;
        nDynamicCycles = 0;
        dynamicAverageTime = 0;

        transferBuffer_voxelID.resize(N_MANUFACTURED_ITEMS, 0);

        // Get a device/stream ID to use from the GPU Manager
        streamInfo = pGpuDistributor->getAvailableStream();

        pSchedSupport->dynamicShouldJoin = false;
        pSchedSupport->dynamicStarted = false;
        
        // Launch a worker thread bound to this instance
        th = std::move(std::thread([this](){
            this->workerThread();
        }));
    }
    ~dynamicThread() {
        pSchedSupport->dynamicShouldJoin = true;
        th.join();
    }

    void setDynamicAverageTime(int val) { dynamicAverageTime = val; }
    void setNDynamicCycles(int val) { nDynamicCycles = val; }

    // buffer exchange methods
    void setDestinationBuffer_voxelID(voxelID_default_t* pPB) { pKinematicOwnedBuffer_voxelID = pPB; }
    voxelID_default_t* pBuffer_voxelID() { return transferBuffer_voxelID.data(); }

    // Set SimParams items
    void setSimParams(unsigned char nvXp2,
                      unsigned char nvYp2,
                      unsigned char nvZp2,
                      float l,
                      double voxelSize,
                      float3 LBFPoint,
                      float3 G,
                      double ts_size);

    // Resize managed arrays (and perhaps Instruct/Suggest their preferred residence location as well?)
    void allocateManagedArrays(unsigned int nClumpBodies,
                               unsigned int nSpheresGM,
                               unsigned int nClumpTopo,
                               unsigned int nClumpComponents,
                               unsigned int nMatTuples);

    // Data type TBD, should come from JITCed headers
    void populateManagedArrays(const std::vector<clumpBodyInertiaOffset_default_t>& input_clump_types,
                               const std::vector<float3>& input_clump_xyz,
                               const std::vector<float>& clumps_mass_types,
                               const std::vector<std::vector<float>>& clumps_sp_radii_types,
                               const std::vector<std::vector<float3>>& clumps_sp_location_types);

    // Put sim data array pointers in place
    void packDataPointers();

    void WriteCsvAsSpheres(std::ofstream& ptFile) const;

    // Called each time when the user calls LaunchThreads.
    void startThread();

    // The actual kernel things go here. 
    // It is called upon construction.
    void workerThread();
};

}  // namespace sgps
