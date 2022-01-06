//
// Created by henry on 15/12/2021.
//

#ifndef CPPBAZAARBOT_METRICS_H
#define CPPBAZAARBOT_METRICS_H

#include "../traders/AI_trader.h"
# include <regex>
#include <fstream>
namespace {
    // SRC: https://www.jeremymorgan.com/tutorials/c-programming/how-to-capture-the-output-of-a-linux-command-in-c/
    std::string GetStdoutFromCommand(std::string cmd) {
        std::string data;
        FILE * stream;
        const int max_buffer = 256;
        char buffer[max_buffer];
        cmd.append(" 2>&1");

        stream = popen(cmd.c_str(), "r");

        if (stream) {
            while (!feof(stream))
                if (fgets(buffer, max_buffer, stream) != NULL) data.append(buffer);
            pclose(stream);
        }
        return data;
    }
}

class GlobalMetrics {
public:
    std::vector<std::string> tracked_goods;
    std::map<std::string, bool> visible;
    std::vector<std::string> tracked_roles;
    int total_deaths = 0;
    double avg_overall_age = 0;
    std::map<std::string, int> deaths_per_class;
    std::map<std::string, double> age_per_class;
    std::map<std::string, std::vector<std::pair<double, double>>> avg_price_metrics;

private:
    std::shared_ptr<std::mutex> file_mutex;
    std::map<std::string, std::tuple<std::string, std::string>> hardcoded_legend;
    int curr_tick = 0;
    std::uint64_t offset;
    std::uint64_t start_time;

    std::map<std::string, std::vector<std::pair<double, double>>> net_supply_metrics;
    std::map<std::string, std::vector<std::pair<double, double>>> avg_trades_metrics;
    std::map<std::string, std::vector<std::pair<double, double>>> avg_asks_metrics;
    std::map<std::string, std::vector<std::pair<double, double>>> avg_bids_metrics;
    std::map<std::string, std::vector<std::pair<double, double>>> num_alive_metrics;

    std::map<std::string, std::unique_ptr<std::ofstream>> data_files;

    int lookback = 1;
public:
    GlobalMetrics(std::uint64_t start_time, const std::vector<std::string>& tracked_goods, std::vector<std::string> tracked_roles, std::shared_ptr<std::mutex> mutex)
            : start_time(start_time)
            , tracked_goods(tracked_goods)
            , tracked_roles(tracked_roles)
            , file_mutex(mutex) {
        offset = to_unix_timestamp_ms(std::chrono::system_clock::now()) - start_time;
        init_datafiles();

        hardcoded_legend["food"] = {R"(\*)", "\x1b[1;32m*\x1b[0m"};
        hardcoded_legend["wood"] = {"#", "\x1b[1;33m#\x1b[0m"};
        hardcoded_legend["fertilizer"] = {R"(\$)", "\x1b[1;35m$\x1b[0m"};
        hardcoded_legend["ore"] = {"%", "\x1b[1;31m%\x1b[0m"};
        hardcoded_legend["metal"] = {"@", "\x1b[1;37m@\x1b[0m"};
        hardcoded_legend["tools"] = {"&", "\x1b[1;34m&\x1b[0m"};

        for (auto& good : tracked_goods) {
            visible[good] = true;
            net_supply_metrics[good] = {};
            avg_price_metrics[good] = {};
            avg_trades_metrics[good] = {};
            avg_asks_metrics[good] = {};
            avg_bids_metrics[good] = {};
        }
        for (auto& role : tracked_roles) {
            num_alive_metrics[role] = {};
            age_per_class[role] = 0;
            deaths_per_class[role] = 0;
        }
    }

    void init_datafiles() {
        file_mutex->lock();
        for (auto& good : tracked_goods) {
            data_files[good] = std::make_unique<std::ofstream>();
            data_files[good]->open(("tmp/"+good + ".dat").c_str(), std::ios::trunc);
            *(data_files[good].get()) << "# raw data file for " << good << std::endl;
            *(data_files[good].get()) << "0 0\n";
        }
        file_mutex->unlock();
    }
    void update_datafiles() {
        file_mutex->lock();
        for (auto& item : data_files) {
            item.second->close();
            item.second->open(("tmp/"+item.first + ".dat").c_str(), std::ios::app);
        }
        file_mutex->unlock();
    }
    void CollectMetrics(const std::shared_ptr<AuctionHouse>& auction_house, int num_alive) {
        auto local_curr_time = to_unix_timestamp_ms(std::chrono::system_clock::now());
        double time_passed_ms = (double)(local_curr_time - offset - start_time) / 1000;
        for (auto& good : tracked_goods) {
            double price = auction_house->MostRecentPrice(good);
            double asks = auction_house->AverageHistoricalAsks(good, lookback);
            double bids = auction_house->AverageHistoricalBids(good, lookback);
            double trades = auction_house->AverageHistoricalTrades(good, lookback);

            file_mutex->lock();
            *(data_files[good].get()) << time_passed_ms << " " << price << "\n";
            file_mutex->unlock();

            avg_price_metrics[good].emplace_back(time_passed_ms, price);
            avg_trades_metrics[good].emplace_back(time_passed_ms, trades);
            avg_asks_metrics[good].emplace_back(time_passed_ms, asks);
            avg_bids_metrics[good].emplace_back(time_passed_ms, bids);

            net_supply_metrics[good].emplace_back(time_passed_ms, asks-bids);
        }
        for (auto& role : tracked_roles) {
            num_alive_metrics[role].emplace_back(time_passed_ms, num_alive);
        }

        curr_tick++;
    }

