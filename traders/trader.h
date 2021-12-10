//
// Created by henry on 06/12/2021.
//

#ifndef CPPBAZAARBOT_TRADER_H
#define CPPBAZAARBOT_TRADER_H

#include <utility>

#include "inventory.h"
#include "../common/messages.h"

#include "../auction/auction_house.h"
#include "../metrics/metrics.h"

namespace {
    double PositionInRange(double value, double min, double max) {
        value -= min;
        max -= min;
        min = 0;
        value = (value / (max - min));

        if (value < 0) { value = 0; }
        if (value > 1) { value = 1; }

        return value;
    };
}

class Role {
private:
    // rng_gen
    std::mersenne_twister_engine<uint_fast32_t, 32, 624, 397, 31, 0x9908b0dfUL, 11, 0xffffffffUL, 7, 0x9d2c5680UL, 15, 0xefc60000UL, 18, 1812433253UL> rng_gen = std::mt19937(std::random_device()());

public:
    bool Random(double chance);
    virtual void TickRole(BasicTrader& trader) = 0;
    void Produce(BasicTrader& trader, std::string commodity, int amount, double chance = 1);
    void Consume(BasicTrader& trader, std::string commodity, int amount, double chance = 1);
    void LoseMoney(BasicTrader& trader, double amount);
};


class BasicTrader : public Agent {
private:
    bool initialised = false;

    std::optional<std::shared_ptr<Role>> logic;
    double MIN_PRICE = 0.01;
    int ticks = 0;
    friend AuctionHouse;
    Inventory _inventory;
    // TODO Change this to an array to allow multiple auction houses to be traded with at once
    std::weak_ptr<AuctionHouse> auction_house;

    std::vector<Message> inbox;
    std::vector<std::pair<int, Message>> outbox;

    std::map<std::string, std::vector<double>> _observedTradingRange;
    double profit_last_round = 0;
    int  external_lookback = 15; //history range (num ticks)
    int internal_lookback = 50; //history range (num trades)

public:
    std::string class_name; // eg "Farmer", "Woodcutter" etc. Auction House will verify this on registration. (TODO)
    double money;

    bool destroyed = false;
    double money_last_round = 0;
    double profit = 0;

    double track_costs;

    ConsoleLogger logger;

public:
    BasicTrader(int id, std::weak_ptr<AuctionHouse> auction_house_ptr, std::optional<std::shared_ptr<Role>> AI_logic, std::string class_name, double starting_money, double inv_capacity, const std::vector<InventoryItem> &starting_inv, Log::LogLevel log_level = Log::WARN)
    : Agent(id)
    , auction_house(std::move(auction_house_ptr))
    , logic(std::move(AI_logic))
    , class_name(class_name)
    , money(starting_money)
    , logger(ConsoleLogger(class_name+std::to_string(id), log_level)){
        //construct inv
        _inventory = Inventory(inv_capacity, starting_inv);
        for (const auto &item : starting_inv) {
            double base_price = auction_house.lock()->AverageHistoricalPrice(item.name, external_lookback);
            _observedTradingRange[item.name] = {base_price, base_price*3};
            _inventory.SetCost(item.name, base_price);
        }


        track_costs = 0;
    }
    // Messaging functions
    void ReceiveMessage(Message incoming_message) override;

    void SendMessage(Message& outgoing_message, int recipient) override;

    void FlushOutbox();
    void FlushInbox();

    void ProcessBidResult(Message& message);
    void ProcessAskResult(Message& message);
    void ProcessRegistrationResponse(Message& message);

    // Inventory functions
    bool HasMoney(double quantity) override;
    double TryTakeMoney(double quantity, bool atomic) override;
    void ForceTakeMoney(double quantity);
    void AddMoney(double quantity) override;

    bool HasCommodity(std::string commodity, int quantity) override;
    int TryTakeCommodity(std::string commodity, int quantity, std::optional<double> unit_price, bool atomic) override;
    int TryAddCommodity(std::string commodity, int quantity, std::optional<double> unit_price, bool atomic) override;

    int Query(const std::string& name) override;
    double QueryCost(const std::string& name) override;
    double GetEmptySpace() override;

    // Trading functions
    void UpdatePriceModelFromBid(BidResult& result);
    void UpdatePriceModelFromAsk(AskResult result);

    void GenerateOffers(std::string commodity);
    BidOffer CreateBid(std::string commodity, int max_limit);
    AskOffer CreateAsk(std::string commodity, int min_limit);

