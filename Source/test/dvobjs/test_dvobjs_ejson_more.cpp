#include "purc-variant.h"
#include "purc-dvobjs.h"
#include "purc-ports.h"

#include "config.h"
#include "../helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <locale.h>
#include <limits.h>
#include <unistd.h>
#include <sys/time.h>

#include <gtest/gtest.h>

TEST(dvobjs, basic)
{
    purc_instance_extra_info info = {};
    int ret = purc_init_ex(PURC_MODULE_EJSON, "cn.fmsfot.hvml.test",
            "dvobjs", &info);
    ASSERT_EQ (ret, PURC_ERROR_OK);

    purc_variant_t dvobj;

    dvobj = purc_dvobj_ejson_new();
    ASSERT_EQ(purc_variant_is_object(dvobj), true);
    purc_variant_unref(dvobj);

    purc_cleanup();
}

static purc_variant_t get_dvobj_ejson(void* ctxt, const char* name)
{
    if (strcmp(name, "EJSON") == 0) {
        return (purc_variant_t)ctxt;
    }

    return PURC_VARIANT_INVALID;
}

typedef purc_variant_t (*fn_expected)(purc_variant_t dvobj, const char* name);
typedef bool (*fn_cmp)(purc_variant_t result, purc_variant_t expected);

struct ejson_result {
    const char             *name;
    const char             *ejson;

    fn_expected             expected;
    fn_cmp                  vrtcmp;
    int                     errcode;
};

purc_variant_t numberify(purc_variant_t dvobj, const char* name)
{
    double d = 0;

    (void)dvobj;

    if (strcmp(name, "zero") == 0) {
        d = 0;
    }
    else {
        d = strtod(name, NULL);
    }

    return purc_variant_make_number(d);
}

static bool numberify_vrtcmp(purc_variant_t result, purc_variant_t expected)
{
    double r1, r2;

    if (purc_variant_cast_to_number(result, &r1, false) &&
            purc_variant_cast_to_number(expected, &r2, false)) {
        return r1 == r2;
    }

    return false;
}

TEST(dvobjs, numberify)
{
    static const struct ejson_result test_cases[] = {
        { "zero",
            "$EJSON.numberify",
            numberify, numberify_vrtcmp, 0 },
        { "zero",
            "$EJSON.numberify(null)",
            numberify, numberify_vrtcmp, 0 },
        { "zero",
            "$EJSON.numberify(false)",
            numberify, numberify_vrtcmp, 0 },
        { "zero",
            "$EJSON.numberify([])",
            numberify, numberify_vrtcmp, 0 },
        { "zero",
            "$EJSON.numberify({})",
            numberify, numberify_vrtcmp, 0 },
        { "1.0",
            "$EJSON.numberify(true)",
            numberify, numberify_vrtcmp, 0 },
        { "1.0",
            "$EJSON.numberify(1.0)",
            numberify, numberify_vrtcmp, 0 },
        { "1.0",
            "$EJSON.numberify('1.0')",
            numberify, numberify_vrtcmp, 0 },
        { "2.0",
            "$EJSON.numberify([1.0, 1.0])",
            numberify, numberify_vrtcmp, 0 },
        { "2.0",
            "$EJSON.numberify({x:1.0, y:1.0})",
            numberify, numberify_vrtcmp, 0 },
        { "0",
            "$EJSON.numberify($EJSON)",
            numberify, numberify_vrtcmp, 0 },
        { "3.0",
            "$EJSON.numberify($EJSON.numberify(3.0))",
            numberify, numberify_vrtcmp, 0 },
    };

    int ret = purc_init_ex(PURC_MODULE_EJSON, "cn.fmsfot.hvml.test",
            "dvobjs", NULL);
    ASSERT_EQ (ret, PURC_ERROR_OK);

    purc_variant_t dvobj = purc_dvobj_ejson_new();
    ASSERT_NE(dvobj, nullptr);
    ASSERT_EQ(purc_variant_is_object(dvobj), true);

    for (size_t i = 0; i < PCA_TABLESIZE(test_cases); i++) {
        struct purc_ejson_parse_tree *ptree;
        purc_variant_t result, expected;

        purc_log_info("evalute: %s\n", test_cases[i].ejson);

        ptree = purc_variant_ejson_parse_string(test_cases[i].ejson,
                strlen(test_cases[i].ejson));
        result = purc_variant_ejson_parse_tree_evalute(ptree,
                get_dvobj_ejson, dvobj, true);
        purc_variant_ejson_parse_tree_destroy(ptree);

        /* FIXME: purc_variant_ejson_parse_tree_evalute should not return NULL
           when evaluating silently */
        ASSERT_NE(result, nullptr);

        if (test_cases[i].expected) {
            expected = test_cases[i].expected(dvobj, test_cases[i].name);

            if (purc_variant_get_type(result) != purc_variant_get_type(expected)) {
                purc_log_error("result type: %s, error message: %s\n",
                        purc_variant_typename(purc_variant_get_type(result)),
                        purc_get_error_message(purc_get_last_error()));
            }

            if (test_cases[i].vrtcmp) {
                ASSERT_EQ(test_cases[i].vrtcmp(result, expected), true);
            }
            else {
                ASSERT_EQ(purc_variant_is_equal_to(result, expected), true);
            }

            if (test_cases[i].errcode) {
                ASSERT_EQ(purc_get_last_error(), test_cases[i].errcode);
            }

            purc_variant_unref(expected);
        }
        else {
            ASSERT_EQ(purc_variant_get_type(result), PURC_VARIANT_TYPE_NULL);
        }

        purc_variant_unref(result);
    }

    purc_variant_unref(dvobj);
    purc_cleanup();
}

