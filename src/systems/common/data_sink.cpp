#include "systems/common/data_sink.hpp"

#include <cassert>
#include <cstring>
#include <filesystem>

DataSink::DataSink(const std::string& filepath, std::vector<std::string> columns)
    : columns_(std::move(columns)) {
    // Ensure parent directories exist.
    auto parent = std::filesystem::path(filepath).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    file_ = std::fopen(filepath.c_str(), "w");
    if (!file_) return;

    // Write header row.
    for (size_t i = 0; i < columns_.size(); i++) {
        if (i > 0) std::fputc(',', file_);
        std::fputs(columns_[i].c_str(), file_);
    }
    std::fputc('\n', file_);
}

DataSink::~DataSink() {
    if (file_) std::fclose(file_);
}

void DataSink::write_row(const std::vector<double>& values) {
    if (!file_) return;
    assert(values.size() == columns_.size());

    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) std::fputc(',', file_);
        // Use %g for compact representation; integers print without decimals.
        std::fprintf(file_, "%g", values[i]);
    }
    std::fputc('\n', file_);
    rows_written_++;
}

void DataSink::flush() {
    if (file_) std::fflush(file_);
}