    int DetermineBuyQuantity(std::string commodity);
    int DetermineSaleQuantity(std::string commodity);

    std::pair<double, double> ObserveTradingRange(std::string commodity, int window);

    double GetProfit();
    // Misc
    void Destroy();
    void Tick();
};

void BasicTrader::ReceiveMessage(Message incoming_message) {
    logger.LogReceived(incoming_message.sender_id, Log::DEBUG, incoming_message.ToString());
    inbox.push_back(incoming_message);
}
void BasicTrader::SendMessage(Message& outgoing_message, int recipient) {
    outbox.emplace_back(recipient, std::move(outgoing_message));
}
void BasicTrader::FlushOutbox() {
        logger.Log(Log::DEBUG, "Flushing outbox");
        while (!outbox.empty()) {
            auto& outgoing = outbox.back();
            outbox.pop_back();
            // Trader can currently only talk to auction houses (not other traders)
            if (outgoing.first != auction_house.lock()->id) {
                logger.Log(Log::ERROR, "Failed to send message, unknown recipient " + std::to_string(outgoing.first));
                continue;
            }
            logger.LogSent(outgoing.first, Log::DEBUG, outgoing.second.ToString());
            auction_house.lock()->ReceiveMessage(std::move(outgoing.second));
        }
        logger.Log(Log::DEBUG, "Flush finished");
}
void BasicTrader::FlushInbox() {
    logger.Log(Log::DEBUG, "Flushing inbox");
    while (!inbox.empty()) {
        auto& incoming_message = inbox.back();
        if (incoming_message.GetType() == Msg::EMPTY) {
            //no-op
        } else if (incoming_message.GetType() == Msg::BID_RESULT) {
            ProcessBidResult(incoming_message);
        } else if (incoming_message.GetType() == Msg::ASK_RESULT) {
            ProcessAskResult(incoming_message);
        } else if (incoming_message.GetType() == Msg::REGISTER_RESPONSE) {
            ProcessRegistrationResponse(incoming_message);
        } else {
            std::cout << "Unknown/unsupported message type " << incoming_message.GetType() << std::endl;
        }
        inbox.pop_back();
    }
    logger.Log(Log::DEBUG, "Flush finished");
}
void BasicTrader::ProcessAskResult(Message& message) {
    UpdatePriceModelFromAsk(*message.ask_result);
}
void BasicTrader::ProcessBidResult(Message& message) {
    UpdatePriceModelFromBid(*message.bid_result);
}
void BasicTrader::ProcessRegistrationResponse(Message& message) {
    if (message.register_response->accepted) {
        initialised = true;
        logger.Log(Log::INFO, "Successfully registered with auction house");
    } else {
        logger.Log(Log::ERROR, "Failed to register with auction house");
        Destroy();
    }
}

bool BasicTrader::HasMoney(double quantity) {
    return (money >= quantity);
}
double BasicTrader::TryTakeMoney(double quantity, bool atomic) {
    double amount_transferred = 0;
    if (!atomic) {
        // Take what you can
        amount_transferred = std::min(money, quantity);
    } else {
        if (money < quantity) {
            logger.Log(Log::DEBUG, "Failed to take $"+std::to_string(quantity));
            amount_transferred = 0;
        } else {
            amount_transferred = quantity;
        }
    }
    money -= amount_transferred;
    return amount_transferred;
}
void BasicTrader::ForceTakeMoney(double quantity) {
    logger.Log(Log::DEBUG, "Lost money: $" + std::to_string(quantity));
    money -= quantity;
}
void BasicTrader::AddMoney(double quantity) {
    logger.Log(Log::DEBUG, "Gained money: $" + std::to_string(quantity));
    money += quantity;
}

