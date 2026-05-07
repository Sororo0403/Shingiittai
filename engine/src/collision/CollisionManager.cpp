#include "CollisionManager.h"
#include <algorithm>

void CollisionManager::Clear() {
    bodies_.clear();
    nextBodyId_ = 1;
}

CollisionManager::BodyId
CollisionManager::AddBody(const CollisionManager::BodyDesc &desc) {
    Body body{};
    body.id = nextBodyId_++;
    if (nextBodyId_ == kInvalidBodyId) {
        nextBodyId_ = 1;
    }
    body.desc = desc;
    bodies_.push_back(body);
    return body.id;
}

bool CollisionManager::RemoveBody(BodyId id) {
    const auto it = std::remove_if(
        bodies_.begin(), bodies_.end(),
        [id](const Body &body) { return body.id == id; });
    if (it == bodies_.end()) {
        return false;
    }

    bodies_.erase(it, bodies_.end());
    return true;
}

bool CollisionManager::UpdateBody(BodyId id, const BodyDesc &desc) {
    Body *body = FindBody(id);
    if (body == nullptr) {
        return false;
    }

    body->desc = desc;
    return true;
}

const CollisionManager::Body *CollisionManager::GetBody(BodyId id) const {
    return FindBody(id);
}

bool CollisionManager::Test(BodyId a, BodyId b, Hit *outHit) const {
    const Body *bodyA = FindBody(a);
    const Body *bodyB = FindBody(b);
    if (bodyA == nullptr || bodyB == nullptr || !CanCollide(*bodyA, *bodyB)) {
        return false;
    }

    CollisionUtil::CollisionResult result =
        CollisionUtil::TestOBB(bodyA->desc.box, bodyB->desc.box);
    if (!result.hit) {
        return false;
    }

    if (outHit != nullptr) {
        outHit->a = bodyA->id;
        outHit->b = bodyB->id;
        outHit->result = result;
    }

    return true;
}

bool CollisionManager::QueryFirst(BodyId body, Hit &outHit) const {
    for (const Body &other : bodies_) {
        if (other.id == body) {
            continue;
        }
        if (Test(body, other.id, &outHit)) {
            return true;
        }
    }

    return false;
}

std::vector<CollisionManager::Hit> CollisionManager::Query(BodyId body) const {
    std::vector<Hit> hits;
    for (const Body &other : bodies_) {
        if (other.id == body) {
            continue;
        }

        Hit hit{};
        if (Test(body, other.id, &hit)) {
            hits.push_back(hit);
        }
    }
    return hits;
}

std::vector<CollisionManager::Hit> CollisionManager::FindPairs() const {
    std::vector<Hit> hits;
    for (size_t i = 0; i < bodies_.size(); ++i) {
        for (size_t j = i + 1; j < bodies_.size(); ++j) {
            Hit hit{};
            if (Test(bodies_[i].id, bodies_[j].id, &hit)) {
                hits.push_back(hit);
            }
        }
    }
    return hits;
}

CollisionManager::Body *CollisionManager::FindBody(BodyId id) {
    for (Body &body : bodies_) {
        if (body.id == id) {
            return &body;
        }
    }
    return nullptr;
}

const CollisionManager::Body *CollisionManager::FindBody(BodyId id) const {
    for (const Body &body : bodies_) {
        if (body.id == id) {
            return &body;
        }
    }
    return nullptr;
}

bool CollisionManager::CanCollide(const Body &a, const Body &b) const {
    if (!a.desc.isActive || !b.desc.isActive || a.id == b.id) {
        return false;
    }

    const bool aAcceptsB = (a.desc.mask & b.desc.layer) != 0;
    const bool bAcceptsA = (b.desc.mask & a.desc.layer) != 0;
    return aAcceptsB && bAcceptsA;
}
