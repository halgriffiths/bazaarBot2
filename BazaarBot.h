#ifndef BAZAAR_BOT_H
#define BAZAAR_BOT_H



#include "common/commodity.h"
#include "traders/inventory.h"

#include "common/agent.h"

#include "auction/auction_house.h"
#include "common/messages.h"
#include "metrics/metrics.h"
#include "traders/AI_trader.h"
#include "traders/fake_trader.h"
#include "traders/roles.h"

std::shared_ptr<AITrader> CreateAndRegisterBasic(int id,
                                                    const std::vector<std::pair<Commodity, int>>& inv,
                                                    const std::shared_ptr<AuctionHouse>& auction_house) {

    std::vector<InventoryItem> inv_vector;
    for (const auto &item : inv) {
        inv_vector.emplace_back(item.first.name, item.second);
    }
    auto trader = std::make_shared<AITrader>(id, auction_house, std::nullopt, "test_class", 100.0, 50, inv_vector, Log::WARN);

    trader->SendMessage(*Message(id).AddRegisterRequest(std::move(RegisterRequest(trader->id, trader))), auction_house->id);
    trader->Tick();
    return trader;
}
std::shared_ptr<AITrader> CreateAndRegister(int id,
                                               const std::shared_ptr<AuctionHouse>& auction_house,
                                               std::shared_ptr<Role> AI_logic,
                                               const std::string& name,
                                               double starting_money,
                                               double inv_capacity,
                                               const std::vector<InventoryItem> inv,
                                               Log::LogLevel log_level
) {

    auto trader = std::make_shared<AITrader>(id, auction_house, std::move(AI_logic), name, starting_money, inv_capacity, inv, log_level);
    trader->SendMessage(*Message(id).AddRegisterRequest(std::move(RegisterRequest(trader->id, trader))), auction_house->id);
    trader->Tick();
    return trader;
}
std::shared_ptr<AITrader> CreateAndRegisterFarmer(int id,
                                                     const std::vector<InventoryItem>& inv,
                                                     const std::shared_ptr<AuctionHouse>& auction_house,
                                                     double starting_money=100.0) {
    std::shared_ptr<Role> AI_logic;
    AI_logic = std::make_shared<RoleFarmer>();
    return CreateAndRegister(id, auction_house, AI_logic, "farmer", starting_money, 50, inv, Log::WARN);
}


#endif //BAZAAR_BOT_H