bool BasicTrader::HasCommodity(std::string commodity, int quantity) {
    auto stored = _inventory.Query(commodity);
    return (stored >= quantity);
}
int BasicTrader::TryTakeCommodity(std::string commodity, int quantity, std::optional<double> unit_price, bool atomic) {
    int actual_transferred = 0;
    auto comm = _inventory.GetItem(commodity);
    if (!comm) {
        //item unknown, fail
        logger.Log(Log::ERROR, "Tried to take unknown item "+commodity);
        return 0;
    }

    auto stored = _inventory.Query(commodity);
    if ( stored>= quantity) {
        actual_transferred = quantity;
    } else {
        if (atomic) {
            actual_transferred = 0;
            logger.Log(Log::DEBUG, "Failed to take "+commodity+std::string(" x") + std::to_string(quantity));
        } else {
            actual_transferred = stored;
        }
    }
    _inventory.TakeItem(commodity, actual_transferred, unit_price);
    return actual_transferred;
}
int BasicTrader::TryAddCommodity(std::string commodity, int quantity, std::optional<double> unit_price, bool atomic) {
    int actual_transferred = 0;
    auto comm = _inventory.GetItem(commodity);
    if (!comm) {
        //item unknown, fail
        logger.Log(Log::ERROR, "Tried to add unknown item "+commodity);
        return 0;
    }

    if (_inventory.GetEmptySpace() >= quantity*comm->size) {
        actual_transferred = quantity;
    } else {
        if (atomic) {
            actual_transferred = 0;
            logger.Log(Log::DEBUG, "Failed to add "+commodity+std::string(" x") + std::to_string(quantity));
        } else {
            actual_transferred = (int) (_inventory.GetEmptySpace()/comm->size);
        }
    }
    _inventory.AddItem(commodity, actual_transferred, unit_price);
    return actual_transferred;
}

int BasicTrader::Query(const std::string& name) { return _inventory.Query(name); }
double BasicTrader::QueryCost(const std::string& name) { return _inventory.QueryCost(name); }
double BasicTrader::GetEmptySpace() { return _inventory.GetEmptySpace(); }

// Trading functions
void BasicTrader::UpdatePriceModelFromBid(BidResult& result) {
    for (int i = 0; i < result.quantity_traded; i++) {
        _observedTradingRange[result.commodity].push_back(result.avg_price);
    }

    while (_observedTradingRange[result.commodity].size() > internal_lookback) {
        _observedTradingRange[result.commodity].erase(_observedTradingRange[result.commodity].begin());
    }
};
void BasicTrader::UpdatePriceModelFromAsk(AskResult result) {
    for (int i = 0; i < result.quantity_traded; i++) {
        _observedTradingRange[result.commodity].push_back(result.avg_price);
    }

    while (_observedTradingRange[result.commodity].size() > internal_lookback) {
        _observedTradingRange[result.commodity].erase(_observedTradingRange[result.commodity].begin());
    }
};

void BasicTrader::GenerateOffers(std::string commodity) {
    int surplus = _inventory.Surplus(commodity);
    if (surplus >= 1) {
        auto offer = CreateAsk(commodity, 1);
        if (offer.quantity > 0) {
            SendMessage(*Message(id).AddAskOffer(offer), auction_house.lock()->id);
        }
    }

    int shortage = _inventory.Shortage(commodity);
    double space = _inventory.GetEmptySpace();
    double unit_size = _inventory.GetSize(commodity);

    if (shortage > 0 && space >= unit_size) {
        int limit = (shortage*unit_size <= space) ? shortage : (int) space/shortage;
        if (limit > 0)
        {
            auto offer = CreateBid(commodity, limit);
            if (offer.quantity > 0) {
                SendMessage(*Message(id).AddBidOffer(offer), auction_house.lock()->id);
            }
        }
    }
};
BidOffer BasicTrader::CreateBid(std::string commodity, int max_limit) {
    //AI agents offer a fair bid price - 5% above recent average market value
    double bid_price = 1.05* (auction_house.lock()->AverageHistoricalPrice(commodity, external_lookback));
    int ideal = DetermineBuyQuantity(commodity);

    //can't buy more than limit
    int quantity = ideal > max_limit ? max_limit : ideal;
    //note that this could be a noop (quantity=0) at this point
    return BidOffer(id, commodity, quantity, bid_price);
}
AskOffer BasicTrader::CreateAsk(std::string commodity, int min_limit) {
    //AI agents offer a fair ask price - costs + 2% profit
    double ask_price = _inventory.QueryCost(commodity) * 1.02;

    int quantity = DetermineSaleQuantity(commodity);
    //can't sell less than limit
    quantity = quantity < min_limit ? min_limit : quantity;
    return AskOffer(id, commodity, quantity, ask_price);
};

int BasicTrader::DetermineBuyQuantity(std::string commodity) {
    double avg_price = auction_house.lock()->AverageHistoricalPrice(commodity, external_lookback);
    std::pair<double, double> range = ObserveTradingRange(commodity, internal_lookback);
    if (range.first == 0 && range.second == 0) {
        //uninitialised range
        logger.Log(Log::WARN, "Tried to make bid with unitialised trading range");
        return 0;
    }
    double favorability = PositionInRange(avg_price, range.first, range.second);
    favorability = 1 - favorability; //do 1 - favorability to see how close we are to the low end
    double amount_to_buy = favorability * _inventory.Shortage(commodity);//double

    return (int) amount_to_buy;
}
int BasicTrader::DetermineSaleQuantity(std::string commodity) {
    return _inventory.Surplus(commodity); //Sell all surplus
};

