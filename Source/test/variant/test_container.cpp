#include "purc.h"

#include "private/hvml.h"
#include "private/utils.h"
#include "purc-rwstream.h"
#include "hvml/hvml-token.h"

#include "../helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <libgen.h>
#include <gtest/gtest.h>

#include <dirent.h>
#include <glob.h>

using namespace std;

#define PRINTF(...)                                                       \
    do {                                                                  \
        fprintf(stderr, "\e[0;32m[          ] \e[0m");                    \
        fprintf(stderr, __VA_ARGS__);                                     \
    } while(false)

#if OS(LINUX) || OS(UNIX)
// get path from env or __FILE__/../<rel> otherwise
#define getpath_from_env_or_rel(_path, _len, _env, _rel) do {  \
    const char *p = getenv(_env);                                      \
    if (p) {                                                           \
        snprintf(_path, _len, "%s", p);                                \
    } else {                                                           \
        char tmp[PATH_MAX+1];                                          \
        snprintf(tmp, sizeof(tmp), __FILE__);                          \
        const char *folder = dirname(tmp);                             \
        snprintf(_path, _len, "%s/%s", folder, _rel);                  \
    }                                                                  \
} while (0)

#endif // OS(LINUX) || OS(UNIX)

#define MIN_BUFFER     512
#define MAX_BUFFER     1024 * 1024 * 1024

char* variant_to_string(purc_variant_t v)
{
    purc_rwstream_t my_rws = purc_rwstream_new_buffer(MIN_BUFFER, MAX_BUFFER);
    size_t len_expected = 0;
    purc_variant_serialize(v, my_rws,
            0, PCVARIANT_SERIALIZE_OPT_PLAIN, &len_expected);
    char* buf = (char*)purc_rwstream_get_mem_buffer_ex(my_rws, NULL, NULL, true);
    purc_rwstream_destroy(my_rws);
    return buf;
}


enum container_ops_type {
    CONTAINER_OPS_TYPE_DISPLACE,
    CONTAINER_OPS_TYPE_APPEND,
    CONTAINER_OPS_TYPE_PREPEND,
    CONTAINER_OPS_TYPE_MERGE,
    CONTAINER_OPS_TYPE_REMOVE,
    CONTAINER_OPS_TYPE_INSERT_BEFORE,
    CONTAINER_OPS_TYPE_INSERT_AFTER,
    CONTAINER_OPS_TYPE_UNITE,
    CONTAINER_OPS_TYPE_INTERSECT,
    CONTAINER_OPS_TYPE_SUBTRACT,
    CONTAINER_OPS_TYPE_XOR,
    CONTAINER_OPS_TYPE_OVERWRITE
};

struct test_case {
    char* filename;
    char* data;
};

#define TO_TYPE(type_name, type_enum)                       \
    if (strcmp (type, type_name) == 0) {                    \
        return type_enum;                                   \
    }

enum container_ops_type to_ops_type(const char* type)
{
    TO_TYPE("displace", CONTAINER_OPS_TYPE_DISPLACE);
    TO_TYPE("append", CONTAINER_OPS_TYPE_APPEND);
    TO_TYPE("prepend", CONTAINER_OPS_TYPE_PREPEND);
    TO_TYPE("merge", CONTAINER_OPS_TYPE_MERGE);
    TO_TYPE("remove", CONTAINER_OPS_TYPE_REMOVE);
    TO_TYPE("insertBefore", CONTAINER_OPS_TYPE_INSERT_BEFORE);
    TO_TYPE("insertAfter", CONTAINER_OPS_TYPE_INSERT_AFTER);
    TO_TYPE("unite", CONTAINER_OPS_TYPE_UNITE);
    TO_TYPE("intersect", CONTAINER_OPS_TYPE_INTERSECT);
    TO_TYPE("subtract", CONTAINER_OPS_TYPE_SUBTRACT);
    TO_TYPE("xor", CONTAINER_OPS_TYPE_XOR);
    TO_TYPE("overwrite", CONTAINER_OPS_TYPE_OVERWRITE);

    return CONTAINER_OPS_TYPE_DISPLACE;
}

purc_variant_type to_variant_type(const char* type)
{
    TO_TYPE("object", PURC_VARIANT_TYPE_OBJECT);
    TO_TYPE("array", PURC_VARIANT_TYPE_ARRAY);
    TO_TYPE("set", PURC_VARIANT_TYPE_SET);
    return PURC_VARIANT_TYPE_OBJECT;
}

static inline void
add_test_case(std::vector<test_case> &test_cases,
        const char* filename, const char* data)
{
    struct test_case test;
    test.filename = MemCollector::strdup(filename);
    test.data = MemCollector::strdup(data);
    test_cases.push_back(test);
}

char* trim(char *str)
{
    if (!str)
    {
        return NULL;
    }
    char *end;

    while (isspace((unsigned char)*str)) {
        str++;
    }

    if(*str == 0) {
        return str;
    }

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }

    end[1] = '\0';
    return str;
}

