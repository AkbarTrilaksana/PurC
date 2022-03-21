#include "purc.h"

#include <gtest/gtest.h>

TEST(doc_var, basic)
{
    const char *test_hvml =
    "<!DOCTYPE hvml>"
    "<hvml target=\"html\" lang=\"en\">"
    "<head>"
    "    <base href=\"$HVML.base(! 'https://gitlab.fmsoft.cn/hvml/hvml-docs/raw/master/samples/calculator/' )\" />"
    ""
    "    <link rel=\"stylesheet\" type=\"text/css\" href=\"assets/calculator.css\" />"
    "        <init as=\"buttons\" uniquely>"
    "            ["
    "                { \"letters\": \"7\", \"class\": \"number\" },"
    "                { \"letters\": \"8\", \"class\": \"number\" },"
    "                { \"letters\": \"9\", \"class\": \"number\" },"
    "                { \"letters\": \"←\", \"class\": \"c_blue backspace\" },"
    "                { \"letters\": \"C\", \"class\": \"c_blue clear\" },"
    "                { \"letters\": \"4\", \"class\": \"number\" },"
    "                { \"letters\": \"5\", \"class\": \"number\" },"
    "                { \"letters\": \"6\", \"class\": \"number\" },"
    "                { \"letters\": \"×\", \"class\": \"c_blue multiplication\" },"
    "                { \"letters\": \"÷\", \"class\": \"c_blue division\" },"
    "                { \"letters\": \"1\", \"class\": \"number\" },"
    "                { \"letters\": \"2\", \"class\": \"number\" },"
    "                { \"letters\": \"3\", \"class\": \"number\" },"
    "                { \"letters\": \"+\", \"class\": \"c_blue plus\" },"
    "                { \"letters\": \"-\", \"class\": \"c_blue subtraction\" },"
    "                { \"letters\": \"0\", \"class\": \"number\" },"
    "                { \"letters\": \"00\", \"class\": \"number\" },"
    "                { \"letters\": \".\", \"class\": \"number\" },"
    "                { \"letters\": \"%\", \"class\": \"c_blue percent\" },"
    "                { \"letters\": \"=\", \"class\": \"c_yellow equal\" },"
    "            ]"
    "        </init>"
    "</head>"
    ""
    "<body>"
    "    <div id=\"calculator\">"
    ""
    "        <div value=\"assets/{$SYSTEM.locale}.json\">"
    "        </div>"
    ""
    "        <div value=\"$T.get('HVML Calculator')\">"
    "        </div>"
    ""
    "        <div>"
    "            $T.get('HVML Calculator')"
    "        </div>"
    ""
    "        <div value=\"$SYSTEM.time()\">"
    "        </div>"
    ""
    "        <div value=\"$SYSTEM.cwd\">"
    "        </div>"
    ""
    "        <div value=\"$SYSTEM.cwd(!'/tmp/')\">"
    "              set cwd to /tmp/"
    "        </div>"
    ""
    "        <div value=\"$SYSTEM.cwd\">"
    "        </div>"
    ""
    "        <div value=\"$SESSION.user\">"
    "        </div>"
    ""
    "        <div value=\"test set SESSION.user(!'abc', 123)\">"
    "            $SESSION.user(!'abc', 123)"
    "        </div>"
    ""
    "        <div value=\"$SESSION.user\">"
    "        </div>"
    ""
    "        <div value=\"$SESSION.user('abc')\">"
    "        </div>"
    ""
    "        <div value=\"$SESSION.user('abc')\">"
    "        </div>"
    ""
    "        <div value=\"$buttons[0].letters\">"
    "            <init as=\"buttons\" uniquely>"
    "                ["
    "                    { \"letters\": \"777\", \"class\": \"number\" },"
    "                ]"
    "            </init>"
    "            <div value=\"$buttons[0].letters\">"
    "            </div>"
    "        </div>"
    ""
    "    </div>"
    "</body>"
    ""
    "</hvml>";
    (void)test_hvml;

    const char *hvmls[] = {
        test_hvml,
    };

    purc_instance_extra_info info = {};
    int ret = 0;
    bool cleanup = false;

    // initial purc
    ret = purc_init_ex (PURC_MODULE_HVML, "cn.fmsoft.hybridos.test",
            "test_init", &info);

    ASSERT_EQ (ret, PURC_ERROR_OK);

    // get statitics information
    struct purc_variant_stat * stat = purc_variant_usage_stat ();
    ASSERT_NE(stat, nullptr);

    for (size_t i=0; i<PCA_TABLESIZE(hvmls); ++i) {
        const char *hvml = hvmls[i];
        purc_vdom_t vdom = purc_load_hvml_from_string(hvml);
        ASSERT_NE(vdom, nullptr);
    }

    purc_run(PURC_VARIANT_INVALID, NULL);

    cleanup = purc_cleanup ();
    ASSERT_EQ (cleanup, true);
}

