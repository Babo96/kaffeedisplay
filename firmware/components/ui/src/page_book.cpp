#include <algorithm>
#include <string>
#include <vector>

#include "format.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_internal.hpp"

namespace kaffee::ui {

namespace {

lv_obj_t* make_avatar(lv_obj_t* parent, const std::string& name, const std::string& color,
                      int size) {
    lv_obj_t* circle =
        theme::make_circle(parent, size, parse_hex_color(color, theme::COLOR_ACCENT));
    lv_obj_t* text = theme::make_label(circle, initials(name).c_str(), theme::font_body_bold(),
                                       theme::color(theme::COLOR_BG));
    lv_obj_center(text);
    return circle;
}

}  // namespace

void UiAppImpl::build_page_book(lv_obj_t* tile) {
    build_header(tile, strings::BOOK_TITLE, &clock_labels_[0]);

    book_body_ = lv_obj_create(tile);
    lv_obj_set_size(book_body_, lv_pct(100), 424);
    lv_obj_set_pos(book_body_, 0, 56);
    theme::make_transparent(book_body_);
    lv_obj_set_flex_flow(book_body_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(book_body_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(book_body_, 16, 0);
    lv_obj_set_style_pad_right(book_body_, 16, 0);
    lv_obj_set_style_pad_top(book_body_, 8, 0);
    lv_obj_set_style_pad_bottom(book_body_, 46, 0);
    lv_obj_set_style_pad_row(book_body_, 8, 0);
    lv_obj_set_scroll_dir(book_body_, LV_DIR_VER);
    theme::hide_scrollbars(book_body_);

    refresh_book_page();
}

void UiAppImpl::refresh_book_page() {
    if (book_body_ == nullptr) return;
    balance_label_ = nullptr;
    last_book_fingerprint_ = book_catalog_fingerprint();
    lv_obj_clean(book_body_);

    std::vector<User> active_users;
    for (const User& user : backend_.users()) {
        if (user.active) active_users.push_back(user);
    }
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

        lv_obj_t* avatar = guest_mode_ ? make_avatar(card, strings::BOOK_GUEST, "#D4A373", 46)
                                       : make_avatar(card, user->name, user->color, 46);
        (void)avatar;

        lv_obj_t* names = theme::make_column(card, 0);
        lv_obj_set_width(names, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(names, 1);
        lv_obj_set_flex_align(names, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER);
        theme::make_label(names, guest_mode_ ? strings::BOOK_GUEST : user->name.c_str(),
                          theme::font_title(), theme::color(theme::COLOR_TEXT));
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
        lv_obj_set_size(item, lv_pct(31), 100);
        lv_obj_set_style_bg_color(item, theme::color(theme::COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(item, 20, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        theme::hide_scrollbars(item);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(item, 5, 0);
        theme::set_pressed_feedback(item, theme::color(theme::COLOR_SURFACE_ALT));

        make_avatar(item, member.name, member.color, 42);
        lv_obj_t* name = theme::make_label(item, member.name.c_str(), theme::font_body(),
                                           theme::color(theme::COLOR_TEXT));
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        theme::set_ellipsis(name);

        auto* ctx = new IdContext{this, member.id};
        lv_obj_add_event_cb(item, &UiAppImpl::user_click_cb, LV_EVENT_CLICKED, ctx);
        lv_obj_add_event_cb(item, &UiAppImpl::delete_context_cb, LV_EVENT_DELETE, ctx);
    }

    if (guest_enabled) {
        lv_obj_t* item = lv_obj_create(grid);
        lv_obj_set_size(item, lv_pct(31), 100);
        lv_obj_set_style_bg_color(item, theme::color(theme::COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(item, 20, 0);
        lv_obj_set_style_border_width(item, 1, 0);
        lv_obj_set_style_border_color(item, theme::color(theme::COLOR_ACCENT), 0);
        lv_obj_set_style_pad_all(item, 8, 0);
        theme::hide_scrollbars(item);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(item, 3, 0);
        theme::set_pressed_feedback(item, theme::color(theme::COLOR_SURFACE_ALT));

        make_avatar(item, strings::BOOK_GUEST, "#D4A373", 42);
        theme::make_label(item, strings::BOOK_GUEST, theme::font_body(),
                          theme::color(theme::COLOR_ACCENT));
        theme::make_label(item, strings::BOOK_GUEST_HINT, theme::font_small(),
                          theme::color(theme::COLOR_MUTED));
        lv_obj_add_event_cb(item, &UiAppImpl::guest_click_cb, LV_EVENT_CLICKED, this);
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