class TestCaseData : public testing::TestWithParam<test_case>
{
protected:
    void SetUp() {
        purc_init ("cn.fmsoft.hybridos.test", "purc_variant", NULL);
    }
    void TearDown() {
        purc_cleanup ();
    }
    const test_case get_data() {
        return GetParam();
    }
};

purc_variant_t to_variant_set(const char* unique_key, purc_variant_t var)
{
    purc_variant_t set = PURC_VARIANT_INVALID;

    if (unique_key && strlen(unique_key)) {
        set = purc_variant_make_set_by_ckey(0, unique_key,
                PURC_VARIANT_INVALID);
    }
    else {
        set = purc_variant_make_set(0, PURC_VARIANT_INVALID,
                PURC_VARIANT_INVALID);
    }
    if (var == PURC_VARIANT_INVALID) {
        goto end;
    }

    if (purc_variant_is_object(var)) {
        purc_variant_set_add(set, var, false);
    }
    else if (purc_variant_is_array(var)) {
        size_t sz = purc_variant_array_get_size(var);
        for (size_t i = 0; i < sz; i++) {
            purc_variant_t v = purc_variant_array_get(var, i);
            purc_variant_set_add(set, v, false);
        }
    }

end:
    return set;
}

purc_variant_t build_test_dst(purc_variant_t test_case_variant)
{
    const char* dst_unique_key = NULL;
    purc_variant_t dst_unique_key_var = purc_variant_object_get_by_ckey(
            test_case_variant, "dst_unique_key", false);
    if (dst_unique_key_var != PURC_VARIANT_INVALID) {
        dst_unique_key = purc_variant_get_string_const(dst_unique_key_var);
    }

    const char* dst_type = NULL;
    purc_variant_t dst_type_var = purc_variant_object_get_by_ckey(
            test_case_variant, "dst_type", false);
    if (dst_type_var != PURC_VARIANT_INVALID) {
        dst_type = purc_variant_get_string_const(dst_type_var);
    }

    purc_variant_t dst = purc_variant_object_get_by_ckey(test_case_variant,
                "dst", false);
    if (dst == PURC_VARIANT_INVALID) {
        return PURC_VARIANT_INVALID;
    }

    enum purc_variant_type type = to_variant_type(dst_type);
    if (type == PURC_VARIANT_TYPE_SET) {
        return to_variant_set(dst_unique_key, dst);
    }
    purc_variant_ref(dst);
    return dst;
}

purc_variant_t build_test_src(purc_variant_t test_case_variant)
{
    const char* src_unique_key = NULL;
    purc_variant_t src_unique_key_var = purc_variant_object_get_by_ckey(
            test_case_variant, "src_unique_key", false);
    if (src_unique_key_var != PURC_VARIANT_INVALID) {
        src_unique_key = purc_variant_get_string_const(src_unique_key_var);
    }

    const char* src_type = NULL;
    purc_variant_t src_type_var = purc_variant_object_get_by_ckey(
            test_case_variant, "src_type", false);
    if (src_type_var != PURC_VARIANT_INVALID) {
        src_type = purc_variant_get_string_const(src_type_var);
    }

    purc_variant_t src = purc_variant_object_get_by_ckey(test_case_variant,
                "src", false);
    if (src == PURC_VARIANT_INVALID) {
        return PURC_VARIANT_INVALID;
    }

    enum purc_variant_type type = to_variant_type(src_type);
    if (type == PURC_VARIANT_TYPE_SET) {
        return to_variant_set(src_unique_key, src);
    }
    purc_variant_ref(src);
    return src;
}

