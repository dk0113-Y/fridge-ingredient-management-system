#pragma once

#include <string>

#include "inventory_engine.hpp"
#include "service_config.hpp"

namespace fridge {

class LocalServiceFacade {
public:
    explicit LocalServiceFacade(LocalServiceConfig config = {});

    std::string handle_health(const InventoryEngine& engine) const;
    std::string handle_inventory(const InventoryEngine& engine) const;
    std::string handle_events(const InventoryEngine& engine) const;
    std::string handle_pending(const InventoryEngine& engine) const;
    std::string handle_confirm(InventoryEngine& engine, const PendingDecision& decision) const;
    std::string handle_manual_update(InventoryEngine& engine, const ManualInventoryUpdate& request) const;

private:
    LocalServiceConfig config_;
};

}  // namespace fridge
