/* Calendar math, event queue and graphics primitives. */
#include "core/datetime.h"
#include "core/evq.h"
#include "gfx/gfx.h"
#include "tst.h"

extern const uint8_t font5x7[95][5];

TEST(datetime_epoch_zero_is_thursday_1970)
{
    datetime_t dt;
    dt_from_epoch(0, &dt);
    CHECK_EQ(dt.year, 1970);
    CHECK_EQ(dt.month, 1);
    CHECK_EQ(dt.day, 1);
    CHECK_EQ(dt.hour, 0);
    CHECK_STR(dt_wday_name(dt.wday), "THU");
}

TEST(datetime_known_dates)
{
    datetime_t dt;
    dt_from_epoch(1767225600u, &dt); /* 2026-01-01 00:00:00 */
    CHECK_EQ(dt.year, 2026);
    CHECK_EQ(dt.month, 1);
    CHECK_EQ(dt.day, 1);
    CHECK_STR(dt_wday_name(dt.wday), "THU");

    datetime_t in = { .year = 2026, .month = 10, .day = 13, .hour = 14, .minute = 7, .second = 59 };
    uint32_t e = dt_to_epoch(&in);
    dt_from_epoch(e, &dt);
    CHECK_EQ(dt.hour, 14);
    CHECK_EQ(dt.minute, 7);
    CHECK_EQ(dt.second, 59);
    CHECK_STR(dt_wday_name(dt.wday), "TUE");
    CHECK_STR(dt_month_name(dt.month), "OCT");

    datetime_t leap = { .year = 2024, .month = 2, .day = 29, .hour = 23, .minute = 59, .second = 59 };
    dt_from_epoch(dt_to_epoch(&leap) + 1, &dt);
    CHECK_EQ(dt.month, 3);
    CHECK_EQ(dt.day, 1);
    CHECK_EQ(dt.hour, 0);
}

TEST(datetime_round_trip_every_day_2000_to_2100)
{
    datetime_t dt;
    int32_t first = dt_days_from_civil(2000, 1, 1);
    int32_t last = dt_days_from_civil(2100, 12, 31);
    uint8_t expect_wday = 5; /* 2000-01-01 was a Saturday */
    for (int32_t d = first; d <= last; d++) {
        uint32_t e = (uint32_t)d * 86400u + 12345;
        dt_from_epoch(e, &dt);
        if (dt_to_epoch(&dt) != e || dt.wday != expect_wday) {
            CHECK(dt_to_epoch(&dt) == e);
            CHECK_EQ(dt.wday, expect_wday);
            return;
        }
        expect_wday = (uint8_t)((expect_wday + 1) % 7);
    }
}

TEST(datetime_leap_years_and_month_lengths)
{
    CHECK(dt_is_leap(2000));
    CHECK(dt_is_leap(2024));
    CHECK(!dt_is_leap(2100));
    CHECK(!dt_is_leap(2026));
    CHECK_EQ(dt_days_in_month(2024, 2), 29);
    CHECK_EQ(dt_days_in_month(2026, 2), 28);
    CHECK_EQ(dt_days_in_month(2026, 4), 30);
    CHECK_EQ(dt_days_in_month(2026, 12), 31);
    CHECK_EQ(dt_days_in_month(2026, 13), 0);
}

TEST(datetime_normalize_clamps_invalid_fields)
{
    datetime_t dt = { .year = 2026, .month = 2, .day = 31, .hour = 25, .minute = 61 };
    CHECK(!dt_normalize(&dt));
    CHECK_EQ(dt.day, 28);
    CHECK_EQ(dt.hour, 23);
    CHECK_EQ(dt.minute, 59);
    datetime_t ok = { .year = 2026, .month = 10, .day = 13, .hour = 9 };
    CHECK(dt_normalize(&ok));
    CHECK_STR(dt_wday_name(ok.wday), "TUE");
}

