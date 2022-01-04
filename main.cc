//
// Created by henry on 06/12/2021.
//
#include <iostream>
#include "BazaarBot.h"
#include "metrics/metrics.h"
#include <vector>
#include <chrono>
#include <thread>

std::shared_ptr<AITrader> MakeAgent(const std::string& class_name, int curr_id,
                                    std::shared_ptr<AuctionHouse>& auction_house,
                                    std::map<std::string, std::vector<InventoryItem>>& inv,
                                    std::mt19937& gen) {
    Log::LogLevel LOGLEVEL = Log::WARN;
    double STARTING_MONEY = 500.0;
    double MIN_COST = 10;
    std::uniform_real_distribution<> random_money(0.5*STARTING_MONEY, 1.5*STARTING_MONEY); // define the range
    std::uniform_real_distribution<> random_cost(0.9*MIN_COST, 1.1*MIN_COST); // define the range
    if (class_name == "farmer") {
        return CreateAndRegister(curr_id, auction_house, std::make_shared<RoleFarmer>(random_cost(gen)), class_name, random_money(gen), 20, inv[class_name], LOGLEVEL);
    } else if (class_name == "woodcutter") {
        return CreateAndRegister(curr_id, auction_house, std::make_shared<RoleWoodcutter>(random_cost(gen)), class_name, random_money(gen), 20, inv[class_name], LOGLEVEL);
    } else if (class_name == "miner") {
        return CreateAndRegister(curr_id, auction_house, std::make_shared<RoleMiner>(random_cost(gen)), class_name, random_money(gen), 20, inv[class_name], LOGLEVEL);
    } else if (class_name == "refiner") {
        return CreateAndRegister(curr_id, auction_house, std::make_shared<RoleRefiner>(random_cost(gen)), class_name, random_money(gen), 20, inv[class_name], LOGLEVEL);
    } else if (class_name == "blacksmith") {
        return CreateAndRegister(curr_id, auction_house, std::make_shared<RoleBlacksmith>(random_cost(gen)), class_name, random_money(gen), 20, inv[class_name], LOGLEVEL);
    } else if (class_name == "composter") {
        return CreateAndRegister(curr_id, auction_house, std::make_shared<RoleComposter>(random_cost(gen)), class_name, random_money(gen), 20, inv[class_name], LOGLEVEL);
    } else {
        std::cout << "Error: Invalid class type passed to make_agent lambda" << std::endl;
    }
    return std::shared_ptr<AITrader>();
}

std::string ChooseNewClassRandom(std::vector<std::string>& tracked_roles, std::mt19937& gen) {
    std::uniform_int_distribution<> random_job(0, (int) tracked_roles.size() - 1); // define the range
    int new_job = random_job(gen);
    return tracked_roles[new_job];
}

int RandomChoice(int num_weights, std::vector<double>& weights, std::mt19937& gen) {
    double sum_of_weight = 0;
    for(int i=0; i<num_weights; i++) {
        sum_of_weight += weights[i];
    }
    std::uniform_real_distribution<> random(0, sum_of_weight);
    double rnd = random(gen);
    for(int i=0; i<num_weights; i++) {
        if(rnd < weights[i])
            return i;
        rnd -= weights[i];
    }
    return -1;
}

