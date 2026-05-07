#pragma once
#include "CollisionUtil.h"
#include <cstdint>
#include <vector>

class CollisionManager {
  public:
    using BodyId = uint32_t;
    using LayerMask = uint32_t;

    static constexpr BodyId kInvalidBodyId = 0;
    static constexpr LayerMask kLayerNone = 0;
    static constexpr LayerMask kLayerAll = 0xffffffffu;

    struct BodyDesc {
        OBB box{};
        LayerMask layer = kLayerAll;
        LayerMask mask = kLayerAll;
        bool isActive = true;
        bool isTrigger = true;
        const void *userData = nullptr;
    };

    struct Body {
        BodyId id = kInvalidBodyId;
        BodyDesc desc{};
    };

    struct Hit {
        BodyId a = kInvalidBodyId;
        BodyId b = kInvalidBodyId;
        CollisionUtil::CollisionResult result{};
    };

    void Clear();

    BodyId AddBody(const BodyDesc &desc);
    bool RemoveBody(BodyId id);
    bool UpdateBody(BodyId id, const BodyDesc &desc);

    const Body *GetBody(BodyId id) const;

    bool Test(BodyId a, BodyId b, Hit *outHit = nullptr) const;
    bool QueryFirst(BodyId body, Hit &outHit) const;
    std::vector<Hit> Query(BodyId body) const;
    std::vector<Hit> FindPairs() const;

  private:
    Body *FindBody(BodyId id);
    const Body *FindBody(BodyId id) const;
    bool CanCollide(const Body &a, const Body &b) const;

  private:
    std::vector<Body> bodies_;
    BodyId nextBodyId_ = 1;
};
