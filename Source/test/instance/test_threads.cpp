#include "purc.h"

#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>           /* For O_* constants */
#include <gtest/gtest.h>
#include <wtf/Compiler.h>

#define NR_THREADS          10

static volatile purc_atom_t other_inst[NR_THREADS];
static volatile purc_atom_t main_inst;

struct thread_arg {
    sem_t  *wait;
    int     nr;
};

static void* general_thread_entry(void* arg)
{
    struct thread_arg *my_arg = (struct thread_arg *)arg;
    char runner_name[32];

    sprintf(runner_name, "thread%d", my_arg->nr);

    // initial purc instance
    int ret = purc_init_ex(PURC_MODULE_VARIANT, "cn.fmsoft.purc.test",
            runner_name, NULL);

    if (ret == PURC_ERROR_OK) {
        purc_enable_log(true, false);
        other_inst[my_arg->nr] =
            purc_inst_create_move_buffer(PCINST_MOVE_BUFFER_BROADCAST, 16);
        purc_log_info("purc_inst_create_move_buffer returns: %x\n",
                other_inst[my_arg->nr]);
    }
    sem_post(my_arg->wait);

    size_t n;
    do {
        ret = purc_inst_holding_messages_count(&n);

        if (ret) {
            purc_log_error("purc_inst_holding_messages_count failed: %d\n", ret);
        }
        else if (n > 0) {
            purc_log_info("purc_inst_holding_messages_count returns: %d\n", (int)n);

            pcrdr_msg *msg = purc_inst_take_away_message(0);
            purc_log_info("purc_inst_take_away_message returns a message:\n");
            purc_log_info("    type:        %d\n", msg->type);
            purc_log_info("    target:      %d\n", msg->target);
            purc_log_info("    targetValue: %d\n", (int)msg->targetValue);
            purc_log_info("    event:       %s\n",
                    purc_variant_get_string_const(msg->event));

            purc_inst_move_message(main_inst, msg);
            pcrdr_release_message(msg);
            break;
        }
        else {
            usleep(10000);  // 10m
        }

    } while (true);

    n = purc_inst_destroy_move_buffer();
    purc_log_info("move buffer destroyed, %d messages discarded\n", (int)n);

    purc_cleanup();
    return NULL;
}

static int create_thread(int nr)
{
    int ret;
    struct thread_arg arg;
    pthread_t th;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    arg.nr = nr;
ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    sem_unlink("sync");
    arg.wait = sem_open("sync", O_CREAT | O_EXCL, 0644, 0);
    if (arg.wait == SEM_FAILED) {
        purc_log_error("failed to create semaphore: %s\n", strerror(errno));
        return -1;
    }
    ret = pthread_create(&th, &attr, general_thread_entry, &arg);
    if (ret) {
        sem_close(arg.wait);
        purc_log_error("failed to create thread: %d\n", nr);
        return -1;
    }
    pthread_attr_destroy(&attr);

    sem_wait(arg.wait);
    sem_close(arg.wait);
ALLOW_DEPRECATED_DECLARATIONS_END

    return ret;
}

TEST(instance, thread)
{
    int ret;

    // initial purc
    ret = purc_init_ex(PURC_MODULE_VARIANT, "cn.fmsoft.purc.test", "threads",
            NULL);
    ASSERT_EQ(ret, PURC_ERROR_OK);

    purc_enable_log(true, false);

    main_inst =
            purc_inst_create_move_buffer(PCINST_MOVE_BUFFER_BROADCAST, 16);
    ASSERT_NE(main_inst, 0);

    create_thread(0);
    ASSERT_NE(other_inst[0], 0);

    pcrdr_msg *event;
    event = pcrdr_make_event_message(
            PCRDR_MSG_TARGET_THREAD,
            1,
            "test",
            PCRDR_MSG_ELEMENT_TYPE_VOID, NULL, NULL,
            PCRDR_MSG_DATA_TYPE_VOID, NULL, 0);

    if (purc_inst_move_message(other_inst[0], event) == 0) {
        purc_log_error("purc_inst_move_message: no recipient\n");
    }
    pcrdr_release_message(event);

    size_t n;
    do {

        ret = purc_inst_holding_messages_count(&n);
        if (ret) {
            purc_log_error("purc_inst_holding_messages_count failed\n");
            break;
        }
        else if (n > 0) {
            pcrdr_msg *msg = purc_inst_take_away_message(0);

            ASSERT_EQ(msg->target, PCRDR_MSG_TARGET_THREAD);
            ASSERT_EQ(msg->targetValue, 1);
            ASSERT_STREQ(purc_variant_get_string_const(msg->event), "test");

            pcrdr_release_message(msg);
            break;
        }
        else {
            usleep(10000);  // 10m
        }

    } while (true);

    n = purc_inst_destroy_move_buffer();
    purc_log_info("move buffer destroyed, %d messages discarded\n", (int)n);

    purc_cleanup();
}

TEST(instance, threads)
{
    int ret;

    // initial purc
    ret = purc_init_ex(PURC_MODULE_VARIANT, "cn.fmsoft.purc.test", "threads",
            NULL);
    ASSERT_EQ(ret, PURC_ERROR_OK);

    purc_enable_log(true, false);

    main_inst =
            purc_inst_create_move_buffer(PCINST_MOVE_BUFFER_BROADCAST, 16);
    ASSERT_NE(main_inst, 0);

    for (int i = 1; i < NR_THREADS; i++) {
        create_thread(i);
        ASSERT_NE(other_inst[i], 0);
    }

    pcrdr_msg *event;
    event = pcrdr_make_event_message(
            PCRDR_MSG_TARGET_THREAD,
            1,
            "test",
            PCRDR_MSG_ELEMENT_TYPE_VOID, NULL, NULL,
            PCRDR_MSG_DATA_TYPE_VOID, NULL, 0);

    // broadcast the event
    if (purc_inst_move_message(0, event) == 0) {
        purc_log_error("purc_inst_move_message: no recipient\n");
    }
    pcrdr_release_message(event);

    size_t n;
    int nr_got = 0;
    do {
        ret = purc_inst_holding_messages_count(&n);
        if (ret) {
            purc_log_error("purc_inst_holding_messages_count failed\n");
            break;
        }
        else if (n > 0) {
            pcrdr_msg *msg = purc_inst_take_away_message(0);

            ASSERT_EQ(msg->target, PCRDR_MSG_TARGET_THREAD);
            ASSERT_EQ(msg->targetValue, 1);
            ASSERT_STREQ(purc_variant_get_string_const(msg->event), "test");

            pcrdr_release_message(msg);

            nr_got++;
            if (nr_got == NR_THREADS)
                break;
        }
        else {
            usleep(10000);  // 10m
        }

    } while (true);

    n = purc_inst_destroy_move_buffer();
    purc_log_info("move buffer destroyed, %d messages discarded\n", (int)n);

    purc_cleanup();
}

