//
// Created by henry on 06/12/2021.
//

#ifndef CPPBAZAARBOT_MESSAGES_H
#define CPPBAZAARBOT_MESSAGES_H

// IWY
#include "../traders/inventory.h"
#include "../common/commodity.h"
#include <memory>

class Agent;

namespace Msg {
    enum MessageType {
        EMPTY,
        REGISTER_REQUEST,
        REGISTER_RESPONSE,
        BID_OFFER,
        ASK_OFFER,
        BID_RESULT,
        ASK_RESULT
    };
}

struct EmptyMessage {
    std::string ToString() {
        std::string output("Empty message");
        return output;
    }
};
struct RegisterRequest {
    int sender_id;
    std::weak_ptr<Agent> trader_pointer;
    RegisterRequest(int sender_id, std::weak_ptr<Agent> new_trader)
            : sender_id(sender_id)
            , trader_pointer(new_trader) {};

    std::string ToString() {
        std::string output("RegistrationRequest from ");
        output.append(std::to_string(sender_id))
                .append(" ");
        return output;
    }
};

struct RegisterResponse {
    int sender_id;
    bool accepted;
    std::optional<std::string> rejection_reason;

    RegisterResponse(int sender_id, bool accepted, std::optional<std::string> reason = std::nullopt)
            : sender_id(sender_id)
            , accepted(accepted)
            , rejection_reason(std::move(reason)) {};

    std::string ToString() {
        std::string output("RegistrationResponse from ");
        output.append(std::to_string(sender_id))
                .append(": ");
        if (accepted) {
            output.append("OK");
        } else {
            output.append("FAILED - ");
            if (rejection_reason) {
                output.append(*rejection_reason);
            }
        }
        return output;
    }
};

struct BidResult {
    int sender_id;
    std::string commodity;
    int quantity_untraded = 0;
    int quantity_traded = 0;
    double avg_price = 0;

    BidResult(int sender_id, std::string commodity)
            : sender_id(sender_id)
            , commodity(std::move(commodity)) {};

    void UpdateWithTrade(int trade_quantity, double unit_price) {
        avg_price = (avg_price*quantity_traded + unit_price*trade_quantity)/(trade_quantity + quantity_traded);
        quantity_traded += trade_quantity;
    }

    void UpdateWithNoTrade(int remainder) {
        quantity_untraded += remainder;
    }

    std::string ToString() {
        std::string output("BID RESULT from ");
        output.append(std::to_string(sender_id))
                .append(": Bought ")
                .append(commodity)
                .append(" x")
                .append(std::to_string(quantity_traded))
                .append(" @ avg price $")
                .append(std::to_string(avg_price))
                .append(" (")
                .append(std::to_string(quantity_traded))
                .append("/")
                .append(std::to_string(quantity_traded+quantity_untraded))
                .append(" bought)");
        return output;
    }
};

struct AskResult {
    int sender_id;
    std::string commodity;
    int quantity_untraded = 0;
    int quantity_traded = 0;
    double avg_price = 0;

    AskResult(int sender_id, std::string commodity)
            : sender_id(sender_id)
            , commodity(std::move(commodity)) {};

    void UpdateWithTrade(int trade_quantity, double unit_price) {
        avg_price = (avg_price*quantity_traded + unit_price*trade_quantity)/(trade_quantity + quantity_traded);
        quantity_traded += trade_quantity;
    }

    void UpdateWithNoTrade(int remainder) {
        quantity_untraded += remainder;
    }

    std::string ToString() {
        std::string output("ASK RESULT from ");
        output.append(std::to_string(sender_id))
                .append(": Sold   ")
                .append(commodity)
                .append(" x")
                .append(std::to_string(quantity_traded))
                .append(" @ avg price $")
                .append(std::to_string(avg_price))
                .append(" (")
                .append(std::to_string(quantity_traded))
                .append("/")
                .append(std::to_string(quantity_untraded+quantity_traded))
                .append(" sold)");
        return output;
    }
};

struct BidOffer {
    int sender_id;
    std::string commodity;
    int quantity;
    double unit_price;

