#include "engine/engine.h"
#include <cmath>

namespace engine {

Engine::Engine() {
    reset();
}

void Engine::step(double dt) {
    time_ += dt;
}

void Engine::reset() {
    time_ = 0.0;
    objects_.clear();
    objects_.push_back({
        .posRadius = QVector4D(0.0f, 0.0f, 0.0f, 5e10f),
        .color = QVector4D(1.0f, 1.0f, 1.0f, 1.0f),
        .mass = 4.3e6 * physics::kSolarMass,
        .velocity = QVector3D(0.0f, 0.0f, 0.0f),
        .spin = 0.9
    });
    gridMeshDirty_ = true;
}

void Engine::addObject(const SceneObject& object) {
    objects_.push_back(object);
    gridMeshDirty_ = true;
}

std::vector<SceneObject>& Engine::objects() {
    gridMeshDirty_ = true;
    return objects_;
}

DiskParams Engine::diskParams() const {
    if (objects_.empty()) {
        return DiskParams{};
    }
    const auto& primary = objects_.front();
    const double r_s = physics::schwarzschildRadius(primary.mass);
    return DiskParams{
        .r1 = static_cast<float>(2.2 * r_s),
        .r2 = static_cast<float>(11.0 * r_s),
        .spin = static_cast<float>(primary.spin)
    };
}

const GridMesh& Engine::gridMesh() const {
    if (gridMeshDirty_) {
        rebuildGridMesh();
        gridMeshDirty_ = false;
        ++gridVersion_;
    }
    return cachedGridMesh_;
}

void Engine::rebuildGridMesh() const {
    cachedGridMesh_.vertices.clear();
    cachedGridMesh_.indices.clear();

    cachedGridMesh_.vertices.reserve((kGridSize + 1) * (kGridSize + 1));
    cachedGridMesh_.indices.reserve(kGridSize * kGridSize * 4);

    for (int z = 0; z <= kGridSize; ++z) {
        for (int x = 0; x <= kGridSize; ++x) {
            const float worldX = static_cast<float>(x - kGridSize / 2) * kGridSpacing;
            const float worldZ = static_cast<float>(z - kGridSize / 2) * kGridSpacing;
            float y = 0.0f;

            for (const auto& obj : objects_) {
                const double r_s = physics::schwarzschildRadius(obj.mass);
                const double dx = static_cast<double>(worldX) - static_cast<double>(obj.posRadius.x());
                const double dz = static_cast<double>(worldZ) - static_cast<double>(obj.posRadius.z());
                const double dist = std::sqrt(dx * dx + dz * dz);

                if (dist > r_s) {
                    const double deltaY = 2.0 * std::sqrt(r_s * (dist - r_s));
                    y += static_cast<float>(deltaY) - 3e10f;
                } else {
                    y += 2.0f * static_cast<float>(r_s) - 3e10f;
                }
            }
            cachedGridMesh_.vertices.emplace_back(worldX, y, worldZ);
        }
    }

    for (int z = 0; z < kGridSize; ++z) {
        for (int x = 0; x < kGridSize; ++x) {
            const unsigned int i = static_cast<unsigned int>(z * (kGridSize + 1) + x);
            cachedGridMesh_.indices.push_back(i);
            cachedGridMesh_.indices.push_back(i + 1);

            cachedGridMesh_.indices.push_back(i);
            cachedGridMesh_.indices.push_back(i + kGridSize + 1);
        }
    }
}

} // namespace engine
