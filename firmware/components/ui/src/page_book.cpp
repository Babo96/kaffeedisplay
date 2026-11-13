#include <algorithm>
#include <string>
#include <vector>

#include "avatar_art_generated.hpp"
#include "format.hpp"
#include "member_order.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_internal.hpp"

namespace kaffee::ui {

// Each preset is a distinct 38x38 I4 indexed illustration with an ARGB8888
// palette, held in flash and drawn at runtime; no decoder or upload is involved.
// Users without a preset fall back to coloured initials. Shared by the booking
// page and the leaderboard so a user looks the same on every page.
lv_obj_t* make_user_avatar(lv_obj_t* parent, const std::string& name, const std::string& color,
                           int size, int avatar_id) {
    const lv_image_dsc_t* artwork = avatar_art(avatar_id);
    const bool preset = artwork != nullptr;
    const lv_color_t fill =
        preset ? theme::color(theme::COLOR_SURFACE) : parse_hex_color(color, theme::COLOR_ACCENT);
    lv_obj_t* circle = theme::make_circle(parent, size, fill);

    if (preset) {
        // The I4 plate is baked to the card surface colour, so the disc blends
        // into the circle and only the motif is visible. The name is always
        // shown next to the avatar, so no initials badge is drawn on top: at
        // 42-46 px it read as a notch cut out of the artwork.
        lv_obj_set_style_clip_corner(circle, true, 0);
        lv_obj_t* image = lv_image_create(circle);
        lv_image_set_src(image, artwork);
        // The artwork is authored at 38 px. Scale it down rather than letting
        // the circle clip the motif on smaller rows.
        if (artwork->header.w != 0 && static_cast<int>(artwork->header.w) > size) {
            lv_image_set_scale(image,
                               static_cast<uint32_t>(size) * LV_SCALE_NONE / artwork->header.w);
        }
        lv_obj_center(image);
        lv_obj_set_clickable(image, false);
        lv_obj_set_scrollable(image, false);
    } else {
        lv_obj_t* text = theme::make_label(circle, initials(name).c_str(), theme::font_body_bold(),
                                           theme::color(theme::COLOR_BG));
        lv_obj_center(text);
    }
    return circle;
}

MemberOrder UiAppImpl::ordered_members() const {
    MemberOrder order;
    for (const User& user : backend_.users()) {
        if (user.active) order.users.push_back(user);
    }
    if (backend_.app_settings().user_sort != UserSortOrder::Usage) return order;

    // The month only needs a plausible wall clock; `trusted` tracks NTP quality
    // and must not disable ordering, because a browser-synced kiosk reports
    // time_trusted=0. An unset clock (0) keeps the join order.
    const TimeInfo time = backend_.time_info();
    if (time.now <= 0) return order;

    Timestamp from = 0;
    Timestamp to = 0;
    month_bounds(time.now, 0, &from, &to);
    if (to <= from) return order;
    const Leaderboard board = backend_.leaderboard(from, to);
    for (const LeaderboardEntry& entry : board.entries) {
        order.usage[entry.user_id] = entry.count;
    }
    order.users = order_members(std::move(order.users), order.usage, UserSortOrder::Usage);
    return order;
}

void UiAppImpl::build_page_book(lv_obj_t* tile) {
    build_header(tile, strings::BOOK_TITLE, &clock_labels_[0]);

    book_body_ = lv_obj_create(tile);
    // 392 tall (56..448) so the last tile row ends above the page dots; see
    // docs/tasks/T-013-page1-layout.md.
    lv_obj_set_size(book_body_, lv_pct(100), 392);
    lv_obj_set_pos(book_body_, 0, 56);
    theme::make_transparent(book_body_);
    lv_obj_set_flex_flow(book_body_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(book_body_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(book_body_, 16, 0);
    lv_obj_set_style_pad_right(book_body_, 16, 0);
    lv_obj_set_style_pad_top(book_body_, 8, 0);
    lv_obj_set_style_pad_bottom(book_body_, 8, 0);
    lv_obj_set_style_pad_row(book_body_, 8, 0);
    lv_obj_set_scroll_dir(book_body_, LV_DIR_VER);
    // The member list must show that it continues: a slim scrollbar appears only
    // when the grid is taller than the body (30+ members) and costs no objects.
    lv_obj_set_scrollbar_mode(book_body_, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_style_width(book_body_, 4, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(book_body_, theme::color(theme::COLOR_DIM), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(book_body_, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(book_body_, 2, LV_PART_SCROLLBAR);

    refresh_book_page();
}

void UiAppImpl::refresh_book_page() {
    if (book_body_ == nullptr) return;
    balance_label_ = nullptr;
    // The guest action is a child of the page tile, not of book_body_, so
    // lv_obj_clean() below does not remove it.
    if (guest_button_ != nullptr) {
        lv_obj_delete(guest_button_);
        guest_button_ = nullptr;
    }
    last_book_fingerprint_ = book_catalog_fingerprint();
    lv_obj_clean(book_body_);

    const MemberOrder members = ordered_members();
    const std::vector<User>& active_users = members.users;
    const bool guest_enabled = backend_.app_settings().guest_enabled;
    if (!guest_enabled) guest_mode_ = false;

    auto user =
        selected_user_id_.empty() ? std::optional<User>{} : backend_.find_user(selected_user_id_);
    if (user && !user->active) user.reset();
    if (!user) selected_user_id_.clear();

    if (guest_mode_ || user) {
        lv_obj_t* card = theme::make_card(book_body_);
        lv_obj_set_size(card, lv_pct(100), 72);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(card, 10, 0);
        lv_obj_set_style_pad_column(card, 10, 0);

        if (guest_mode_) {
            make_user_avatar(card, strings::BOOK_GUEST, "#D4A373", 46);
        } else {
            make_user_avatar(card, user->name, user->color, 46, user->avatar_id);
        }

        lv_obj_t* names = theme::make_column(card, 0);
        lv_obj_set_width(names, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(names, 1);
        lv_obj_set_flex_align(names, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_t* card_name =
            theme::make_label(names, guest_mode_ ? strings::BOOK_GUEST : user->name.c_str(),
                              theme::font_title(), theme::color(theme::COLOR_TEXT));
        // Names may be up to 40 characters; the avatar takes 56 px of the row.
        theme::set_ellipsis(card_name);
        if (guest_mode_) {
            theme::make_label(names, strings::BOOK_GUEST_HINT, theme::font_small(),
                              theme::color(theme::COLOR_MUTED));
        } else {
            Cents saldo = backend_.balance(user->id);
            balance_label_ = theme::make_label(
                names, format_balance(saldo).c_str(), theme::font_body_bold(),
                theme::color(saldo < 0 ? theme::COLOR_NEGATIVE : theme::COLOR_POSITIVE));
        }

        lv_obj_t* change = theme::make_button(card, strings::BOOK_CHANGE_USER, false);
        lv_obj_set_size(change, 104, 40);
        lv_obj_add_event_cb(change, &UiAppImpl::change_user_cb, LV_EVENT_CLICKED, this);

        lv_obj_t* choose =
            theme::make_label(book_body_, strings::BOOK_PICK_DRINK, theme::font_body_bold(),
                              theme::color(theme::COLOR_MUTED));
        (void)choose;

        std::vector<Product> products;
        for (const Product& product : backend_.products()) {
            if (product.active) products.push_back(product);
        }
        std::sort(products.begin(), products.end(), [](const Product& a, const Product& b) {
            if (a.sort != b.sort) return a.sort < b.sort;
            return a.name < b.name;
        });

        if (products.empty()) {
            theme::make_label(book_body_, strings::BOOK_NO_PRODUCTS, theme::font_body(),
                              theme::color(theme::COLOR_MUTED));
            return;
        }

        lv_obj_t* grid = lv_obj_create(book_body_);
        theme::make_transparent(grid);
        lv_obj_set_clickable(grid, false);
        lv_obj_set_width(grid, lv_pct(100));
        lv_obj_set_height(grid, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_column(grid, 8, 0);
        lv_obj_set_style_pad_row(grid, 8, 0);
        theme::hide_scrollbars(grid);

        for (const Product& product : products) {
            lv_obj_t* item = lv_obj_create(grid);
            lv_obj_set_size(item, lv_pct(48), 76);
            lv_obj_set_style_bg_color(item, theme::color(theme::COLOR_SURFACE), 0);
            lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(item, 18, 0);
            lv_obj_set_style_border_width(item, 0, 0);
            lv_obj_set_style_pad_all(item, 10, 0);
            theme::hide_scrollbars(item);
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(item, 10, 0);
            theme::set_pressed_feedback(item, theme::color(theme::COLOR_SURFACE_ALT));

            lv_obj_t* badge =
                theme::make_circle(item, 38, parse_hex_color(product.color, theme::COLOR_ACCENT));
            lv_obj_t* badge_text =
                theme::make_label(badge, initials(product.name).c_str(), theme::font_body_bold(),
                                  theme::color(theme::COLOR_BG));
            lv_obj_center(badge_text);

            lv_obj_t* text = theme::make_column(item, 0);
            lv_obj_set_width(text, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(text, 1);
            lv_obj_set_flex_align(text, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_t* name = theme::make_label(text, product.name.c_str(), theme::font_body(),
                                               theme::color(theme::COLOR_TEXT));
            theme::set_ellipsis(name);
            theme::make_label(text, format_money(product.price_cents).c_str(), theme::font_small(),
                              theme::color(theme::COLOR_MUTED));

            auto* ctx = new IdContext{this, product.id};
            lv_obj_add_event_cb(item, &UiAppImpl::product_click_cb, LV_EVENT_CLICKED, ctx);
            lv_obj_add_event_cb(item, &UiAppImpl::delete_context_cb, LV_EVENT_DELETE, ctx);
        }
        return;
    }

    theme::make_label(book_body_, strings::BOOK_QUESTION, theme::font_title(),
                      theme::color(theme::COLOR_TEXT));

    // The guest action is pinned to the header band (between the title and the
    // clock) instead of trailing the member grid: at 30 members a trailing tile
    // sits more than a screen below the fold, and a question-row button would
    // cost the fourth tile row (see docs/tasks/T-013-page1-layout.md section 6).
    if (guest_enabled && book_body_ != nullptr) {
        guest_button_ =
            theme::make_button(lv_obj_get_parent(book_body_), strings::BOOK_GUEST, false);
        lv_obj_set_pos(guest_button_, 256, 8);
        lv_obj_set_size(guest_button_, 96, 40);
        lv_obj_set_style_border_width(guest_button_, 1, 0);
        lv_obj_set_style_border_color(guest_button_, theme::color(theme::COLOR_ACCENT), 0);
        lv_obj_add_event_cb(guest_button_, &UiAppImpl::guest_click_cb, LV_EVENT_CLICKED, this);
    }

    if (active_users.empty()) {
        lv_obj_t* empty = theme::make_card(book_body_);
        lv_obj_set_size(empty, lv_pct(100), 150);
        lv_obj_set_flex_flow(empty, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(empty, 18, 0);
        lv_obj_t* text = theme::make_label(empty, strings::BOOK_EMPTY, theme::font_body(),
                                           theme::color(theme::COLOR_MUTED));
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
        theme::set_wrapped(text);
    }

    lv_obj_t* grid = lv_obj_create(book_body_);
    theme::make_transparent(grid);
    lv_obj_set_clickable(grid, false);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_set_style_pad_row(grid, 8, 0);
    theme::hide_scrollbars(grid);

    for (const User& member : active_users) {
        lv_obj_t* item = lv_obj_create(grid);
        // 4 columns of 106 px + 3 gaps of 8 px = 448 px of content.
        lv_obj_set_size(item, 106, 80);
        lv_obj_set_style_bg_color(item, theme::color(theme::COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(item, 20, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 5, 0);
        theme::hide_scrollbars(item);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(item, 5, 0);
        theme::set_pressed_feedback(item, theme::color(theme::COLOR_SURFACE_ALT));

        make_user_avatar(item, member.name, member.color, 42, member.avatar_id);
        lv_obj_t* name = theme::make_label(item, member.name.c_str(), theme::font_body(),
                                           theme::color(theme::COLOR_TEXT));
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        theme::set_ellipsis(name);

        auto* ctx = new IdContext{this, member.id};
        lv_obj_add_event_cb(item, &UiAppImpl::user_click_cb, LV_EVENT_CLICKED, ctx);
        lv_obj_add_event_cb(item, &UiAppImpl::delete_context_cb, LV_EVENT_DELETE, ctx);
    }
}

void UiAppImpl::user_click_cb(lv_event_t* event) {
    auto* ctx = static_cast<IdContext*>(lv_event_get_user_data(event));
    if (ctx == nullptr || ctx->app == nullptr) return;
    auto user = ctx->app->backend_.find_user(ctx->id);
    if (user) ctx->app->select_user(*user);
}

void UiAppImpl::product_click_cb(lv_event_t* event) {
    auto* ctx = static_cast<IdContext*>(lv_event_get_user_data(event));
    if (ctx == nullptr || ctx->app == nullptr) return;
    auto product = ctx->app->backend_.find_product(ctx->id);
    if (product) ctx->app->book_product(*product);
}

void UiAppImpl::guest_click_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app != nullptr) app->select_guest();
}

void UiAppImpl::change_user_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app != nullptr) app->clear_selection();
}

}  // namespace kaffee::ui
