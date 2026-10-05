#include "core/simulation/SimConstants.hpp"

#include <format>

namespace sbx::sim {

static_assert(kTickRate == 30, "틱레이트를 바꾸면 kSimVersion 을 올리고 골든 해시를 갱신해야 한다");
static_assert(isValidSnapshotRate(10) && isValidSnapshotRate(15) && isValidSnapshotRate(30));
static_assert(!isValidSnapshotRate(20) && !isValidSnapshotRate(0));

std::string describeSimulationConstants() {
    return std::format("tick {} Hz, dt {:.7f} s", kTickRate, static_cast<f64>(kFixedDt));
}

} // namespace sbx::sim
