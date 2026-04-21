#ifndef CRISKY_SYSTEMS_DATA_SINK_HPP
#define CRISKY_SYSTEMS_DATA_SINK_HPP

#include <cstdio>
#include <string>
#include <vector>

// Simple CSV writer for experiment output.
// Each gym creates a DataSink that writes rows to a file in output/<gym>/.
class DataSink {
public:
    // Opens file for writing.  Writes the header row immediately.
    DataSink(const std::string& filepath, std::vector<std::string> columns);
    ~DataSink();

    DataSink(const DataSink&) = delete;
    DataSink& operator=(const DataSink&) = delete;

    // Write one row.  values.size() must equal columns.size().
    void write_row(const std::vector<double>& values);

    // Flush any buffered output.
    void flush();

    // Number of rows written so far (excluding header).
    int rows_written() const { return rows_written_; }

private:
    FILE* file_ = nullptr;
    std::vector<std::string> columns_;
    int rows_written_ = 0;
};

#endif
