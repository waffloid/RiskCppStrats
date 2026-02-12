#ifndef CRISKY_VIZ_STATS_TABLE_HPP
#define CRISKY_VIZ_STATS_TABLE_HPP

#include "viz/panel.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

// Key-value stats table panel.
// Displays rows of (name, formatted value) from a data callback.
class StatsTable : public Panel {
public:
    using DataFn = std::function<std::vector<std::pair<std::string, std::string>>()>;

    StatsTable(std::string title, DataFn data_fn);

    void draw() override;
    const char* title() const override { return title_.c_str(); }

private:
    std::string title_;
    DataFn data_fn_;
};

#endif
