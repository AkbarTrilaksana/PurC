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

    struct pchvml_uc* uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 'T');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 'h');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 'i');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 's');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 0x6D4B);
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 0x8BD5);
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 0);

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

    struct pchvml_uc* uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 'T');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 'h');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 'i');
    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 's');

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

    struct pchvml_uc* uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 'T');

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 'h');

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 'i');

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 's');

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 0);

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 0);

    uc = pchvml_rwswrap_next_char (wrap);
    ASSERT_EQ(uc->character, 0);

    int ret = purc_rwstream_destroy (rws);
    ASSERT_EQ(ret, 0);
    pchvml_rwswrap_destroy(wrap);

    remove_temp_file(tmp_file);
}
