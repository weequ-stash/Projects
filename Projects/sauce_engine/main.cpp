#include <raylib.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

float GetDistanceSq(const Vector3& A, const Vector3& B) {
    return (B.x - A.x) * (B.x - A.x) + (B.y - A.y) * (B.y - A.y) + (B.z - A.z) * (B.z - A.z);
}
float Q_rsqrt(float number) {
    if(number == 0) return 0;

    std::uint32_t i;
    float x2, y;
    const float threehalfs = 1.5f;

    x2 = number * 0.5f;
    y = number;
    i = std::bit_cast<std::uint32_t>(y); // (not so) evil floating point bit level hacking
    i = 0x5f3759df - (i >> 1);           // what the fuck?
    y = std::bit_cast<float>(i);
    y = y * (threehalfs - (x2 * y * y)); // 1st iteration

    return y;
}

Vector3 GetDirection(const Vector3& A, const Vector3& B) {
    Vector3 d = {B.x - A.x,
                 B.y - A.y,
                 B.z - A.z};

    float rsqrt = Q_rsqrt((d.x * d.x) +
                          (d.y * d.y) +
                          (d.z * d.z));

    // dir.x = cos(yaw) * cos(pitch)
    // dir.y = sin(pitch)
    // dir.z = sin(yaw) * cos(pitch)
    return {d.x * rsqrt,
            d.y * rsqrt,
            d.z * rsqrt};
}

class GameObject {
public:
    bool isMovable = 1;
    bool isAirborn = 1;

    size_t id; // index in engine's object vector
    Color color;

    float mass;
    Vector3 worldpos;
    Vector3 vel;
    Vector3 acc;
    Vector3 dims;     // object dimentions (please initialize object using this param, bbox is calculated automatically)
    BoundingBox bbox; // relative bounding box (calculated automatically)

    Texture texture;
};

class PhysicsEngine {
private:
    int m_tickrate = 60;
    float m_valuePerFrame = 1.0f / m_tickrate; // always equals to (1 / m_tickrate), used to normalize values that need to be updated per frame
    float m_frametimeAccumulator = 0.0f;

    std::vector<std::unique_ptr<GameObject>> objects;

    std::vector<std::tuple<float, bool, size_t>> collisionMap[3]; // {pos, start(0)/end(1), id}, 0 - x, 1 - y, 2 - z

    // A -> startpos of the Force vector, B -> endpos of the Force vector
    void _ApplyForce(size_t id, const Vector3& A, const Vector3& B, float value) {
        Vector3 dir = GetDirection(A, B);

        objects[id]->acc.x += dir.x * value / objects[id]->mass;
        objects[id]->acc.y += dir.y * value / objects[id]->mass;
        objects[id]->acc.z += dir.z * value / objects[id]->mass;
    }

    void _ResetForces() {
        for(auto& obj : objects) {
            obj->acc = {0, 0, 0};
        }
    }

    void _CalculateBBoxes() {
        for(auto& obj : objects) {
            obj->bbox.min = {std::min(obj->worldpos.x - obj->dims.x / 2, obj->worldpos.x + obj->dims.x / 2),
                             std::min(obj->worldpos.y - obj->dims.y / 2, obj->worldpos.y + obj->dims.y / 2),
                             std::min(obj->worldpos.z - obj->dims.z / 2, obj->worldpos.z + obj->dims.z / 2)};
            obj->bbox.max = {std::max(obj->worldpos.x - obj->dims.x / 2, obj->worldpos.x + obj->dims.x / 2),
                             std::max(obj->worldpos.y - obj->dims.y / 2, obj->worldpos.y + obj->dims.y / 2),
                             std::max(obj->worldpos.z - obj->dims.z / 2, obj->worldpos.z + obj->dims.z / 2)};
        }
    }

    void _CollisionBuild3DMap() {
        if(objects.empty()) return;

        for(auto dim : {0, 1, 2}) collisionMap[dim].clear();

        // reserve space
        for(auto dim : {0, 1, 2})
            collisionMap[dim].reserve(objects.size() * 2);

        // build the map
        for(auto it = objects.begin(); it != objects.end(); ++it) {
            collisionMap[0].emplace_back((*it)->bbox.min.x, 0, (*it)->id);
            collisionMap[1].emplace_back((*it)->bbox.min.y, 0, (*it)->id);
            collisionMap[2].emplace_back((*it)->bbox.min.z, 0, (*it)->id);

            collisionMap[0].emplace_back((*it)->bbox.max.x, 1, (*it)->id);
            collisionMap[1].emplace_back((*it)->bbox.max.y, 1, (*it)->id);
            collisionMap[2].emplace_back((*it)->bbox.max.z, 1, (*it)->id);
        }
        for(auto dim : {0, 1, 2}) {
            sort(collisionMap[dim].begin(), collisionMap[dim].end());
        }
    }

