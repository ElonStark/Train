/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
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

#include "It_vfs_jffs.h"

static UINT32 testcase(VOID)
{
    INT32 fd = -1;
    INT32 fd1 = -1;
    INT32 len, ret;
    struct stat statBuf1 = {0};
    CHAR filebuf[JFFS_STANDARD_NAME_LENGTH] = "1234567890abcde&";
    CHAR readbuf[JFFS_STANDARD_NAME_LENGTH] = {0};
    CHAR pathname1[JFFS_STANDARD_NAME_LENGTH] = { JFFS_PATH_NAME0 };
    CHAR pathname2[JFFS_STANDARD_NAME_LENGTH] = { JFFS_PATH_NAME01 };

    fd = open(pathname1, O_NONBLOCK | O_CREAT | O_RDWR, HIGHEST_AUTHORITY);
    ICUNIT_GOTO_NOT_EQUAL(fd, JFFS_IS_ERROR, fd, EXIT);

    len = write(fd, filebuf, strlen(filebuf));
    ICUNIT_GOTO_EQUAL(len, strlen(filebuf), len, EXIT3);

    ret = close(fd);
    ICUNIT_GOTO_EQUAL(ret, JFFS_NO_ERROR, ret, EXIT3);

    ret = symlink(pathname1, pathname2);
    ICUNIT_GOTO_EQUAL(ret, JFFS_NO_ERROR, ret, EXIT1);

    ret = stat(pathname2, &statBuf1);
    ICUNIT_GOTO_EQUAL(ret, JFFS_NO_ERROR, ret, EXIT2);
    ICUNIT_GOTO_EQUAL(statBuf1.st_mode & S_IFMT, S_IFLNK, statBuf1.st_mode & S_IFMT, EXIT2);
    ICUNIT_GOTO_EQUAL(statBuf1.st_size, strlen(pathname1), statBuf1.st_size, EXIT2);

    len = readlink(pathname2, readbuf, sizeof(readbuf));
    ICUNIT_GOTO_EQUAL(len, strlen(pathname1), len, EXIT1);
    ICUNIT_GOTO_STRING_EQUAL(readbuf, pathname1, readbuf, EXIT2);

    fd1 = open(pathname2, O_NONBLOCK | O_RDWR, HIGHEST_AUTHORITY);
    ICUNIT_GOTO_NOT_EQUAL(fd, JFFS_IS_ERROR, fd, EXIT2);

    memset(readbuf, 0, JFFS_STANDARD_NAME_LENGTH);
    len = read(fd1, readbuf, strlen(filebuf));
    ICUNIT_GOTO_EQUAL(len, strlen(filebuf), len, EXIT4);
    ICUNIT_GOTO_STRING_EQUAL(readbuf, filebuf, readbuf, EXIT4);

    ret = close(fd1);
    ICUNIT_GOTO_EQUAL(ret, JFFS_NO_ERROR, ret, EXIT4);

    ret = unlink(pathname2);
    ICUNIT_GOTO_EQUAL(ret, JFFS_NO_ERROR, ret, EXIT2);

    ret = unlink(pathname1);
    ICUNIT_GOTO_EQUAL(ret, JFFS_NO_ERROR, ret, EXIT1);

    return JFFS_NO_ERROR;

EXIT4:
    close(fd1);
    goto EXIT2;
EXIT3:
    close(fd);
    goto EXIT1;
EXIT2:
    unlink(pathname2);
EXIT1:
    unlink(pathname1);
EXIT:
    return JFFS_NO_ERROR;
}

VOID ItFsTestSymlink001(VOID)
{
    TEST_ADD_CASE("IT_FS_TEST_SYMLINK_001", testcase, TEST_VFS, TEST_JFFS, TEST_LEVEL0, TEST_FUNCTION);
}

