#pragma once
#include <cstdint>
#include <vector>
#include "engine/physics.h"

namespace engine {

class Engine {
public:
    Engine();

    void step(double dt);
    void reset();

    void addObject(const SceneObject& object);
    const std::vector<SceneObject>& objects() const { return objects_; }
    std::vector<SceneObject>& objects();

    const GridMesh& gridMesh() const;
    DiskParams diskParams() const;
    double time() const { return time_; }
    uint64_t gridVersion() const { return gridVersion_; }

private:
    void rebuildGridMesh() const;

    std::vector<SceneObject> objects_;
    double time_ = 0.0;

    mutable GridMesh cachedGridMesh_;
    mutable bool gridMeshDirty_ = true;
    mutable uint64_t gridVersion_ = 1;

    static constexpr int kGridSize = 25;
    static constexpr float kGridSpacing = 1e10f;
};

} // namespace engine
