/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_posix_mutex.h"
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cpluscplus */
#endif /* __cpluscplus */


static pthread_mutex_t g_mutex066;

static void *TaskF01(void *argv)
{
    UINT32 ret;
    g_testCount++;

    ret = pthread_mutex_trylock(&g_mutex066);
    ICUNIT_TRACK_EQUAL(ret, 0, ret);

    ret = pthread_mutex_trylock(&g_mutex066);
    ICUNIT_TRACK_EQUAL(ret, EBUSY, ret);

    LOS_TaskDelay(2); // 2, set delay time.

    ret = pthread_mutex_unlock(&g_mutex066);
    ICUNIT_TRACK_EQUAL(ret, 0, ret);

    ret = pthread_mutex_unlock(&g_mutex066);
    ICUNIT_TRACK_NOT_EQUAL(ret, 0, ret);

    g_testCount++;

    return NULL;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    pthread_t newTh;
    pthread_attr_t attr;

    g_testCount = 0;

    ret = pthread_mutex_init(&g_mutex066, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = PosixPthreadInit(&attr, 4); // 4, Set thread priority.
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = pthread_create(&newTh, &attr, TaskF01, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);

    ret = pthread_mutex_destroy(&g_mutex066);
    ICUNIT_ASSERT_EQUAL(ret, EBUSY, ret);

    LOS_TaskDelay(3); // 3, set delay time.

    ICUNIT_ASSERT_EQUAL(g_testCount, 2, g_testCount); // 2, Assert that g_testCount.

    ret = pthread_mutex_destroy(&g_mutex066);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = PosixPthreadDestroy(&attr, newTh);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    return LOS_OK;
}


VOID ItPosixMux066(void)
{
    TEST_ADD_CASE("ItPosixMux066", Testcase, TEST_POSIX, TEST_MUX, TEST_LEVEL2, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cpluscplus */
#endif /* __cpluscplus */