TEST_P(TestCaseData, container_ops)
{
    const struct test_case data = get_data();
    PRINTF("filename=%s\n", data.filename);

    purc_variant_t test_case_variant = purc_variant_make_from_json_string(
            data.data, strlen(data.data));
    ASSERT_NE(test_case_variant, PURC_VARIANT_INVALID);

    purc_variant_t ignore_var = purc_variant_object_get_by_ckey(test_case_variant,
                "ignore", true);
    if (ignore_var != PURC_VARIANT_INVALID
            && purc_variant_booleanize(ignore_var)) {
        return;
    }

    purc_variant_t dst = build_test_dst(test_case_variant);
    ASSERT_NE(dst, PURC_VARIANT_INVALID);

    purc_variant_t src = build_test_src(test_case_variant);
    ASSERT_NE(src, PURC_VARIANT_INVALID);

    purc_variant_t cmp = purc_variant_object_get_by_ckey(test_case_variant,
                "cmp", false);
    ASSERT_NE(cmp, PURC_VARIANT_INVALID);

    //  do container ops
    purc_variant_t ops_type_var = purc_variant_object_get_by_ckey(test_case_variant,
                "ops", false);
    ASSERT_NE(ops_type_var, PURC_VARIANT_INVALID);

    const char* ops_type_str = purc_variant_get_string_const(ops_type_var);
    enum container_ops_type ops_type = to_ops_type(ops_type_str);

    bool result = false;
    switch (ops_type) {
        case CONTAINER_OPS_TYPE_DISPLACE:
            result = purc_variant_container_displace(dst, src, true);
            break;

        case CONTAINER_OPS_TYPE_APPEND:
            result = purc_variant_array_append_another(dst, src, true);
            break;

        case CONTAINER_OPS_TYPE_PREPEND:
            result = purc_variant_array_prepend_another(dst, src, true);
            break;

        case CONTAINER_OPS_TYPE_MERGE:
            result = purc_variant_object_merge_another(dst, src, true);
            break;

        case CONTAINER_OPS_TYPE_REMOVE:
            result = purc_variant_container_remove(dst, src, true);
            break;

        case CONTAINER_OPS_TYPE_INSERT_BEFORE:
            {
                purc_variant_t idx_var = purc_variant_object_get_by_ckey(
                        test_case_variant, "idx", false);
                ASSERT_NE(idx_var, PURC_VARIANT_INVALID);
                int64_t idx = 0;
                purc_variant_cast_to_longint(idx_var, &idx, false);
                result = purc_variant_array_insert_another_before(
                        dst, idx, src, true);
            }
            break;

        case CONTAINER_OPS_TYPE_INSERT_AFTER:
            {
                purc_variant_t idx_var = purc_variant_object_get_by_ckey(
                        test_case_variant, "idx", false);
                ASSERT_NE(idx_var, PURC_VARIANT_INVALID);
                int64_t idx = 0;
                purc_variant_cast_to_longint(idx_var, &idx, false);
                result = purc_variant_array_insert_another_after(
                        dst, idx, src, true);
            }
            break;

        case CONTAINER_OPS_TYPE_UNITE:
            result = purc_variant_set_unite(dst, src, true);
            break;

        case CONTAINER_OPS_TYPE_INTERSECT:
            result = purc_variant_set_intersect(dst, src, true);
            break;

        case CONTAINER_OPS_TYPE_SUBTRACT:
            result = purc_variant_set_subtract(dst, src, true);
            break;

        case CONTAINER_OPS_TYPE_XOR:
            result = purc_variant_set_xor(dst, src, true);
            break;

        case CONTAINER_OPS_TYPE_OVERWRITE:
            result = purc_variant_set_overwrite(dst, src, true);
            break;
    }
    ASSERT_EQ(result, true);

    char* dst_result = variant_to_string(dst);
    char* cmp_result = variant_to_string(cmp);
    PRINTF("dst=%s\n", dst_result);
    PRINTF("cmp=%s\n", cmp_result);
    ASSERT_STREQ(dst_result, cmp_result);

    // clear
    free(dst_result);
    free(cmp_result);

    purc_variant_unref(src);
    purc_variant_unref(dst);
    purc_variant_unref(test_case_variant);
}

char* read_file (const char* file)
{
    FILE* fp = fopen (file, "r");
    if (fp == NULL) {
        return NULL;
    }
    fseek (fp, 0, SEEK_END);
    size_t sz = ftell (fp);
    char* buf = (char*) malloc(sz + 1);
    fseek (fp, 0, SEEK_SET);
    sz = fread (buf, 1, sz, fp);
    fclose (fp);
    buf[sz] = 0;
    return buf;
}

char inner_test_data[] = "" \
    "{" \
    "    \"ignore\": false," \
    "    \"error\": 0," \
    "    \"ops\": \"displace\"," \
    "    \"idx\": 0," \
    "    \"src_type\": \"object\"," \
    "    \"src_unique_key\": null," \
    "    \"src\": {" \
    "        \"id\": 2," \
    "        \"name\": \"name src\"," \
    "        \"title\": \"title src\"" \
    "    }," \
    "    \"dst_type\": \"object\"," \
    "    \"dst_unique_key\": null," \
    "    \"dst\": {" \
    "        \"id\": 1," \
    "        \"name\": \"name dst\"" \
    "    }," \
    "    \"cmp\": {" \
    "        \"id\": 2," \
    "        \"name\": \"name src\"," \
    "        \"title\": \"title src\"" \
    "    }" \
    "}";

std::vector<test_case> load_test_case()
{
    int r = 0;
    std::vector<test_case> test_cases;
    glob_t globbuf;
    memset(&globbuf, 0, sizeof(globbuf));


    char path[PATH_MAX+1];
    const char* env = "VARIANT_TEST_CONTAINER_OPS_PATH";
    getpath_from_env_or_rel(path, sizeof(path), env, "/data/*.json");

    if (!path[0])
        goto end;

    globbuf.gl_offs = 0;
    r = glob(path, GLOB_DOOFFS | GLOB_APPEND, NULL, &globbuf);

    if (r == 0) {
        for (size_t i = 0; i < globbuf.gl_pathc; ++i) {
            char *buf  = read_file(globbuf.gl_pathv[i]);
            add_test_case(test_cases,
                    basename((char *)globbuf.gl_pathv[i]), buf);
            free(buf);
        }
    }
    globfree(&globbuf);

    if (test_cases.empty()) {
        add_test_case(test_cases, "inner_test", inner_test_data);
    }

end:
    return test_cases;
}

INSTANTIATE_TEST_SUITE_P(purc_variant, TestCaseData,
        testing::ValuesIn(load_test_case()));