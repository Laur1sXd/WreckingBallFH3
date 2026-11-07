#pragma once
#include <algorithm>
#include <vector>

namespace wb {
struct Range {
    int begin,end;
    bool operator==(const Range& other) const { return begin==other.begin && end==other.end; }
};
// Native FH3 groups can cover only part of a model batch. Preserve their
// instance indices and add only the holes, never another copy of an instance.
inline std::vector<Range> uncovered_ranges(int count,std::vector<Range> covered) {
    std::sort(covered.begin(),covered.end(),[](Range a,Range b){return a.begin<b.begin;});
    std::vector<Range> result;
    int cursor=0;
    for (auto r:covered) {
        r.begin=std::clamp(r.begin,0,count);r.end=std::clamp(r.end,0,count);
        if (r.end<=r.begin || r.end<=cursor) continue;
        if (r.begin>cursor) result.push_back({cursor,r.begin});
        cursor=std::max(cursor,r.end);
    }
    if (cursor<count) result.push_back({cursor,count});
    return result;
}
}
