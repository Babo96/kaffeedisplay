#include "member_order.hpp"

#include <algorithm>
#include <utility>

namespace kaffee::ui {

std::vector<User> order_members(std::vector<User> users, const std::map<std::string, int>& usage,
                                UserSortOrder sort) {
    if (sort != UserSortOrder::Usage || usage.empty()) return users;

    auto count_of = [&usage](const User& user) {
        auto it = usage.find(user.id);
        return it == usage.end() ? 0 : it->second;
    };
    // stable_sort keeps the join order among members with identical counts and
    // names; the name tie-break then makes the result independent of the input.
    std::stable_sort(users.begin(), users.end(), [&count_of](const User& a, const User& b) {
        const int count_a = count_of(a);
        const int count_b = count_of(b);
        if (count_a != count_b) return count_a > count_b;
        return a.name < b.name;
    });
    return users;
}

}  // namespace kaffee::ui
