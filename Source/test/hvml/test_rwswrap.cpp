#include "purc.h"

#include "hvml-rwswrap.h"

#include <stdio.h>
#include <errno.h>
#include <gtest/gtest.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

void create_temp_file(const char* file, const char* buf, size_t buf_len)
{
    FILE* fp = fopen(file, "wb");
    fwrite(buf, buf_len, 1, fp);
    fflush(fp);
    fclose(fp);
}

void remove_temp_file(const char* file)
{
    remove(file);
}

TEST(rwswrap, new_destory)
{
    struct pchvml_rwswrap* wrap = pchvml_rwswrap_new ();
    ASSERT_NE(wrap, nullptr);
    pchvml_rwswrap_destroy(wrap);
}

TEST(rwswrap, next_char)
{
    char buf[] = "This测试";
    struct pchvml_rwswrap* wrap = pchvml_rwswrap_new ();
    ASSERT_NE(wrap, nullptr);

    purc_rwstream_t rws = purc_rwstream_new_from_mem (buf, strlen(buf));

    pchvml_rwswrap_set_rwstream (wrap, rws);

    uint32_t uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'T');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'h');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'i');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 's');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 0x6D4B);
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 0x8BD5);
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 0);

    purc_rwstream_destroy (rws);
    pchvml_rwswrap_destroy(wrap);
}

TEST(rwswrap, buffer_char)
{
    char buf[] = "This测试";
    struct pchvml_rwswrap* wrap = pchvml_rwswrap_new ();
    ASSERT_NE(wrap, nullptr);

    purc_rwstream_t rws = purc_rwstream_new_from_mem (buf, strlen(buf));

    pchvml_rwswrap_set_rwstream (wrap, rws);

    uint32_t uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'T');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'h');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'i');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 's');

    uint32_t buff[] = {'T', 'h', 'i', 's'};
    pchvml_rwswrap_buffer_chars (wrap, buff, 4);
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'T');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'h');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'i');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 's');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 0x6D4B);

    purc_rwstream_destroy (rws);
    pchvml_rwswrap_destroy(wrap);
}

TEST(rwswrap, buffer_arrlist)
{
    char buf[] = "This测试";
    struct pchvml_rwswrap* wrap = pchvml_rwswrap_new ();
    ASSERT_NE(wrap, nullptr);

    purc_rwstream_t rws = purc_rwstream_new_from_mem (buf, strlen(buf));

    pchvml_rwswrap_set_rwstream (wrap, rws);

    uint32_t uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'T');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'h');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'i');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 's');

    pcutils_arrlist* ucs = pcutils_arrlist_new(NULL);
    uintptr_t v = 'T';
    pcutils_arrlist_add(ucs, (void*)v);
    v = 'h';
    pcutils_arrlist_add(ucs, (void*)v);
    v = 'i';
    pcutils_arrlist_add(ucs, (void*)v);
    v = 's';
    pcutils_arrlist_add(ucs, (void*)v);

    pchvml_rwswrap_buffer_arrlist (wrap, ucs);

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'T');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'h');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'i');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 's');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 0x6D4B);

    pcutils_arrlist_free(ucs);
    purc_rwstream_destroy (rws);
    pchvml_rwswrap_destroy(wrap);
}

TEST(rwswrap, read_eof)
{
    char tmp_file[] = "/tmp/rwswrap.txt";
    char buf[] = "This";
    size_t buf_len = strlen(buf);
    create_temp_file(tmp_file, buf, buf_len);

    struct pchvml_rwswrap* wrap = pchvml_rwswrap_new ();
    ASSERT_NE(wrap, nullptr);

    purc_rwstream_t rws = purc_rwstream_new_from_file(tmp_file, "r");
    ASSERT_NE(rws, nullptr);

    pchvml_rwswrap_set_rwstream (wrap, rws);

    uint32_t uc = 0;

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'T');

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'h');

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 'i');

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 's');

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 0);

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 0);

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc, 0);

    int ret = purc_rwstream_destroy (rws);
    ASSERT_EQ(ret, 0);
    pchvml_rwswrap_destroy(wrap);

    remove_temp_file(tmp_file);
}