TEST(evq_is_fifo_and_counts_drops)
{
    evq_t q;
    evq_init(&q);
    for (int i = 0; i < EVQ_CAPACITY; i++) {
        CHECK(evq_push(&q, (event_t){ .type = EV_TICK, .u32 = (uint32_t)i }));
    }
    CHECK(!evq_push(&q, (event_t){ .type = EV_TICK }));
    CHECK_EQ(q.dropped, 1);
    event_t ev;
    for (int i = 0; i < 5; i++) {
        REQUIRE(evq_pop(&q, &ev));
        CHECK_EQ(ev.u32, i);
    }
    for (int i = 0; i < 5; i++) {
        CHECK(evq_push(&q, (event_t){ .type = EV_STEPS, .u32 = 100u + (uint32_t)i }));
    }
    int n = 0;
    uint32_t prev = 0;
    while (evq_pop(&q, &ev)) {
        CHECK(n == 0 || ev.u32 > prev);
        prev = ev.u32;
        n++;
    }
    CHECK_EQ(n, EVQ_CAPACITY);
}

TEST(gfx_pixels_and_clipping)
{
    static gfx_t g;
    gfx_clear(&g);
    CHECK_EQ(gfx_count_black(&g), 0);
    gfx_pixel(&g, 0, 0, true);
    gfx_pixel(&g, 199, 199, true);
    gfx_pixel(&g, -1, 5, true);
    gfx_pixel(&g, 200, 5, true);
    gfx_pixel(&g, 5, 200, true);
    CHECK_EQ(gfx_count_black(&g), 2);
    CHECK(gfx_get(&g, 0, 0));
    CHECK_EQ(g.px[0], 0x80); /* MSB = leftmost pixel */
    CHECK(gfx_get(&g, 199, 199));
    CHECK(!gfx_get(&g, -5, -5));

    gfx_clear(&g);
    gfx_fill(&g, 190, 190, 20, 20, true); /* clipped to 10x10 */
    CHECK_EQ(gfx_count_black(&g), 100);
    gfx_invert(&g, 0, 0, GFX_W, GFX_H);
    CHECK_EQ(gfx_count_black(&g), GFX_W * GFX_H - 100);
}

TEST(gfx_rect_line_and_progress)
{
    static gfx_t g;
    gfx_clear(&g);
    gfx_rect(&g, 10, 10, 10, 5, true);
    CHECK_EQ(gfx_count_black(&g), 2 * 10 + 2 * 3);
    gfx_clear(&g);
    gfx_line(&g, 0, 0, 9, 9, true);
    CHECK_EQ(gfx_count_black(&g), 10);
    CHECK(gfx_get(&g, 5, 5));
    gfx_clear(&g);
    gfx_progress(&g, 0, 0, 104, 10, 500);
    CHECK(gfx_get(&g, 2 + 49, 5));
    CHECK(!gfx_get(&g, 2 + 51, 5));
}

TEST(gfx_font_glyphs_fit_5x7)
{
    for (int c = 0; c < 95; c++) {
        int pixels = 0;
        for (int col = 0; col < 5; col++) {
            CHECK((font5x7[c][col] & 0x80) == 0);
            pixels += __builtin_popcount(font5x7[c][col]);
        }
        if (c == 0) {
            CHECK_EQ(pixels, 0);
        } else {
            CHECK(pixels > 0);
        }
    }
}

TEST(gfx_text_width_and_rendering)
{
    static gfx_t g;
    CHECK_EQ(gfx_text_width("", 1), 0);
    CHECK_EQ(gfx_text_width("ABC", 1), 17);
    CHECK_EQ(gfx_text_width("ABC", 2), 34);
    gfx_clear(&g);
    int end = gfx_text(&g, 0, 0, "I", 1, true);
    CHECK_EQ(end, 6);
    CHECK_EQ(gfx_count_black(&g), 11);
    gfx_clear(&g);
    gfx_text(&g, 0, 0, "I", 3, true);
    CHECK_EQ(gfx_count_black(&g), 11 * 9);
}

TEST(gfx_seven_segment_digits_are_distinct)
{
    static gfx_t a, b;
    uint32_t counts[10];
    for (int d = 0; d < 10; d++) {
        gfx_clear(&a);
        gfx_seg_digit(&a, 0, 0, 38, 84, 9, d);
        counts[d] = gfx_count_black(&a);
        CHECK(counts[d] > 0);
        for (int e = 0; e < d; e++) {
            gfx_clear(&b);
            gfx_seg_digit(&b, 0, 0, 38, 84, 9, e);
            CHECK(memcmp(a.px, b.px, GFX_BYTES) != 0);
        }
    }
    CHECK(counts[8] > counts[0]);
    CHECK(counts[1] < counts[7]);
    gfx_clear(&a);
    gfx_seg_digit(&a, 0, 0, 38, 84, 9, 10);
    CHECK_EQ(gfx_count_black(&a), 0);
}
