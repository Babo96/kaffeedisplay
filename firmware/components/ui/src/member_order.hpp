#pragma once

#include <map>
#include <string>
#include <vector>

#include "kaffee/domain.hpp"
#include "kaffee/settings.hpp"

namespace kaffee::ui {

// Orders the member tiles of the booking page. Pure so it can be tested on the
// host without LVGL.
//
// `usage` maps user id -> booking count for the current month; an empty map (or
// UserSortOrder::Creation) keeps the incoming order, which is the join order.
// With UserSortOrder::Usage the members with the most bookings come first, ties
// are broken by name so the grid never shuffles between rebuilds, and members
// without a booking this month sink to the end by name.
std::vector<User> order_members(std::vector<User> users, const std::map<std::string, int>& usage,
                                UserSortOrder sort);

}  // namespace kaffee::ui
