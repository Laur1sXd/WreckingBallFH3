#include "coverage.h"
#include <cassert>
#include <iostream>
int main() {
    using wb::Range;using wb::uncovered_ranges;
    using Runs=std::vector<Range>;
    assert(uncovered_ranges(5,{})==Runs({{0,5}}));
    assert(uncovered_ranges(5,{{0,5}}).empty());
    assert(uncovered_ranges(5,{{0,2}})==Runs({{2,5}}));
    assert(uncovered_ranges(10,{{2,4},{7,9}})==Runs({{0,2},{4,7},{9,10}}));
    assert(uncovered_ranges(10,{{5,8},{2,6},{0,2},{8,10}}).empty());
    assert(uncovered_ranges(5,{{-7,2},{4,99}})==Runs({{2,4}}));
    assert(uncovered_ranges(5,{{2,2}})==Runs({{0,5}}));
    assert(uncovered_ranges(0,{}).empty());
    std::cout<<"8 partial-batch coverage cases passed\n";
}