std::pair<double, double> BasicTrader::ObserveTradingRange(std::string commodity, int window) {
    if (_observedTradingRange.count(commodity) < 1 || _observedTradingRange[commodity].size() < 1) {
        return {0,0};
    }
    double min_observed = _observedTradingRange[commodity][0];
    double max_observed = _observedTradingRange[commodity][0];
    window = std::min(window, (int) _observedTradingRange[commodity].size());

    for (int i = 0; i < window; i++) {
        min_observed = std::min(min_observed,_observedTradingRange[commodity][i]);
        max_observed = std::max(max_observed,_observedTradingRange[commodity][i]);
    }
    return {min_observed, max_observed};
};

double BasicTrader::GetProfit() {return money - money_last_round;}
// Misc
void BasicTrader::Destroy() {
    auto res = auction_house.lock();
    if (res) {
        res->ReceiveMessage(*Message(id).AddShutdownNotify({id}));
    }
    destroyed = true;
    logger.Log(Log::INFO, class_name+std::to_string(id)+std::string(" destroyed."));
    _inventory.inventory.clear();
    auction_house.reset();
}
void BasicTrader::Tick() {
    money_last_round = money;
    FlushInbox();
    if (initialised) {
        if (logic) {
            logger.Log(Log::DEBUG, "Ticking internal logic");
            (*logic)->TickRole(*this);
        }
        for (const auto& commodity : _inventory.inventory) {
            GenerateOffers(commodity.first);
        }
    }
    if (money < 0) {
        Destroy();
        return;
    }
    FlushOutbox();
    if (initialised){
        ticks++;
    }
}



bool Role::Random(double chance) {
    if (chance >= 1) return true;
    return (rng_gen() < chance*rng_gen.max());
}
void Role::Produce(BasicTrader& trader, std::string commodity, int amount, double chance) {
    if (Random(chance)) {
        trader.logger.Log(Log::DEBUG, "Produced " + std::string(commodity) + std::string(" x") + std::to_string(amount));

        if (trader.track_costs < 1) trader.track_costs = 1;
        trader.TryAddCommodity(commodity, amount, trader.track_costs /  amount, false);
        trader.track_costs = 0;
    }
}
void Role::Consume(BasicTrader& trader, std::string commodity, int amount, double chance) {
    if (Random(chance)) {
        trader.logger.Log(Log::DEBUG, "Consumed " + std::string(commodity) + std::string(" x") + std::to_string(amount));
        trader.TryTakeCommodity(commodity, amount, 0, false);
        trader.track_costs += amount*trader.QueryCost(commodity);
    }
}
void Role::LoseMoney(BasicTrader& trader, double amount) {
    trader.ForceTakeMoney(amount);
    trader.track_costs += amount;
}

class EmptyRole : public Role {
    void TickRole(BasicTrader& trader) override {};
};

class RoleFarmer : public Role {
public:
    void TickRole(BasicTrader& trader) override {
        bool has_wood = (0 < trader.Query("wood"));
        bool has_tools = (0 < trader.Query("tools"));

        if (!has_wood) {
            LoseMoney(trader, 2);//$2 idleness fine
            return;
        }

        if (has_tools) {
            // 10% chance tools break
            Consume(trader, "tools", 1, 0.1);
            Consume(trader, "wood", 1);
            Produce(trader, "food", 6);
        } else {
            Consume(trader, "wood", 1);
            Produce(trader, "food", 3);
        }
    }
};

class RoleWoodcutter : public Role {
public:
    void TickRole(BasicTrader& trader) override {
        bool has_food = (0 < trader.Query("food"));
        bool has_tools = (0 < trader.Query("tools"));

        if (!has_food) {
            LoseMoney(trader, 2);//$2 idleness fine
            return;
        }

        if (has_tools) {
            // 10% chance tools break
            Consume(trader, "tools", 1, 0.1);
            Consume(trader, "food", 1);
            Produce(trader, "wood", 2);
        } else {
            Consume(trader, "food", 1);
            Produce(trader, "wood", 1);
        }
    }
};

#endif//CPPBAZAARBOT_TRADER_H
