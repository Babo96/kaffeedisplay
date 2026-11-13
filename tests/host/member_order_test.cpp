#include <doctest.h>

#include <map>
#include <string>
#include <vector>

#include "kaffee/domain.hpp"
#include "kaffee/settings.hpp"
#include "member_order.hpp"

using kaffee::User;
using kaffee::UserSortOrder;
using kaffee::ui::order_members;

namespace {

User make_user(std::string id, std::string name) {
    User user;
    user.id = std::move(id);
    user.name = std::move(name);
    user.color = "#D4A373";
    user.active = true;
    return user;
}

std::vector<std::string> names(const std::vector<User>& users) {
    std::vector<std::string> out;
    for (const User& user : users) out.push_back(user.name);
    return out;
}

}  // namespace

TEST_CASE("creation order is left untouched") {
    std::vector<User> users{make_user("u3", "Cem"), make_user("u1", "Ada"), make_user("u2", "Ben")};
    const std::map<std::string, int> usage{{"u1", 9}, {"u2", 4}};
    CHECK(names(order_members(users, usage, UserSortOrder::Creation)) ==
          std::vector<std::string>{"Cem", "Ada", "Ben"});
}

TEST_CASE("usage order puts the most booked members first") {
    std::vector<User> users{make_user("u1", "Ada"), make_user("u2", "Ben"), make_user("u3", "Cem")};
    const std::map<std::string, int> usage{{"u1", 2}, {"u2", 7}, {"u3", 4}};
    CHECK(names(order_members(users, usage, UserSortOrder::Usage)) ==
          std::vector<std::string>{"Ben", "Cem", "Ada"});
}

TEST_CASE("usage order breaks ties by name and sinks members without bookings") {
    std::vector<User> users{make_user("u1", "Zoe"), make_user("u2", "Ben"), make_user("u3", "Ada"),
                            make_user("u4", "Ida")};
    const std::map<std::string, int> usage{{"u1", 3}, {"u2", 3}, {"u3", 1}};
    // Zoe/Ben tie on 3 and sort by name; Ada has 1; Ida has none.
    CHECK(names(order_members(users, usage, UserSortOrder::Usage)) ==
          std::vector<std::string>{"Ben", "Zoe", "Ada", "Ida"});
}

TEST_CASE("usage order without counts keeps the join order") {
    std::vector<User> users{make_user("u1", "Cem"), make_user("u2", "Ada")};
    CHECK(names(order_members(users, {}, UserSortOrder::Usage)) ==
          std::vector<std::string>{"Cem", "Ada"});
}

TEST_CASE("archived members are ordered like any other entry") {
    // The caller filters inactive users; order_members must not resurrect them.
    std::vector<User> users{make_user("u1", "Ada"), make_user("u2", "Ben")};
    users[1].active = false;
    const std::map<std::string, int> usage{{"u1", 1}, {"u2", 5}};
    CHECK(names(order_members(users, usage, UserSortOrder::Usage)) ==
          std::vector<std::string>{"Ben", "Ada"});
}
