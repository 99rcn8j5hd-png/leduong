#pragma once

#include <memory>
#include "iface/snapshot.hpp"

namespace hs {
// Trả nullptr nếu build không phải iOS/macOS.
std::unique_ptr<ISnapshotProvider> make_pubg_provider(int argc, char** argv);
}
