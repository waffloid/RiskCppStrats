#include "systems/common/data_sink.hpp"
#include "systems/common/buffer.hpp"
#include "systems/common/types.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

static void test_data_sink_basic() {
    const std::string path = "output/test/data_sink_test.csv";

    {
        DataSink sink(path, {"tick", "value", "count"});
        sink.write_row({1.0, 3.14, 42.0});
        sink.write_row({2.0, 2.718, 100.0});
        assert(sink.rows_written() == 2);
    } // destructor flushes + closes

    // Read back and verify.
    std::ifstream in(path);
    assert(in.good());

    std::string line;
    std::getline(in, line);
    assert(line == "tick,value,count");

    std::getline(in, line);
    assert(line == "1,3.14,42");

    std::getline(in, line);
    assert(line == "2,2.718,100");

    // Clean up.
    std::filesystem::remove(path);
    std::filesystem::remove("output/test");
    std::printf("test_data_sink_basic: PASS\n");
}

static void test_buffer_basic() {
    Buffer<int> buf(0);

    // Initial read returns default.
    assert(buf.read() == 0);
    assert(!buf.has_update());

    // Write stages but doesn't publish.
    buf.write(42);
    assert(buf.has_update());
    assert(buf.read() == 0);  // still old value

    // Commit publishes.
    buf.commit();
    assert(buf.read() == 42);
    assert(!buf.has_update());

    // Double commit is safe.
    buf.commit();
    assert(buf.read() == 42);

    std::printf("test_buffer_basic: PASS\n");
}

static void test_buffer_overwrite() {
    Buffer<std::string> buf("initial");

    buf.write("first");
    buf.write("second");  // overwrites pending
    buf.commit();
    assert(buf.read() == "second");

    std::printf("test_buffer_overwrite: PASS\n");
}

static void test_types_compile() {
    // Verify contract types are well-formed.
    BuildPlan plan;
    plan.steps.push_back({0, NodeState::FACTORY});
    plan.estimated_production = 5.0f;
    assert(plan.steps.size() == 1);

    DesiredDistribution dd;
    dd.target_troops = {10, 20, 30};
    assert(dd.target_troops.size() == 3);

    TroopDemand td;
    td.demands.push_back({5, 100});
    assert(td.demands.size() == 1);

    std::printf("test_types_compile: PASS\n");
}

int main() {
    test_data_sink_basic();
    test_buffer_basic();
    test_buffer_overwrite();
    test_types_compile();
    std::printf("\nAll infrastructure tests passed.\n");
    return 0;
}