    BidOffer(int sender_id, const std::string& commodity_name, int quantity, double unit_price)
            : sender_id(sender_id)
            , commodity(commodity_name)
            , quantity(quantity)
            , unit_price(unit_price) {};

    std::string ToString() {
        std::string output("BID from ");
        output.append(std::to_string(sender_id))
                .append(": ")
                .append(commodity)
                .append(" x")
                .append(std::to_string(quantity))
                .append(" @ $")
                .append(std::to_string(unit_price));
        return output;
    }
};

struct AskOffer {
    int sender_id;
    std::string commodity;
    int quantity;
    double unit_price;

    AskOffer(int sender_id, const std::string& commodity_name, int quantity, double unit_price)
            : sender_id(sender_id)
            , commodity(commodity_name)
            , quantity(quantity)
            , unit_price(unit_price) {};

    std::string ToString() {
        std::string output("ASK from ");
        output.append(std::to_string(sender_id))
                .append(": ")
                .append(commodity)
                .append(" x")
                .append(std::to_string(quantity))
                .append(" @ $")
                .append(std::to_string(unit_price));
        return output;
    }
};

bool operator< (const BidOffer& a, const BidOffer& b) {
    return a.unit_price < b.unit_price;
};
bool operator< (const AskOffer& a, const AskOffer& b) {
    return a.unit_price < b.unit_price;
};

class Message {
public:
    int sender_id; //originator of message
    std::optional<EmptyMessage> empty_message = std::nullopt;
    std::optional<RegisterRequest> register_request = std::nullopt;
    std::optional<RegisterResponse> register_response = std::nullopt;
    std::optional<BidOffer> bid_offer = std::nullopt;
    std::optional<AskOffer> ask_offer = std::nullopt;
    std::optional<BidResult> bid_result = std::nullopt;
    std::optional<AskResult> ask_result = std::nullopt;

    Message(int sender_id)
        : sender_id(sender_id)
        , type(Msg::EMPTY)
        , empty_message(EmptyMessage()) {};
    
    Msg::MessageType GetType() {
        return type;
    }
    Message* AddRegisterRequest(RegisterRequest msg) {
        if (type != Msg::EMPTY) {
            return this; //disallow multiple messages
        }
        type = Msg::REGISTER_REQUEST;
        register_request = std::move(msg);
        return this;
    }
    Message* AddRegisterResponse(RegisterResponse msg) {
        if (type != Msg::EMPTY) {
            return this; //disallow multiple messages
        }
        type = Msg::REGISTER_RESPONSE;
        register_response = std::move(msg);
        return this;
    }
    Message* AddBidOffer(BidOffer msg) {
        if (type != Msg::EMPTY) {
            return this; //disallow multiple messages
        }
        type = Msg::BID_OFFER;
        bid_offer = std::move(msg);
        return this;
    }
    Message* AddBidResult(BidResult msg) {
        if (type != Msg::EMPTY) {
            return this; //disallow multiple messages
        }
        type = Msg::BID_RESULT;
        bid_result = std::move(msg);
        return this;
    }
    Message* AddAskOffer(AskOffer msg) {
        if (type != Msg::EMPTY) {
            return this; //disallow multiple messages
        }
        type = Msg::ASK_OFFER;
        ask_offer = std::move(msg);
        return this;
    }
    Message* AddAskResult(AskResult msg) {
        if (type != Msg::EMPTY) {
            return this; //disallow multiple messages
        }
        type = Msg::ASK_RESULT;
        ask_result = std::move(msg);
        return this;
    }

    std::string ToString() {
        if (type == Msg::EMPTY) {
            return empty_message->ToString();
        } else if (type == Msg::REGISTER_REQUEST) {
            return register_request->ToString();
        } else if (type == Msg::REGISTER_RESPONSE) {
            return register_response->ToString();
        } else if (type == Msg::BID_OFFER) {
            return bid_offer->ToString();
        } else if (type == Msg::BID_RESULT) {
            return bid_result->ToString();
        } else if (type == Msg::ASK_OFFER) {
            return ask_offer->ToString();
        } else if (type == Msg::ASK_RESULT) {
            return ask_result->ToString();
        }
    }
    
private:
    Msg::MessageType type;
};
#endif//CPPBAZAARBOT_MESSAGES_H