purc_variant_t booleanize(purc_variant_t dvobj, const char* name)
{
    bool b = false;

    (void)dvobj;

    if (strcmp(name, "true") == 0) {
        b = true;
    }

    return purc_variant_make_boolean(b);
}

static bool booleanize_vrtcmp(purc_variant_t result, purc_variant_t expected)
{
    if (purc_variant_is_true(result) && purc_variant_is_true(expected)) {
        return true;
    }
    else if (purc_variant_is_false(result) && purc_variant_is_false(expected)) {
        return true;
    }

    return false;
}

TEST(dvobjs, booleanize)
{
    static const struct ejson_result test_cases[] = {
        { "false",
            "$EJSON.booleanize",
            booleanize, booleanize_vrtcmp, 0 },
        { "false",
            "$EJSON.booleanize(null)",
            booleanize, booleanize_vrtcmp, 0 },
        { "false",
            "$EJSON.booleanize(false)",
            booleanize, booleanize_vrtcmp, 0 },
        { "true",
            "$EJSON.booleanize(true)",
            booleanize, booleanize_vrtcmp, 0 },
        { "false",
            "$EJSON.booleanize(0)",
            booleanize, booleanize_vrtcmp, 0 },
        { "false",
            "$EJSON.booleanize('')",
            booleanize, booleanize_vrtcmp, 0 },
        { "false",
            "$EJSON.booleanize({})",
            booleanize, booleanize_vrtcmp, 0 },
        { "false",
            "$EJSON.booleanize([])",
            booleanize, booleanize_vrtcmp, 0 },
        { "true",
            "$EJSON.booleanize(1.0)",
            booleanize, booleanize_vrtcmp, 0 },
        { "true",
            "$EJSON.booleanize('123')",
            booleanize, booleanize_vrtcmp, 0 },
        { "true",
            "$EJSON.booleanize('0')",
            booleanize, booleanize_vrtcmp, 0 },
        { "true",
            "$EJSON.booleanize($EJSON)",
            booleanize, booleanize_vrtcmp, 0 },
        { "false",
            "$EJSON.booleanize($EJSON.booleanize)",
            booleanize, booleanize_vrtcmp, 0 },
    };

    int ret = purc_init_ex(PURC_MODULE_EJSON, "cn.fmsfot.hvml.test",
            "dvobjs", NULL);
    ASSERT_EQ (ret, PURC_ERROR_OK);

    purc_variant_t dvobj = purc_dvobj_ejson_new();
    ASSERT_NE(dvobj, nullptr);
    ASSERT_EQ(purc_variant_is_object(dvobj), true);

    for (size_t i = 0; i < PCA_TABLESIZE(test_cases); i++) {
        struct purc_ejson_parse_tree *ptree;
        purc_variant_t result, expected;

        purc_log_info("evalute: %s\n", test_cases[i].ejson);

        ptree = purc_variant_ejson_parse_string(test_cases[i].ejson,
                strlen(test_cases[i].ejson));
        result = purc_variant_ejson_parse_tree_evalute(ptree,
                get_dvobj_ejson, dvobj, true);
        purc_variant_ejson_parse_tree_destroy(ptree);

        /* FIXME: purc_variant_ejson_parse_tree_evalute should not return NULL
           when evaluating silently */
        ASSERT_NE(result, nullptr);

        if (test_cases[i].expected) {
            expected = test_cases[i].expected(dvobj, test_cases[i].name);

            if (purc_variant_get_type(result) != purc_variant_get_type(expected)) {
                purc_log_error("result type: %s, error message: %s\n",
                        purc_variant_typename(purc_variant_get_type(result)),
                        purc_get_error_message(purc_get_last_error()));
            }

            if (test_cases[i].vrtcmp) {
                ASSERT_EQ(test_cases[i].vrtcmp(result, expected), true);
            }
            else {
                ASSERT_EQ(purc_variant_is_equal_to(result, expected), true);
            }

            if (test_cases[i].errcode) {
                ASSERT_EQ(purc_get_last_error(), test_cases[i].errcode);
            }

            purc_variant_unref(expected);
        }
        else {
            ASSERT_EQ(purc_variant_get_type(result), PURC_VARIANT_TYPE_NULL);
        }

        purc_variant_unref(result);
    }

    purc_variant_unref(dvobj);
    purc_cleanup();
}

