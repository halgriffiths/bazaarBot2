//
// Created by henry on 17/12/2021.
//

#ifndef CPPBAZAARBOT_DISPLAY_H
#define CPPBAZAARBOT_DISPLAY_H
#include "metrics.h"

#if defined(__linux__)
#include <sys/ioctl.h>

#include <utility>
#endif // Windows/Linux
void get_terminal_size(int& width, int& height) {
#if defined(_WIN32)
    width = 100;
    height = 100;
//    CONSOLE_SCREEN_BUFFER_INFO csbi;
//    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
//    width = (int)(csbi.srWindow.Right-csbi.srWindow.Left+1);
//    height = (int)(csbi.srWindow.Bottom-csbi.srWindow.Top+1);
#elif defined(__linux__)
    struct winsize w;
    ioctl(fileno(stdout), TIOCGWINSZ, &w);
    width = (int)(w.ws_col);
    height = (int)(w.ws_row);
#endif // Windows/Linux
}


class GlobalDisplay {
private:
    int window_ms = 20000;
    std::uint64_t start_time;
    std::uint64_t offset;
    int chart_update_ms;
    std::shared_ptr<std::mutex> file_mutex;
    std::shared_ptr<AuctionHouse> auction_house;
    std::vector<std::string> tracked_goods;
    std::map<std::string, bool> visible;
    std::map<std::string, std::tuple<std::string, std::string>> hardcoded_legend;

    std::thread chart_thread;
public:
    std::atomic_bool destroyed = false;
    std::atomic_bool active = true;

    GlobalDisplay(std::uint64_t start_time, std::shared_ptr<AuctionHouse> auction_house, double chart_update_ms, std::shared_ptr<std::mutex> mutex, const std::vector<std::string>& tracked_goods)
            : start_time(start_time)
            , auction_house(auction_house)
            , chart_update_ms(chart_update_ms)
            , file_mutex(std::move(mutex))
            , tracked_goods(tracked_goods)
            , chart_thread([this] { Tick(); }) {
        offset = to_unix_timestamp_ms(std::chrono::system_clock::now()) - start_time;
        for (auto& good : tracked_goods) {
            visible[good] = true;
        }
        hardcoded_legend = {};
        hardcoded_legend["food"] = {R"(\*)", "\x1b[1;32m*\x1b[0m"};
        hardcoded_legend["wood"] = {"#", "\x1b[1;33m#\x1b[0m"};
        hardcoded_legend["fertilizer"] = {R"(\$)", "\x1b[1;35m$\x1b[0m"};
        hardcoded_legend["ore"] = {"%", "\x1b[1;31m%\x1b[0m"};
        hardcoded_legend["metal"] = {"@", "\x1b[1;37m@\x1b[0m"};
        hardcoded_legend["tools"] = {"&", "\x1b[1;34m&\x1b[0m"};
    }

    void Shutdown() {
        destroyed = true;
        if (chart_thread.joinable()) {
            chart_thread.join();
        }
        file_mutex.reset();
    }
    void Tick() {
        int working_frametime_ms;
        std::chrono::duration<double, std::milli> ms_double;
        std::this_thread::sleep_for(std::chrono::milliseconds{chart_update_ms});
        auto t1 = std::chrono::high_resolution_clock::now();
        while (!destroyed) {
            t1 = std::chrono::high_resolution_clock::now();

            if (active) {
                DrawChart();
                WriteFooter();
            }

            ms_double = std::chrono::high_resolution_clock::now() - t1;
            working_frametime_ms = (int) ms_double.count();
            if (working_frametime_ms < chart_update_ms) {
                std::this_thread::sleep_for(std::chrono::milliseconds{chart_update_ms - working_frametime_ms});
            } else {
                std::cout << "[ERROR][DISPLAY] User display thread overran: took " << working_frametime_ms << " ms (target: " << chart_update_ms << ")" << std::endl;
            }
        }
        std::cout << "Closing display" << std::endl;
    }

    void WriteFooter() {
#if __linux__
        auto red_start = "\033[1;31m";
        auto red_end =  "\033[0m";
        auto green_start = "\033[1;32m";
        auto green_end = "\033[0m";
#else
        auto red_start = "";
        auto red_end = "";
        auto green_start = "";
        auto green_end = "";
#endif
        for (auto& good : tracked_goods) {
            double curr_price = auction_house->MostRecentPrice(good);
            double pc_change = auction_house->t_PercentPriceChange(good, window_ms);
            std::cout << std::left << std::setw(10) << good;

            if (pc_change < 0) {
                //▼
                std::cout << " $" << curr_price << " "<< red_start <<"(▼" << pc_change << "%)" << red_end;
            } else if (pc_change > 0) {
                //▲
                std::cout << " " << "$" << curr_price << " "<< green_start <<"(▲" << pc_change << "%)" << green_end;
            } else {
                std::cout << " " << "$" << curr_price << " (" << pc_change << "%)";
            }
            std::cout << std::endl;
        }
    }
    void DrawChart(bool all = false) {
        std::string out;
#if __linux__
        int x = 100;
        int y = 100;
        get_terminal_size(x, y);
        y -= 7;//leave space for legend at bottom

        auto local_curr_time = to_unix_timestamp_ms(std::chrono::system_clock::now());
        double time_passed_s = (double)(local_curr_time - offset - start_time) / 1000;

        std::string args = "gnuplot -e \"set term dumb " + std::to_string(x)+ " " + std::to_string(y);
        args += ";set offsets 0, 0, 0, 0";
        args += ";set title 'Prices'";
        if (all) {
            args += ";set xrange [0:" + std::to_string(time_passed_s) + "]";
        } else {
            args += ";set xrange ["+ std::to_string(time_passed_s - (window_ms/1000)) + ":" + std::to_string(time_passed_s) + "]";
        }

        args += ";plot ";
        for (auto& good : tracked_goods) {
            if (visible[good]) {
                args += "'global_tmp/"+good+".dat' with lines title '" + good + "',";
            }
        }
        args += "\"";
        // GENERATE ASCII PLOT
        file_mutex->lock();
        out = GetStdoutFromCommand(args);
        file_mutex->unlock();
        // Set colors using ANSI codes
        // Could do this all in 1 pass, but O(n) + O(n) is still O(n)
        for (auto& leg : hardcoded_legend) {
            out = std::regex_replace(out, std::regex(std::get<0>(leg.second)), std::get<1>(leg.second));
        }
#else
        out = "Chart-drawing not supported on non-Linux platforms!";
#endif
        std::cout << out << std::endl;
    }
};

#endif//CPPBAZAARBOT_DISPLAY_H
