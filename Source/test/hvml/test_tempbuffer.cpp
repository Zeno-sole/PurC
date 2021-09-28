#include "purc.h"

#include "hvml-buffer.h"

#include <stdio.h>
#include <errno.h>
#include <gtest/gtest.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

TEST(temp_buffer, new_destory)
{
    struct pchvml_buffer* buffer = pchvml_buffer_new ();
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(0, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(0, pchvml_buffer_get_size_in_chars(buffer));

    pchvml_buffer_destroy(buffer);
}

TEST(temp_buffer, append)
{
    struct pchvml_buffer* buffer = pchvml_buffer_new ();
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(0, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(0, pchvml_buffer_get_size_in_chars(buffer));

    char c[8] = {0};
    uint32_t wc = 0;
    pchvml_buffer_append_bytes(buffer, c, 1);
    ASSERT_EQ(1, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(1, pchvml_buffer_get_size_in_chars(buffer));
    ASSERT_EQ(wc, pchvml_buffer_get_last_char(buffer));

    c[0] = 1;
    wc = 1;
    pchvml_buffer_append_bytes(buffer, c, 1);
    ASSERT_EQ(2, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(2, pchvml_buffer_get_size_in_chars(buffer));
    ASSERT_EQ(wc, pchvml_buffer_get_last_char(buffer));
    ASSERT_EQ(1, pchvml_buffer_end_with(buffer, c, 1));

    c[0] = 'a';
    wc = 'a';
    char cmp[8] = {1, 'a', 0};
    pchvml_buffer_append_bytes(buffer, c, 1);
    ASSERT_EQ(3, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(3, pchvml_buffer_get_size_in_chars(buffer));
    ASSERT_EQ(wc, pchvml_buffer_get_last_char(buffer));
    ASSERT_EQ(1, pchvml_buffer_end_with(buffer, cmp, 2));

    char b[] = "你";
    wc = 0x4F60;
    pchvml_buffer_append_bytes(buffer, b, strlen(b));
    ASSERT_EQ(6, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(4, pchvml_buffer_get_size_in_chars(buffer));
    ASSERT_EQ(wc, pchvml_buffer_get_last_char(buffer));

    pchvml_buffer_destroy(buffer);
}

TEST(temp_buffer, end_with_and_is_equal)
{
    struct pchvml_buffer* buffer = pchvml_buffer_new ();
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(0, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(0, pchvml_buffer_get_size_in_chars(buffer));

    char c[8] = {0};
    uint32_t wc = 0;
    pchvml_buffer_append_bytes(buffer, c, 1);
    ASSERT_EQ(1, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(1, pchvml_buffer_get_size_in_chars(buffer));
    ASSERT_EQ(wc, pchvml_buffer_get_last_char(buffer));

    c[0] = 1;
    wc = 1;
    pchvml_buffer_append_bytes(buffer, c, 1);
    ASSERT_EQ(2, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(2, pchvml_buffer_get_size_in_chars(buffer));
    ASSERT_EQ(wc, pchvml_buffer_get_last_char(buffer));
    ASSERT_EQ(1, pchvml_buffer_end_with(buffer, c, 1));

    c[0] = 'a';
    wc = 'a';
    char cmp[8] = {1, 'a', 0};
    pchvml_buffer_append_bytes(buffer, c, 1);
    ASSERT_EQ(3, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(3, pchvml_buffer_get_size_in_chars(buffer));
    ASSERT_EQ(wc, pchvml_buffer_get_last_char(buffer));
    ASSERT_EQ(1, pchvml_buffer_end_with(buffer, cmp, 2));

    c[0] = 'b';
    wc = 'b';
    char p[8] = {0, 1, 'a', 'b', 0};
    pchvml_buffer_append_bytes(buffer, c, 1);
    ASSERT_EQ(4, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(4, pchvml_buffer_get_size_in_chars(buffer));
    ASSERT_EQ(wc, pchvml_buffer_get_last_char(buffer));
    ASSERT_EQ(1, pchvml_buffer_end_with(buffer, p, 4));
    ASSERT_EQ(1, pchvml_buffer_equal_to(buffer, p, 4));

    pchvml_buffer_reset(buffer);
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(0, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(0, pchvml_buffer_get_size_in_chars(buffer));
    ASSERT_EQ(0, pchvml_buffer_get_last_char(buffer));

    pchvml_buffer_destroy(buffer);
}

TEST(temp_buffer, append_temp_buffer)
{
    struct pchvml_buffer* buffer = pchvml_buffer_new ();
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(0, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(0, pchvml_buffer_get_size_in_chars(buffer));

    pchvml_buffer_append_bytes(buffer, "a", 1);
    pchvml_buffer_append_bytes(buffer, "b", 1);
    pchvml_buffer_append_bytes(buffer, "c", 1);
    pchvml_buffer_append_bytes(buffer, "d", 1);
    pchvml_buffer_append_bytes(buffer, "e", 1);
    ASSERT_STREQ("abcde", pchvml_buffer_get_buffer(buffer));


    struct pchvml_buffer* buf2 = pchvml_buffer_new ();
    ASSERT_NE(buf2, nullptr);
    pchvml_buffer_append_bytes(buf2, "1", 1);
    pchvml_buffer_append_bytes(buf2, "2", 1);
    pchvml_buffer_append_bytes(buf2, "3", 1);
    pchvml_buffer_append_bytes(buf2, "4", 1);
    pchvml_buffer_append_bytes(buf2, "5", 1);
    ASSERT_STREQ("12345", pchvml_buffer_get_buffer(buf2));

    pchvml_buffer_append_temp_buffer(buf2, buffer);
    ASSERT_STREQ("12345abcde", pchvml_buffer_get_buffer(buf2));

    pchvml_buffer_destroy(buffer);
}

TEST(temp_buffer, append_chars)
{
    struct pchvml_buffer* buffer = pchvml_buffer_new ();
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(0, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(0, pchvml_buffer_get_size_in_chars(buffer));

    pchvml_buffer_append_bytes(buffer, "a", 1);
    pchvml_buffer_append_bytes(buffer, "b", 1);
    pchvml_buffer_append_bytes(buffer, "c", 1);
    pchvml_buffer_append_bytes(buffer, "d", 1);
    pchvml_buffer_append_bytes(buffer, "e", 1);
    ASSERT_STREQ("abcde", pchvml_buffer_get_buffer(buffer));


    uint32_t wc[] = {0x4F60, 0x597D};
    pchvml_buffer_append_chars(buffer, wc, 2);
    ASSERT_EQ(11, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(7, pchvml_buffer_get_size_in_chars(buffer));
    ASSERT_EQ(wc[1], pchvml_buffer_get_last_char(buffer));
    ASSERT_STREQ("abcde你好", pchvml_buffer_get_buffer(buffer));

    pchvml_buffer_destroy(buffer);
}

TEST(temp_buffer, delete_head)
{
    struct pchvml_buffer* buffer = pchvml_buffer_new ();
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(0, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(0, pchvml_buffer_get_size_in_chars(buffer));

    uint32_t wc[] = {0x4F60, 0x597D};
    pchvml_buffer_append_chars(buffer, wc, 2);
    ASSERT_EQ(6, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(2, pchvml_buffer_get_size_in_chars(buffer));
    ASSERT_EQ(wc[1], pchvml_buffer_get_last_char(buffer));
    ASSERT_STREQ("你好", pchvml_buffer_get_buffer(buffer));

    pchvml_buffer_append_bytes(buffer, "a", 1);
    pchvml_buffer_append_bytes(buffer, "b", 1);
    pchvml_buffer_append_bytes(buffer, "c", 1);
    pchvml_buffer_append_bytes(buffer, "d", 1);
    pchvml_buffer_append_bytes(buffer, "e", 1);
    ASSERT_STREQ("你好abcde", pchvml_buffer_get_buffer(buffer));

    pchvml_buffer_delete_head_chars(buffer, 1);
    ASSERT_STREQ("好abcde", pchvml_buffer_get_buffer(buffer));

    pchvml_buffer_delete_head_chars(buffer, 3);
    ASSERT_STREQ("cde", pchvml_buffer_get_buffer(buffer));

    pchvml_buffer_destroy(buffer);
}

TEST(temp_buffer, delete_tail)
{
    struct pchvml_buffer* buffer = pchvml_buffer_new ();
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(0, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(0, pchvml_buffer_get_size_in_chars(buffer));

    pchvml_buffer_append_bytes(buffer, "a", 1);
    pchvml_buffer_append_bytes(buffer, "b", 1);
    pchvml_buffer_append_bytes(buffer, "c", 1);
    pchvml_buffer_append_bytes(buffer, "d", 1);
    pchvml_buffer_append_bytes(buffer, "e", 1);
    ASSERT_STREQ("abcde", pchvml_buffer_get_buffer(buffer));


    uint32_t wc[] = {0x4F60, 0x597D};
    pchvml_buffer_append_chars(buffer, wc, 2);
    ASSERT_EQ(11, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(7, pchvml_buffer_get_size_in_chars(buffer));
    ASSERT_EQ(wc[1], pchvml_buffer_get_last_char(buffer));
    ASSERT_STREQ("abcde你好", pchvml_buffer_get_buffer(buffer));


    pchvml_buffer_delete_tail_chars(buffer, 1);
    ASSERT_STREQ("abcde你", pchvml_buffer_get_buffer(buffer));

    pchvml_buffer_delete_tail_chars(buffer, 3);
    ASSERT_STREQ("abc", pchvml_buffer_get_buffer(buffer));

    pchvml_buffer_destroy(buffer);
}

TEST(temp_buffer, is_int)
{
    struct pchvml_buffer* buffer = pchvml_buffer_new ();
    ASSERT_NE(buffer, nullptr);
    ASSERT_EQ(0, pchvml_buffer_get_size_in_bytes(buffer));
    ASSERT_EQ(0, pchvml_buffer_get_size_in_chars(buffer));

    pchvml_buffer_append_bytes(buffer, "1", 1);
    pchvml_buffer_append_bytes(buffer, "2", 1);
    pchvml_buffer_append_bytes(buffer, "3", 1);
    ASSERT_STREQ("123", pchvml_buffer_get_buffer(buffer));

    bool is_int = pchvml_buffer_is_int(buffer);
    ASSERT_EQ(is_int, true);

    pchvml_buffer_append_bytes(buffer, " ", 1);
    is_int = pchvml_buffer_is_int(buffer);
    ASSERT_EQ(is_int, false);

    pchvml_buffer_destroy(buffer);
}