std::string GetProducer(std::string& commodity) {
    if (commodity == "food") {
        return "farmer";
    } else if (commodity == "fertilizer") {
        return "composter";
    } else if (commodity == "wood") {
        return "woodcutter";
    } else if (commodity == "ore") {
        return "miner";
    } else if (commodity == "metal") {
        return "refiner";
    } else if (commodity == "tools") {
        return "blacksmith";
    } else {
        return "null";
    }
}
std::string ChooseNewClassWeighted(std::vector<std::string>& tracked_goods, std::shared_ptr<AuctionHouse>& auction_house, std::mt19937& gen) {
    std::vector<double> weights;
    double gamma = -0.02;
    int lookback = 100;
    for (auto& commodity : tracked_goods) {
        double asks = auction_house->AverageHistoricalAsks(commodity, lookback);
        double bids = auction_house->AverageHistoricalBids(commodity, lookback);
        weights.push_back(std::exp(gamma*(asks-bids)));
    }
    int choice = RandomChoice((int) weights.size(),  weights, gen);
    assert(choice != -1);
    return GetProducer(tracked_goods[choice]);
}
void AdvanceTicks(int start_tick, int steps, int& max_id,
                  std::vector<std::string>& tracked_goods,
                  std::vector<std::string>& tracked_roles,
                  std::shared_ptr<AuctionHouse>& auction_house,
                  std::shared_ptr<FakeTrader>& fake_trader,
                  std::vector<std::shared_ptr<AITrader>>& all_traders,
                GlobalMetrics& global_metrics,
                std::mt19937& gen,
                  std::map<std::string, std::vector<InventoryItem>>& inv) {

    std::map<std::string, int> num_alive;
    for (int curr_tick = start_tick; curr_tick < start_tick+steps; curr_tick++) {
        auto t1 = std::chrono::high_resolution_clock::now();
        for (auto& role : tracked_roles) {
            num_alive[role] = 0;
        }
        fake_trader->Tick();
        //all_traders[0]->logger.verbosity = Log::INFO;
        for (int i = 0; i < all_traders.size(); i++) {
            if (!all_traders[i]->IsDestroyed()) {
                all_traders[i]->TickOnce();
                num_alive[all_traders[i]->GetClassName()] += 1;
            } else {
                //trader died, add new trader?
                global_metrics.TrackDeath(all_traders[i]->GetClassName(), all_traders[i]->ticks);
                //auto new_job = ChooseNewClassRandom(tracked_roles, gen);
                auto new_job = ChooseNewClassWeighted(tracked_goods,auction_house, gen);
                all_traders[i] = MakeAgent(new_job, max_id, auction_house, inv, gen);
                max_id++;
            }
        }
        //all_traders[0]->PrintState();
        //auction_house->Tick();

        // collect metrics
        global_metrics.CollectMetrics(auction_house, all_traders, num_alive);
        std::chrono::duration<double, std::milli> ms_double = std::chrono::high_resolution_clock::now() - t1;
        int frametime_ms = ms_double.count();
        std::cout << "Working frametime for tick " << curr_tick << ": " << frametime_ms << std::endl;

        if (frametime_ms < 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10 - frametime_ms});
        }
        ms_double = std::chrono::high_resolution_clock::now() - t1;
        frametime_ms = ms_double.count();
        std::cout << "  Final frametime for tick " << curr_tick << ": " << frametime_ms << std::endl;
    }

}


