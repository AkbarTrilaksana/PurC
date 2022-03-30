#include "purc.h"

#include "private/runloop.h"
#include "private/interpreter.h"
#include "private/timer.h"

#include <gtest/gtest.h>


void one_shot_timer_fire(const char* id, void* ctxt)
{
    UNUSED_PARAM(id);
    UNUSED_PARAM(ctxt);
    pcrunloop_stop(pcrunloop_get_current());
}

void interval_timer_fire(const char* id, void* ctxt)
{
    static int i = 0;
    UNUSED_PARAM(id);
    UNUSED_PARAM(ctxt);
    if (i > 5) {
        pcrunloop_stop(pcrunloop_get_current());
    }
    i++;
}

TEST(timer, oneShot)
{
    purc_instance_extra_info info = {};
    int ret = 0;
    bool cleanup = false;

    // initial purc
    ret = purc_init_ex (PURC_MODULE_HVML, "cn.fmsoft.hybridos.test",
            "test_init", &info);

    ASSERT_EQ (ret, PURC_ERROR_OK);

    pcintr_timer_t timer = pcintr_timer_create("oneShot", NULL, one_shot_timer_fire);
    ASSERT_NE (timer, nullptr);

    pcintr_timer_set_interval(timer, 200);
    pcintr_timer_start_oneshot(timer);

    purc_run(PURC_VARIANT_INVALID, NULL);

    pcintr_timer_destroy(timer);

    cleanup = purc_cleanup ();
    ASSERT_EQ (cleanup, true);
}

TEST(timer, interval)
{
    purc_instance_extra_info info = {};
    int ret = 0;
    bool cleanup = false;

    // initial purc
    ret = purc_init_ex (PURC_MODULE_HVML, "cn.fmsoft.hybridos.test",
            "test_init", &info);

    ASSERT_EQ (ret, PURC_ERROR_OK);

    pcintr_timer_t timer = pcintr_timer_create("oneShot", NULL, interval_timer_fire);
    ASSERT_NE (timer, nullptr);

    pcintr_timer_set_interval(timer, 100);
    pcintr_timer_start(timer);

    purc_run(PURC_VARIANT_INVALID, NULL);

    pcintr_timer_destroy(timer);

    cleanup = purc_cleanup ();
    ASSERT_EQ (cleanup, true);
}

TEST(TIMER, init)
{
#if 0
    const char* hvml =
        "<hvml><body><a><b><c></c></b></a></body></hvml>";

    purc_instance_extra_info info = {};
    int ret = 0;
    bool cleanup = false;

    // initial purc
    ret = purc_init_ex (PURC_MODULE_HVML, "cn.fmsoft.hybridos.test",
            "test_init", &info);
    ASSERT_EQ (ret, PURC_ERROR_OK);

    // get statitics information
    const struct purc_variant_stat * stat = purc_variant_usage_stat ();
    ASSERT_NE(stat, nullptr);

    purc_vdom_t vdom = purc_load_hvml_from_string(hvml);
    ASSERT_NE(vdom, nullptr);

    bool init = pcintr_init_timers(vdom);
    ASSERT_EQ(init, true);

    purc_run(PURC_VARIANT_INVALID, NULL);

    cleanup = purc_cleanup ();
    ASSERT_EQ (cleanup, true);
#endif // 0
}
