#include <algorithm>
#include <string>
#include <vector>

#include "format.hpp"
#include "strings.hpp"
#include "theme.hpp"
#include "ui_internal.hpp"

namespace kaffee::ui {

void UiAppImpl::build_page_stats(lv_obj_t* tile) {
    build_header(tile, strings::STATS_TITLE, &clock_labels_[1]);

    stats_body_ = lv_obj_create(tile);
    lv_obj_set_size(stats_body_, lv_pct(100), 424);
    lv_obj_set_pos(stats_body_, 0, 56);
    theme::make_transparent(stats_body_);
    lv_obj_set_flex_flow(stats_body_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stats_body_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(stats_body_, 16, 0);
    lv_obj_set_style_pad_right(stats_body_, 16, 0);
    lv_obj_set_style_pad_top(stats_body_, 4, 0);
    lv_obj_set_style_pad_bottom(stats_body_, 40, 0);
    lv_obj_set_style_pad_row(stats_body_, 8, 0);
    theme::hide_scrollbars(stats_body_);

    lv_obj_t* month_row = theme::make_row(stats_body_, 8);
    lv_obj_set_height(month_row, 48);

    stats_prev_button_ = theme::make_button(month_row, "<", false);
    lv_obj_set_size(stats_prev_button_, 48, 46);
    lv_obj_add_event_cb(stats_prev_button_, &UiAppImpl::month_prev_cb, LV_EVENT_CLICKED, this);

    stats_month_label_ =
        theme::make_label(month_row, "", theme::font_title_bold(), theme::color(theme::COLOR_TEXT));
    lv_obj_set_flex_grow(stats_month_label_, 1);
    lv_obj_set_style_text_align(stats_month_label_, LV_TEXT_ALIGN_CENTER, 0);

    stats_next_button_ = theme::make_button(month_row, ">", false);
    lv_obj_set_size(stats_next_button_, 48, 46);
    lv_obj_add_event_cb(stats_next_button_, &UiAppImpl::month_next_cb, LV_EVENT_CLICKED, this);

    stats_warning_ = theme::make_label(stats_body_, strings::STATS_TIME_WARNING,
                                       theme::font_small(), theme::color(theme::COLOR_NEGATIVE));
    theme::set_wrapped(stats_warning_);
    lv_obj_set_hidden(stats_warning_, true);

    stats_list_ = lv_obj_create(stats_body_);
    theme::make_transparent(stats_list_);
    lv_obj_set_width(stats_list_, lv_pct(100));
    lv_obj_set_flex_grow(stats_list_, 1);
    lv_obj_set_flex_flow(stats_list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(stats_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(stats_list_, 4, 0);
    lv_obj_set_scroll_dir(stats_list_, LV_DIR_VER);
    theme::hide_scrollbars(stats_list_);

    stats_guest_label_ =
        theme::make_label(stats_body_, "", theme::font_small(), theme::color(theme::COLOR_MUTED));
    (void)stats_guest_label_;

    refresh_stats_page();
}

void UiAppImpl::refresh_stats_page() {
    if (stats_list_ == nullptr) return;
    Timestamp now = backend_.time_info().now;
    lv_label_set_text(stats_month_label_, format_month(now, month_offset_).c_str());

    bool can_go_forward = month_offset_ < 0;
    if (can_go_forward) {
        lv_obj_set_clickable(stats_next_button_, true);
        lv_obj_set_style_opa(stats_next_button_, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_clickable(stats_next_button_, false);
        lv_obj_set_style_opa(stats_next_button_, LV_OPA_40, 0);
    }

    TimeInfo time = backend_.time_info();
    if (time.trusted) {
        lv_obj_set_hidden(stats_warning_, true);
    } else {
        lv_obj_set_hidden(stats_warning_, false);
    }

    Timestamp from = 0;
    Timestamp to = 0;
    month_bounds(now, month_offset_, &from, &to);
    Leaderboard board = backend_.leaderboard(from, to);

    std::vector<LeaderboardEntry> entries;
    for (const LeaderboardEntry& entry : board.entries) {
        if (entry.count > 0) entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(),
              [](const LeaderboardEntry& a, const LeaderboardEntry& b) {
                  if (a.count != b.count) return a.count > b.count;
                  return a.name < b.name;
              });

    std::string guest =
        std::string(strings::STATS_GUEST) + ": " + std::to_string(board.guest_count);
    lv_label_set_text(stats_guest_label_, guest.c_str());

    lv_obj_clean(stats_list_);
    if (entries.empty()) {
        lv_obj_t* empty = theme::make_card(stats_list_);
        lv_obj_set_size(empty, lv_pct(100), 100);
        lv_obj_set_flex_flow(empty, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        theme::make_label(empty, strings::STATS_EMPTY, theme::font_body(),
                          theme::color(theme::COLOR_MUTED));
        return;
    }

    int max_count = entries.front().count;
    const uint32_t rank_colors[3] = {theme::COLOR_GOLD, theme::COLOR_SILVER, theme::COLOR_BRONZE};
    for (size_t i = 0; i < entries.size(); ++i) {
        const LeaderboardEntry& entry = entries[i];
        lv_obj_t* row = lv_obj_create(stats_list_);
        lv_obj_set_size(row, lv_pct(100), 48);
        lv_obj_set_style_bg_color(row, theme::color(theme::COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_scrollable(row, false);
        theme::hide_scrollbars(row);

        if (i < 3) {
            lv_obj_set_style_border_width(row, 2, 0);
            lv_obj_set_style_border_color(row, theme::color(rank_colors[i]), 0);
        } else {
            lv_obj_set_style_border_width(row, 0, 0);
        }

        int bar_width = 36 + (entry.count * 250) / (max_count > 0 ? max_count : 1);
        if (bar_width > 430) bar_width = 430;
        lv_obj_t* bar = lv_obj_create(row);
        lv_obj_set_pos(bar, 0, 0);
        lv_obj_set_size(bar, bar_width, 48);
        lv_obj_set_style_radius(bar, 16, 0);
        lv_obj_set_style_bg_color(bar, theme::color(i < 3 ? rank_colors[i] : theme::COLOR_ACCENT),
                                  0);
        lv_obj_set_style_bg_opa(bar, i < 3 ? LV_OPA_30 : LV_OPA_20, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_set_clickable(bar, false);
        theme::hide_scrollbars(bar);

        lv_obj_t* content = lv_obj_create(row);
        lv_obj_set_pos(content, 0, 0);
        lv_obj_set_size(content, lv_pct(100), lv_pct(100));
        theme::make_transparent(content);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(content, 12, 0);
        lv_obj_set_style_pad_right(content, 14, 0);
        lv_obj_set_style_pad_column(content, 9, 0);
        lv_obj_set_clickable(content, false);
        theme::hide_scrollbars(content);

        lv_obj_t* rank =
            theme::make_label(content, std::to_string(i + 1).c_str(), theme::font_body_bold(),
                              theme::color(i < 3 ? rank_colors[i] : theme::COLOR_MUTED));
        lv_obj_set_width(rank, 24);

        // Same identity as the booking page: draw the preset when the user has
        // one, otherwise coloured initials. Archived users stay resolvable, so a
        // past period keeps its motifs.
        int avatar_id = 0;
        if (auto member = backend_.find_user(entry.user_id)) avatar_id = member->avatar_id;
        make_user_avatar(content, entry.name, entry.color, 38, avatar_id);

        lv_obj_t* name = theme::make_label(content, entry.name.c_str(), theme::font_body(),
                                           theme::color(theme::COLOR_TEXT));
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

        std::string count = std::to_string(entry.count) + strings::COMMON_TIMES;
        theme::make_label(content, count.c_str(), theme::font_body_bold(),
                          theme::color(theme::COLOR_TEXT));
    }
}

void UiAppImpl::month_prev_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app == nullptr) return;
    if (app->month_offset_ > -24) --app->month_offset_;
    app->refresh_stats_page();
}

void UiAppImpl::month_next_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app == nullptr) return;
    if (app->month_offset_ < 0) ++app->month_offset_;
    app->refresh_stats_page();
}

}  // namespace kaffee::ui
