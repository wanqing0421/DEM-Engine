// SPH-DualGPU
// SPH system base class header

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <core/utils/ManagedAllocator.hpp>
#include <core/utils/GpuManager.h>
#include <cuda_runtime_api.h>
#include <sph/datastruct.h>
#include <mutex>
#include <atomic>
struct DataManager {
    float radius;
    std::vector<vector3, sgps::ManagedAllocator<vector3>> m_pos;              // particle locations
    std::vector<vector3, sgps::ManagedAllocator<vector3>> m_vel;              // particle velocities
    std::vector<vector3, sgps::ManagedAllocator<vector3>> m_acc;              // particle accelerations
    std::vector<char, sgps::ManagedAllocator<char>> m_fix;                    // particle fixity
    std::vector<contactData, sgps::ManagedAllocator<contactData>> m_contact;  // contact pair data

    std::vector<int, sgps::ManagedAllocator<int>> m_offset;  // index offset array for the contact pair data
};

class SPHSystem;
class KinematicThread {
  private:
    GpuManager& gpuManager;

    GpuManager::StreamInfo streamInfo;

    DataManager& dataManager;

    SPHSystem& parentSystem;

  public:
    int kinematicCounter;

    KinematicThread(DataManager& dm, GpuManager& gm, SPHSystem& system)
        : dataManager(dm), gpuManager(gm), parentSystem(system) {
        streamInfo = gm.getAvailableStream();
        kinematicCounter = 0;
    }

    void operator()();

    SPHSystem& getParentSystem() { return parentSystem; }
};

class DynamicThread {
  private:
    GpuManager& gpuManager;

    GpuManager::StreamInfo streamInfo;

    DataManager& dataManager;

    SPHSystem& parentSystem;

  public:
    int dynamicCounter;

    DynamicThread(DataManager& dm, GpuManager& gm, SPHSystem& system)
        : dataManager(dm), gpuManager(gm), parentSystem(system) {
        streamInfo = gm.getAvailableStream();
        dynamicCounter = 0;
    }

    void operator()();

    SPHSystem& getParentSystem() { return parentSystem; }
};

class WriteOutThread {
  private:
    DataManager& dataManager;

    SPHSystem& parentSystem;

  public:
    int writeOutCounter;

    WriteOutThread(DataManager& dm, SPHSystem& system) : dataManager(dm), parentSystem(system) { writeOutCounter = 0; }

    void operator()();

    SPHSystem& getParentSystem() { return parentSystem; }
};

class SPHSystem {
  private:
    KinematicThread kt;
    DynamicThread dt;
    WriteOutThread wt;

    DataManager dataManager;

    std::mutex mutex_lock_pos;
    std::mutex mutex_lock_contact;

    // printout indicator
    bool isPrint;

    // write out parameters
    int stepPrint;  // this parameter determines how many steps to do 1 step write out
    int curPrint;   // this parameter counts write out step currently at

  public:
    inline SPHSystem(GpuManager& gm) : kt(dataManager, gm, *this), dt(dataManager, gm, *this), wt(dataManager, *this) {
        curr_time = 0.0f;
        pos_data_isFresh = true;
        contact_data_isFresh = false;

        isPrint = false;
    };

    void setPrintOut(bool isPrint, int stepPrint) {
        this->isPrint = isPrint;
        this->stepPrint = stepPrint;
        this->curPrint = 0;
        this->wt_thread_busy = false;
        this->wt_buffer_fresh = false;
    }

    bool getPrintOut() { return isPrint; }
    int getCurPrint() { return curPrint; }
    void setCurPrint(int set_val) { curPrint = set_val; }
    int getStepPrint() { return stepPrint; }

    // initialize the SPHSystem with pos as the particle positions
    // n as the total number of particles initialized in the SPHSystem
    void initialize(float radius,
                    std::vector<vector3>& pos,
                    std::vector<vector3>& vel,
                    std::vector<vector3>& acc,
                    std::vector<char>& fix);

    // start performing simulation dynamics
    void doStepDynamics(float time_step, float sim_time);

    // print particle file to csv for paraview visualization purposes
    void printCSV(std::string filename, vector3* pos_arr, int pos_n, vector3* vel_arr, vector3* acc_arr);

    // dual gpu coordinations
    std::mutex& getMutexPos() { return mutex_lock_pos; }
    std::mutex& getMutexContact() { return mutex_lock_contact; }

    int getKiCounter() { return kt.kinematicCounter; }
    int getDyCounter() { return dt.dynamicCounter; }

    KinematicThread& getKtRef() { return kt; }
    DynamicThread& getDtRef() { return dt; }
    WriteOutThread& getWtRef() { return wt; }

    std::atomic<float> curr_time;
    std::atomic<float> sim_time;
    std::atomic<float> time_step;

    std::atomic<bool> pos_data_isFresh;
    std::atomic<bool> contact_data_isFresh;

    std::atomic<bool> wt_thread_busy;
    std::atomic<bool> wt_buffer_fresh;
};