    void ResolveCollision() {
        static std::set<std::pair<size_t, size_t>> ids[3];
        static std::set<size_t> active;

        static size_t cur;

        for(auto dim : {0, 1, 2}) {
            ids[dim].clear();
            active.clear();

            active.insert(std::get<2>(collisionMap[dim][0]));

            for(size_t i = 1; i < collisionMap[dim].size(); ++i) {
                cur = std::get<2>(collisionMap[dim][i]);

                if(active.contains(cur)) {
                    active.erase(active.find(cur));
                    continue;
                }

                for(auto& act : active) {
                    ids[dim].insert({std::min(act, cur), std::max(act, cur)});
                }

                active.insert(cur);
            }
        }

        // First version - union find -> calculate the center of mass for the object clusters (objects can touch indirectly)
        static std::vector<size_t> rep; // representative of id (union find)
        rep.resize(objects.size());
        std::iota(rep.begin(), rep.end(), 0);

        static auto GetRep = [](size_t id, auto&& self) -> size_t { // Get the representative of id (union find)
            if(rep[id] == id) return id;

            return rep[id] = self(rep[id], self);
        };

        // build rep
        for(auto it = ids[0].begin(); it != ids[0].end(); ++it) {
            if(ids[1].contains(*it) && ids[2].contains(*it)) {
                rep[std::max(it->first, it->second)] = std::min(it->first, it->second);
            }
        }

        static std::map<size_t, std::vector<size_t>> clusters;
        clusters.clear();

        // build clusters
        for(size_t id = 0; id < objects.size(); ++id) {
            rep[id] = GetRep(id, GetRep);

            clusters[rep[id]].emplace_back(id);
        }

        for(auto& obj : objects) {
            if(obj->isMovable) {
                obj->color = RED; //* debug
            }
        }

        // compute for the every cluster
        for(auto& [_, cluster] : clusters) {
            if(cluster.size() <= 1) continue;

            Vector3 mass_center = {0, 0, 0};
            float mass_sum = 0;
            for(auto& id : cluster) {
                mass_sum += objects[id]->mass;
                mass_center.x += objects[id]->worldpos.x * objects[id]->mass;
                mass_center.y += objects[id]->worldpos.y * objects[id]->mass;
                mass_center.z += objects[id]->worldpos.z * objects[id]->mass;
            }

            for(auto& id : cluster) {
                if(!objects[id]->isMovable) continue;

                objects[id]->color = YELLOW; //* debug

                Vector3 other = mass_center;
                other.x -= objects[id]->worldpos.x * objects[id]->mass;
                other.y -= objects[id]->worldpos.y * objects[id]->mass;
                other.z -= objects[id]->worldpos.z * objects[id]->mass;

                // other.x /= (cluster.size() - 1);
                // other.y /= (cluster.size() - 1);
                // other.z /= (cluster.size() - 1);

                other.x /= (mass_sum - objects[id]->mass);
                other.y /= (mass_sum - objects[id]->mass);
                other.z /= (mass_sum - objects[id]->mass);

                // TODO: make function to get the projection of the surface OR change the behavior to track which edge collides instead of the worldpos (so that collision works properly for large shapes, ex. floor)
                _ApplyForce(id, other, objects[id]->worldpos, Q_rsqrt(GetDistanceSq(objects[id]->worldpos, other)) * (mass_sum - objects[id]->mass) * COLLISION_MULTIPLIER);
            }
        }

        // TODO optional: Second version - calculate center of mass only for objects that are touching directly
    }

    void HandleAirbornState() {
        for(auto& obj : objects) {
            if(obj->isMovable) continue; // we check from the "ground's" point of view

            for(size_t i = 0; i < collisionMap[1].size(); ++i) {
                if(std::get<1>(collisionMap[1][i]) == 1) continue; // TODO: consider changing

                objects[std::get<2>(collisionMap[1][i])]->isAirborn = true;
                // check if close enough (y)
                if(std::get<0>(collisionMap[1][i]) > obj->bbox.max.y && std::get<0>(collisionMap[1][i]) - 0.1f <= obj->bbox.max.y) {
                    objects[std::get<2>(collisionMap[1][i])]->isAirborn = false;
                }
            }

            obj->isAirborn = false;
        }
    }

    void CalculateGravity() {
        for(auto& obj : objects) {
            if(!obj->isAirborn || !obj->isMovable) continue; // continue, NOT A FUCKING RETURN YOU DUMBASS (the only contributor to the code is myself btw)

            _ApplyForce(obj->id, {0, 0, 0}, {0, -1, 0}, GRAVITY * 5); // TODO optional: change the multiplier
        }
    }

    void CalculateFriction() {
        for(auto& obj : objects) {
            _ApplyForce(obj->id, obj->vel, {0, 0, 0}, GetDistanceSq({0, 0, 0}, obj->vel) * FRICTION_MULTIPLIER); // TODO: tune/change the force value
        }
    }

