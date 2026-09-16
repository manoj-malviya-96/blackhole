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
    const std::vector<SceneObject>& objects() const { return m_objects; }
    std::vector<SceneObject>& objects();

    const GridMesh& gridMesh() const;
    DiskParams diskParams() const;
    double time() const { return m_time; }
    uint64_t gridVersion() const { return m_gridVersion; }

private:
    void rebuildGridMesh() const;

    std::vector<SceneObject> m_objects;
    double m_time = 0.0;

    mutable GridMesh m_cachedGridMesh;
    mutable bool m_gridMeshDirty = true;
    mutable uint64_t m_gridVersion = 1;

    static constexpr int kGridSize = 25;
    static constexpr float kGridSpacing = 1e10f;
};

} // namespace engine