void Run(bool animation) {
    int NUM_TRADERS_EACH_TYPE = 10;
    int NUM_TICKS = (animation) ? 2000 : 1000;
    int DURATION_MS = 10000; //10 second simulation
    int WINDOW_SIZE = 100;
    int STEP_SIZE = 1;
    int STEP_PAUSE_MS = 100;
    double target_FPS = 2;

    int target_steptime;
    if (animation) {
        target_steptime = 1000/target_FPS;

    } else {
        target_steptime = 0;
    }
    using std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::duration;
    using std::chrono::milliseconds;

    std::random_device rd; // obtain a random number from hardware
    std::mt19937 gen(rd()); // seed the generator

    std::vector<std::string> tracked_goods = {"food", "wood", "fertilizer", "ore", "metal", "tools"};
    std::vector<std::string> tracked_roles = {"farmer", "woodcutter", "composter", "miner", "refiner", "blacksmith"};

    auto global_metrics = GlobalMetrics(tracked_goods, tracked_roles);

    // --- SET UP DEFAULT COMMODITIES ---
    std::map<std::string, Commodity> comm;
    {
        comm.emplace("food", Commodity("food", 0.5));
        comm.emplace("wood", Commodity("wood", 1));
        comm.emplace("ore", Commodity("ore", 1));
        comm.emplace("metal", Commodity("metal", 1));
        comm.emplace("tools", Commodity("tools", 1));
        comm.emplace("fertilizer", Commodity("fertilizer", 0.1));
    }
    // --- SET UP DEFAULT INVENTORIES ---
    std::map<std::string, std::vector<InventoryItem>> inv;
    {
        inv.emplace("farmer", std::vector<InventoryItem>{{comm["food"], 0, 0},
                                                         {comm["tools"], 1, 2},
                                                         {comm["wood"], 1, 6},
                                                         {comm["fertilizer"], 1, 6}});

        inv.emplace("miner", std::vector<InventoryItem>{{comm["food"], 1, 6},
                                                        {comm["tools"], 1, 2},
                                                        {comm["ore"], 0, 0}});

        inv.emplace("refiner", std::vector<InventoryItem>{{comm["food"], 1, 6},
                                                          {comm["tools"], 1, 2},
                                                          {comm["ore"], 1, 10},
                                                          {comm["metal"], 0, 0}});

        inv.emplace("woodcutter", std::vector<InventoryItem>{{comm["food"], 1, 6},
                                                             {comm["tools"], 1, 2},
                                                             {comm["wood"], 0, 0}});

        inv.emplace("blacksmith", std::vector<InventoryItem>{{comm["food"], 1, 6},
                                                             {comm["tools"], 0, 0},
                                                             {comm["metal"], 0, 10}});

        inv.emplace("composter", std::vector<InventoryItem>{{comm["food"], 1, 6},
                                                            {comm["fertilizer"], 0, 0}});
    }

    // --- SET UP AUCTION HOUSE ---
    int max_id = 0;
    auto auction_house = std::make_shared<AuctionHouse>(max_id, Log::WARN);
    max_id++;
    for (auto& item : comm) {
        auction_house->RegisterCommodity(item.second);
    }
    std::thread auction_house_thread(&AuctionHouse::Tick, auction_house, DURATION_MS);
    // --- SET UP AI TRADERS ---
    std::vector<std::shared_ptr<AITrader>> all_traders;
    for (int i = 0; i < NUM_TRADERS_EACH_TYPE; i++) {
        for (auto& role : tracked_roles) {
            all_traders.push_back(MakeAgent(role, max_id, auction_house, inv, gen));
            max_id++;
        }
    }

    // --- SET UP FAKE TRADER ---
    auto fake_trader = std::make_shared<FakeTrader>(max_id, auction_house);
    {
        fake_trader->SendMessage(*Message(max_id).AddRegisterRequest(std::move(RegisterRequest(max_id, fake_trader))), auction_house->id);
        fake_trader->Tick();
        //fake_trader->RegisterShortage("fertilizer", 3, 620, 50);
        //fake_trader->RegisterSurplus("fertilizer", -0.9, 220, 50);
        max_id++;
    }



    // --- MAIN LOOP ---
    std::cout << std::fixed;
    std::cout << std::setprecision(2);
    auto t1 = high_resolution_clock::now();
    for (int curr_tick = 0; curr_tick < NUM_TICKS; curr_tick += STEP_SIZE) {
        t1 = high_resolution_clock::now();
        AdvanceTicks(curr_tick, STEP_SIZE, max_id,
                tracked_goods,
                tracked_roles,
                auction_house,
                fake_trader,
                all_traders,
                global_metrics,
                 gen,
                 inv);
        if (animation && curr_tick > WINDOW_SIZE) {
            global_metrics.update_datafiles();
            display_plot(global_metrics, WINDOW_SIZE);
        }

        duration<double, std::milli> ms_double = high_resolution_clock::now() - t1;
        int steptime = ms_double.count();
        std::cout << "steptime for ticks " << curr_tick << "-" << curr_tick+STEP_SIZE << ": " << steptime << "ms\n";
        if (animation && curr_tick > WINDOW_SIZE && steptime < target_steptime) {
            std::this_thread::sleep_for(std::chrono::milliseconds(target_steptime-steptime));
        }
    }


    for (int i = 0; i < all_traders.size(); i++) {
        all_traders[i]->Shutdown();
    }
    auction_house->Shutdown();
    auction_house_thread.join();
    //Plot final results
    global_metrics.plot_verbose();
    for (auto& good : tracked_goods) {
        std::cout << "\t\t\t" << good;
    }
    std::cout << std::endl;
    for (auto& good : tracked_goods) {
        double price = auction_house->AverageHistoricalPrice(good, NUM_TICKS);

        std::cout << "\t\t$" << price;
        double pc_change = auction_house->history.prices.percentage_change(good, NUM_TICKS);
        if (pc_change < 0) {
            //▼
            std::cout << "\033[1;31m(▼" << pc_change << "%)\033[0m";
        } else if (pc_change > 0) {
            //▲
            std::cout << "\033[1;32m(▲" << pc_change << "%)\033[0m";
        } else {
            std::cout << "(" << pc_change << "%)";
        }
    }

    std::cout << "\nAverage age on death: " << global_metrics.avg_overall_age << std::endl;
    for (auto& role : tracked_roles) {
        std::cout << role << ": " << global_metrics.age_per_class[role] << "(" <<global_metrics.deaths_per_class[role] <<" total)" << std::endl;
    }
    int survivor_age = 0;
    for (auto& survivor : all_traders){
        survivor_age += survivor->ticks;
    }
    std::cout << "Survivor avg age: " << survivor_age / all_traders.size() << std::endl;
    std::cout << "Avg auction house profit :" << auction_house->spread_profit / NUM_TICKS;
}

// ---------------- MAIN ----------
int main(int argc, char *argv[]) {
    Run((argc > 1));
    return 0;
}