    void PhysicsStep() {
        // calculate stuff
        _CalculateBBoxes();
        _ResetForces();
        _CollisionBuild3DMap();

        HandleAirbornState();
        ResolveCollision(); // TODO: make so that collision is calculated for the future frame and if something goes through a wall/floor, it prevents that (somethinig like wishpos)
        CalculateGravity();
        CalculateFriction();

        // apply stuff

        for(auto& obj : objects) {
            obj->vel.x += obj->acc.x * m_valuePerFrame;
            obj->vel.y += obj->acc.y * m_valuePerFrame;
            obj->vel.z += obj->acc.z * m_valuePerFrame;

            obj->worldpos.x += obj->vel.x;
            obj->worldpos.y += obj->vel.y;
            obj->worldpos.z += obj->vel.z;

            // round up to zero
            obj->vel.x = obj->vel.x < 0.0001f ? 0 : obj->vel.x;
            obj->vel.y = obj->vel.y < 0.0001f ? 0 : obj->vel.y;
            obj->vel.z = obj->vel.z < 0.0001f ? 0 : obj->vel.z;
        }
    }

public:
    // physic "constants"
    float GRAVITY = 9.81f;
    float FRICTION_MULTIPLIER = 10.0f;
    float COLLISION_MULTIPLIER = 0.7f;

    size_t NewGameObject(GameObject&& obj) { // TODO: something better ig
        objects.emplace_back(std::make_unique<GameObject>(std::move(obj)));
        objects.back()->id = objects.size() - 1;

        _CalculateBBoxes();
        _CollisionBuild3DMap();

        return objects.back()->id;
    }
    size_t NewGameObject(std::vector<GameObject>&& objs) { // returns a pointer to the first element that has been added
        size_t size_before = objects.size();
        for(auto&& obj : objs) {
            objects.emplace_back(std::make_unique<GameObject>(std::move(obj)));
            objects.back()->id = objects.size() - 1;
        }

        _CalculateBBoxes();
        _CollisionBuild3DMap();

        return objects.at(size_before)->id;
    }
    void DeleteObject(const size_t id) {
        objects.at(id) = std::move(objects.back());
        objects.at(id)->id = id;
        objects.pop_back();
    }

    void CalculateNextFrame() {
        m_frametimeAccumulator += GetFrameTime();
        while(m_frametimeAccumulator >= m_valuePerFrame) {
            PhysicsStep();
            m_frametimeAccumulator -= m_valuePerFrame;
        }
    }

    void DrawNextFrame(Camera3D& camera) {
        for(auto& obj : objects) DrawGameObject(obj->id, camera);
    }

    void DrawGameObject(const size_t id, Camera3D& camera) {
        if(!objects.at(id)->texture.id) {
            DrawCubeV(objects.at(id)->worldpos, objects.at(id)->dims, objects.at(id)->color);
            DrawCubeV(objects.at(id)->bbox.min,
                      {0.1, 0.1, 0.1},
                      YELLOW);
            DrawCubeV(objects.at(id)->bbox.max,
                      {0.1, 0.1, 0.1},
                      BLUE);
        }
        else {
            DrawBillboard(camera, objects.at(id)->texture, objects.at(id)->worldpos, 1.0f, objects.at(id)->color);
        }
    }
};

int main() {
    InitWindow(1600, 1000, "Sauce Engine");
    SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor()));

    Camera3D camera = {0};
    camera.position = (Vector3){0.0f, 10.0f, 10.0f}; // Camera position
    camera.target = (Vector3){0.0f, 0.0f, 0.0f};     // Camera looking at point
    camera.up = (Vector3){0.0f, 1.0f, 0.0f};         // Camera up vector (rotation towards target)
    camera.fovy = 90.0f;                             // Camera field-of-view Y
    camera.projection = CAMERA_PERSPECTIVE;

    PhysicsEngine engine;

    size_t plane_id = engine.NewGameObject({.isMovable = 0,
                                            .color = DARKGRAY,
                                            .mass = 10,
                                            .worldpos = {0, -5.5, 0},
                                            .dims = {100, 10, 100}});

    while(!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(WHITE);

        BeginMode3D(camera);

        if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            HideCursor();
            UpdateCamera(&camera, CAMERA_FREE);
        }
        else {
            ShowCursor();
        }

        if(IsKeyPressed(KEY_F)) {
            engine.NewGameObject({.color = RED,
                                  .mass = 10,
                                  .worldpos = {
                                      camera.target.x,
                                      camera.target.y,
                                      camera.target.z,
                                  },
                                  .dims = {1, 1, 1}});
        }

        engine.CalculateNextFrame();

        DrawGrid(10, 1);

        engine.DrawNextFrame(camera);

        EndMode3D();
        EndDrawing();
    }

    CloseWindow();
    return 0;
}