    static void preprocess(std::vector<std::pair<double, double>>& data, int smoothing = 1) {
        for (int i = smoothing; i < data.size() - smoothing; i++) {
            double val = 0;
            for (int j = -1*smoothing; j <= smoothing; j++) {
                val += data[i+j].second;
            }
            data[i].second = val / (2*smoothing + 1);
        }
    }
    void plot_verbose() {
        int smoothing = 0;
        // Plot results
        Gnuplot gp;
        gp << "set multiplot layout 2,2\n";
        gp << "set offsets 0, 0, 0, 0\n";
        gp << "set title 'Prices'\n";
        auto plots = gnuplotio::Gnuplot::plotGroup();
        for (auto& good : tracked_goods) {
            preprocess(avg_price_metrics[good], smoothing);
            plots.add_plot1d(avg_price_metrics[good], "with lines title '"+good+std::string("'"));
        }
        gp << plots;

        gp << "set title 'Num successful trades'\n";
        plots = gnuplotio::Gnuplot::plotGroup();
        for (auto& good : tracked_goods) {
            preprocess(avg_trades_metrics[good], smoothing);
            plots.add_plot1d(avg_trades_metrics[good], "with lines title '"+good+std::string("'"));
        }
        gp << plots;

        gp << "set title 'Demographics'\n";
        plots = gnuplotio::Gnuplot::plotGroup();
        for (auto& role : tracked_roles) {
            plots.add_plot1d(num_alive_metrics[role], "with lines title '"+role+std::string("'"));
        }
        gp << plots;

        gp << "set title 'Net supply'\n";
        plots = gnuplotio::Gnuplot::plotGroup();
        for (auto& good : tracked_goods) {
            preprocess(net_supply_metrics[good], smoothing);
            plots.add_plot1d(net_supply_metrics[good], "with lines title '"+good+std::string("'"));
        }
        gp << plots;

//    gp << "set title 'Sample Trader Detail - 1'\n";
//    plots = gp.plotGroup();
//    for (auto& good : tracked_goods) {
//        plots.add_plot1d(sample1_metrics[good], "with lines title '"+good+std::string("'"));
//    }
//    plots.add_plot1d(sample1_metrics["money"], "with lines title 'money'");
//    gp << plots;
//
//    gp << "set title 'Sample Trader Detail - 2'\n";
//    plots = gp.plotGroup();
//    for (auto& good : tracked_goods) {
//        plots.add_plot1d(sample2_metrics[good], "with lines title '"+good+std::string("'"));
//    }
//    plots.add_plot1d(sample2_metrics["money"], "with lines title 'money'");
//    gp << plots;
    }

    std::string plot_terminal(int window = 0, int x = 90, int y = 30) {
        std::string args = "gnuplot -e \"set term dumb " + std::to_string(x)+ " " + std::to_string(y);
        args += ";set offsets 0, 0, 0, 0";
        args += ";set title 'Prices'";
        args += ";set xrange [" + std::to_string(curr_tick - window) + ":" + std::to_string(curr_tick) + "]";
        args += ";plot ";
        for (auto& good : tracked_goods) {
            if (visible[good]) {
                args += "'tmp/"+good+".dat' with lines title '" + good + "',";
            }
        }
        args += "\"";
        // GENERATE ASCII PLOT
        auto out = GetStdoutFromCommand(args);

        // Set colors using ANSI codes
        // Could do this all in 1 pass, but O(n) + O(n) is still O(n)
        for (auto& leg : hardcoded_legend) {
            out = std::regex_replace(out, std::regex(std::get<0>(leg.second)), std::get<1>(leg.second));
        }

        // ADD LEGEND
        for (auto& good : tracked_goods) {
            double price = avg_price_metrics[good].back().second;
            std::string price_str = std::to_string(price);
            price_str = price_str.substr(0, price_str.find('.')+3);

            double pc_change = GetPercentageChange(good, window);
            std::string pc_change_str = std::to_string(pc_change);
            pc_change_str = pc_change_str.substr(0, pc_change_str.find('.')+3);

            out += "\n";
            out += std::get<1>(hardcoded_legend[good])+std::get<1>(hardcoded_legend[good])+std::get<1>(hardcoded_legend[good])+std::get<1>(hardcoded_legend[good]);
            out += " "+good;
            out += ": $";
            out += price_str;
            if (pc_change < 0) {
                //▼
                out += " \033[1;31m(▼" + pc_change_str + "%)\033[0m";
            } else if (pc_change > 0) {
                //▲
                out += " \033[1;32m(▲" +pc_change_str + "%)\033[0m";
            } else {
                out += " (" + pc_change_str + "%) ";
            }
        }
        return out;
    }

    double GetPercentageChange(const std::string& name, int window) {
        double prev_value;
        int size = avg_price_metrics[name].size();
        if (window <= size) {
            prev_value = avg_price_metrics[name][size - window].second;
        } else {
            prev_value = avg_price_metrics[name][0].second;
        }

        double curr_value = avg_price_metrics[name].back().second;
        return 100*(curr_value- prev_value)/prev_value;
    }

    void TrackDeath(const std::string& class_name, int age) {
        avg_overall_age = (avg_overall_age*total_deaths + age)/(total_deaths+1);
        total_deaths++;

        age_per_class[class_name] = (age_per_class[class_name]*deaths_per_class[class_name] + age)/(deaths_per_class[class_name]+1);
        deaths_per_class[class_name]++;
        // TODO: Either finish this or remove it
        //std::map<std::string, double> age_per_class;
    }
};
#endif//CPPBAZAARBOT_METRICS_H
