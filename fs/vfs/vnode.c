/*
 * Copyright (c) 2021-2021 Huawei Device Co., Ltd. All rights reserved.
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

#include "los_mux.h"
#include "vnode.h"
#include "fs/dirent_fs.h"
#include "path_cache.h"

LIST_HEAD g_vnodeFreeList;              /* free vnodes list */
LIST_HEAD g_vnodeVirtualList;           /* dev vnodes list */
LIST_HEAD g_vnodeActiveList;              /* inuse vnodes list */
static int g_freeVnodeSize = 0;         /* system free vnodes size */
static int g_totalVnodeSize = 0;        /* total vnode size */

static LosMux g_vnodeMux;
static struct Vnode *g_rootVnode = NULL;
static struct VnodeOps g_devfsOps;

#define ENTRY_TO_VNODE(ptr)  LOS_DL_LIST_ENTRY(ptr, struct Vnode, actFreeEntry)
#define VNODE_LRU_COUNT      10
#define DEV_VNODE_MODE       0755

int VnodesInit(void)
{
    int retval = LOS_MuxInit(&g_vnodeMux, NULL);
    if (retval != LOS_OK) {
        PRINT_ERR("Create mutex for vnode fail, status: %d", retval);
        return retval;
    }

    LOS_ListInit(&g_vnodeFreeList);
    LOS_ListInit(&g_vnodeVirtualList);
    LOS_ListInit(&g_vnodeActiveList);
    retval = VnodeAlloc(NULL, &g_rootVnode);
    if (retval != LOS_OK) {
        PRINT_ERR("VnodeInit failed error %d\n", retval);
        return retval;
    }
    g_rootVnode->mode = S_IRWXU | S_IRWXG | S_IRWXO | S_IFDIR;
    g_rootVnode->type = VNODE_TYPE_DIR;
    g_rootVnode->filePath = "/";

    return LOS_OK;
}

static struct Vnode *GetFromFreeList(void)
{
    if (g_freeVnodeSize <= 0) {
        return NULL;
    }
    struct Vnode *vnode = NULL;

    if (LOS_ListEmpty(&g_vnodeFreeList)) {
        PRINT_ERR("get vnode from free list failed, list empty but g_freeVnodeSize = %d!\n", g_freeVnodeSize);
        g_freeVnodeSize = 0;
        return NULL;
    }

    vnode = ENTRY_TO_VNODE(LOS_DL_LIST_FIRST(&g_vnodeFreeList));
    LOS_ListDelete(&vnode->actFreeEntry);
    g_freeVnodeSize--;
    return vnode;
}

struct Vnode *VnodeReclaimLru(void)
{
    struct Vnode *item = NULL;
    struct Vnode *nextItem = NULL;
    int releaseCount = 0;

    LOS_DL_LIST_FOR_EACH_ENTRY_SAFE(item, nextItem, &g_vnodeActiveList, struct Vnode, actFreeEntry) {
        if ((item->useCount > 0) ||
            (item->flag & VNODE_FLAG_MOUNT_ORIGIN) ||
            (item->flag & VNODE_FLAG_MOUNT_NEW)) {
            continue;
        }

        if (VnodeFree(item) == LOS_OK) {
            releaseCount++;
        }
        if (releaseCount >= VNODE_LRU_COUNT) {
            break;
        }
    }

    if (releaseCount == 0) {
        PRINT_ERR("VnodeAlloc failed, vnode size hit max but can't reclaim anymore!\n");
        return NULL;
    }

    item = GetFromFreeList();
    if (item == NULL) {
        PRINT_ERR("VnodeAlloc failed, reclaim and get from free list failed!\n");
    }
    return item;
}

int VnodeAlloc(struct VnodeOps *vop, struct Vnode **newVnode)
{
    struct Vnode* vnode = NULL;

    VnodeHold();
    vnode = GetFromFreeList();
    if ((vnode == NULL) && g_totalVnodeSize < LOSCFG_MAX_VNODE_SIZE) {
        vnode = (struct Vnode*)zalloc(sizeof(struct Vnode));
        g_totalVnodeSize++;
    }

    if (vnode == NULL) {
        vnode = VnodeReclaimLru();
    }

    if (vnode == NULL) {
        *newVnode = NULL;
        VnodeDrop();
        return -ENOMEM;
    }

    vnode->type = VNODE_TYPE_UNKNOWN;
    LOS_ListInit((&(vnode->parentPathCaches)));
    LOS_ListInit((&(vnode->childPathCaches)));
    LOS_ListInit((&(vnode->hashEntry)));
    LOS_ListInit((&(vnode->actFreeEntry)));

    if (vop == NULL) {
        LOS_ListAdd(&g_vnodeVirtualList, &(vnode->actFreeEntry));
        vnode->vop = &g_devfsOps;
    } else {
        LOS_ListTailInsert(&g_vnodeActiveList, &(vnode->actFreeEntry));
        vnode->vop = vop;
    }
    LOS_ListInit(&vnode->mapping.page_list);
    LOS_SpinInit(&vnode->mapping.list_lock);
    (VOID)LOS_MuxInit(&vnode->mapping.mux_lock, NULL);
    vnode->mapping.host = vnode;

    VnodeDrop();

    *newVnode = vnode;

    return LOS_OK;
}

int VnodeFree(struct Vnode *vnode)
{
    if (vnode == NULL) {
        return LOS_OK;
    }

    VnodeHold();
    if (vnode->useCount > 0) {
        VnodeDrop();
        return -EBUSY;
    }

    VnodePathCacheFree(vnode);
    LOS_ListDelete(&(vnode->hashEntry));
    LOS_ListDelete(&vnode->actFreeEntry);

    if (vnode->vop->Reclaim) {
        vnode->vop->Reclaim(vnode);
    }

    if (vnode->filePath) {
        free(vnode->filePath);
    }
    if (vnode->vop == &g_devfsOps) {
        /* for dev vnode, just free it */
        free(vnode->data);
        free(vnode);
        g_totalVnodeSize--;
    } else {
        /* for normal vnode, reclaim it to g_VnodeFreeList */
        memset_s(vnode, sizeof(struct Vnode), 0, sizeof(struct Vnode));
        LOS_ListAdd(&g_vnodeFreeList, &vnode->actFreeEntry);
        g_freeVnodeSize++;
    }
    VnodeDrop();

    return LOS_OK;
}

int VnodeFreeAll(const struct Mount *mount)
{
    struct Vnode *vnode = NULL;
    struct Vnode *nextVnode = NULL;
    int ret;

    LOS_DL_LIST_FOR_EACH_ENTRY_SAFE(vnode, nextVnode, &g_vnodeActiveList, struct Vnode, actFreeEntry) {
        if ((vnode->originMount == mount) && !(vnode->flag & VNODE_FLAG_MOUNT_NEW)) {
            ret = VnodeFree(vnode);
            if (ret != LOS_OK) {
                return ret;
            }
        }
    }

    return LOS_OK;
}

BOOL VnodeInUseIter(const struct Mount *mount)
{
    struct Vnode *vnode = NULL;

    LOS_DL_LIST_FOR_EACH_ENTRY(vnode, &g_vnodeActiveList, struct Vnode, actFreeEntry) {
        if (vnode->originMount == mount) {
            if ((vnode->useCount > 0) || (vnode->flag & VNODE_FLAG_MOUNT_ORIGIN)) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

int VnodeHold()
{
    int ret = LOS_MuxLock(&g_vnodeMux, LOS_WAIT_FOREVER);
    if (ret != LOS_OK) {
        PRINT_ERR("VnodeHold lock failed !\n");
    }
    return ret;
}

int VnodeDrop()
{
    int ret = LOS_MuxUnlock(&g_vnodeMux);
    if (ret != LOS_OK) {
        PRINT_ERR("VnodeDrop unlock failed !\n");
    }
    return ret;
}

static char *NextName(char *pos, uint8_t *len)
{
    char *name = NULL;
    while (*pos != 0 && *pos == '/') {
        pos++;
    }
    if (*pos == '\0') {
        return NULL;
    }
    name = (char *)pos;
    while (*pos != '\0' && *pos != '/') {
        pos++;
    }
    *len = pos - name;
    return name;
}

static int PreProcess(const char *originPath, struct Vnode **startVnode, char **path)
{
    int ret;
    char *absolutePath = NULL;

    ret = vfs_normalize_path(NULL, originPath, &absolutePath);
    if (ret == LOS_OK) {
        *startVnode = g_rootVnode;
        *path = absolutePath;
    }

    return ret;
}

static struct Vnode *ConvertVnodeIfMounted(struct Vnode *vnode)
{
    if ((vnode == NULL) || !(vnode->flag & VNODE_FLAG_MOUNT_ORIGIN)) {
        return vnode;
    }
    return vnode->newMount->vnodeCovered;
}

static void RefreshLRU(struct Vnode *vnode)
{
    if (vnode == NULL || (vnode->type != VNODE_TYPE_REG && vnode->type != VNODE_TYPE_DIR) ||
        vnode->vop == &g_devfsOps || vnode->vop == NULL) {
        return;
    }
    LOS_ListDelete(&(vnode->actFreeEntry));
    LOS_ListTailInsert(&g_vnodeActiveList, &(vnode->actFreeEntry));
}

static int ProcessVirtualVnode(struct Vnode *parent, uint32_t flags, struct Vnode **vnode)
{
    int ret = -ENOENT;
    if (flags & V_CREATE) {
        // only create /dev/ vnode
        ret = VnodeAlloc(NULL, vnode);
    }
    if (ret == LOS_OK) {
        (*vnode)->parent = parent;
    }
    return ret;
}

static int Step(char **currentDir, struct Vnode **currentVnode, uint32_t flags)
{
    int ret;
    uint8_t len = 0;
    struct Vnode *nextVnode = NULL;
    char *nextDir = NULL;

    if ((*currentVnode)->type != VNODE_TYPE_DIR) {
        return -ENOTDIR;
    }
    nextDir = NextName(*currentDir, &len);
    if (nextDir == NULL) {
        // there is '/' at the end of the *currentDir.
        *currentDir = NULL;
        return LOS_OK;
    }

    ret = PathCacheLookup(*currentVnode, nextDir, len, &nextVnode);
    if (ret == LOS_OK) {
        goto STEP_FINISH;
    }

    (*currentVnode)->useCount++;
    if (flags & V_DUMMY) {
        ret = ProcessVirtualVnode(*currentVnode, flags, &nextVnode);
    } else {
        if ((*currentVnode)->vop != NULL && (*currentVnode)->vop->Lookup != NULL) {
            ret = (*currentVnode)->vop->Lookup(*currentVnode, nextDir, len, &nextVnode);
        } else {
            ret = -ENOSYS;
        }
    }
    (*currentVnode)->useCount--;

    if (ret == LOS_OK) {
        (void)PathCacheAlloc((*currentVnode), nextVnode, nextDir, len);
    }

STEP_FINISH:
    nextVnode = ConvertVnodeIfMounted(nextVnode);
    RefreshLRU(nextVnode);

    *currentDir = nextDir + len;
    if (ret == LOS_OK) {
        *currentVnode = nextVnode;
    }

    return ret;
}

int VnodeLookupAt(const char *path, struct Vnode **result, uint32_t flags, struct Vnode *orgVnode)
{
    int ret;
    int vnodePathLen;
    char *vnodePath = NULL;
    struct Vnode *startVnode = NULL;
    char *normalizedPath = NULL;

    if (orgVnode != NULL) {
        startVnode = orgVnode;
        normalizedPath = strdup(path);
    } else {
        ret = PreProcess(path, &startVnode, &normalizedPath);
        if (ret != LOS_OK) {
            PRINT_ERR("[VFS]lookup failed, invalid path err = %d\n", ret);
            goto OUT_FREE_PATH;
        }
    }

    if (normalizedPath[0] == '/' && normalizedPath[1] == '\0') {
        *result = g_rootVnode;
        free(normalizedPath);
        return LOS_OK;
    }

    char *currentDir = normalizedPath;
    struct Vnode *currentVnode = startVnode;

    while (*currentDir != '\0') {
        ret = Step(&currentDir, &currentVnode, flags);
        if (currentDir == NULL || *currentDir == '\0') {
            // return target or parent vnode as result
            *result = currentVnode;
            if (currentVnode->filePath == NULL) {
                currentVnode->filePath = normalizedPath;
            } else {
                free(normalizedPath);
            }
            return ret;
        } else if (VfsVnodePermissionCheck(currentVnode, EXEC_OP)) {
            ret = -EACCES;
            goto OUT_FREE_PATH;
        }

        if (ret != LOS_OK) {
            // no such file, lookup failed
            goto OUT_FREE_PATH;
        }
        if (currentVnode->filePath == NULL) {
            vnodePathLen = currentDir - normalizedPath;
            vnodePath = malloc(vnodePathLen + 1);
            if (vnodePath == NULL) {
                ret = -ENOMEM;
                goto OUT_FREE_PATH;
            }
            ret = strncpy_s(vnodePath, vnodePathLen + 1, normalizedPath, vnodePathLen);
            if (ret != EOK) {
                ret = -ENAMETOOLONG;
                free(vnodePath);
                goto OUT_FREE_PATH;
            }
            currentVnode->filePath = vnodePath;
            currentVnode->filePath[vnodePathLen] = 0;
        }
    }
    return ret;

OUT_FREE_PATH:
    if (normalizedPath) {
        free(normalizedPath);
    }
    return ret;
}

int VnodeLookup(const char *path, struct Vnode **vnode, uint32_t flags)
{
    return VnodeLookupAt(path, vnode, flags, NULL);
}

static void ChangeRootInternal(struct Vnode *rootOld, char *dirname)
{
    int ret;
    struct Mount *mnt = NULL;
    char *name = NULL;
    struct Vnode *node = NULL;
    struct Vnode *nodeInFs = NULL;
    struct PathCache *item = NULL;
    struct PathCache *nextItem = NULL;

    LOS_DL_LIST_FOR_EACH_ENTRY_SAFE(item, nextItem, &rootOld->childPathCaches, struct PathCache, childEntry) {
        name = item->name;
        node = item->childVnode;

        if (strcmp(name, dirname)) {
            continue;
        }
        PathCacheFree(item);

        ret = VnodeLookup(dirname, &nodeInFs, 0);
        if (ret) {
            PRINTK("%s-%d %s NOT exist in rootfs\n", __FUNCTION__, __LINE__, dirname);
            break;
        }

        mnt = node->newMount;
        mnt->vnodeBeCovered = nodeInFs;

        nodeInFs->newMount = mnt;
        nodeInFs->flag |= VNODE_FLAG_MOUNT_ORIGIN;

        break;
    }
}

void ChangeRoot(struct Vnode *rootNew)
{
    struct Vnode *rootOld = g_rootVnode;
    g_rootVnode = rootNew;
    ChangeRootInternal(rootOld, "proc");
    ChangeRootInternal(rootOld, "dev");
}

static int VnodeReaddir(struct Vnode *vp, struct fs_dirent_s *dir)
{
    int result;
    int cnt = 0;
    off_t i = 0;
    off_t idx;
    unsigned int dstNameSize;

    struct PathCache *item = NULL;
    struct PathCache *nextItem = NULL;

    if (dir == NULL) {
        return -EINVAL;
    }

    LOS_DL_LIST_FOR_EACH_ENTRY_SAFE(item, nextItem, &vp->childPathCaches, struct PathCache, childEntry) {
        if (i < dir->fd_position) {
            i++;
            continue;
        }

        idx = i - dir->fd_position;

        dstNameSize = sizeof(dir->fd_dir[idx].d_name);
        result = strncpy_s(dir->fd_dir[idx].d_name, dstNameSize, item->name, item->nameLen);
        if (result != EOK) {
            return -ENAMETOOLONG;
        }
        dir->fd_dir[idx].d_off = i;
        dir->fd_dir[idx].d_reclen = (uint16_t)sizeof(struct dirent);

        i++;
        if (++cnt >= dir->read_cnt) {
            break;
        }
    }

    dir->fd_position = i;

    return cnt;
}

int VnodeOpendir(struct Vnode *vnode, struct fs_dirent_s *dir)
{
    (void)vnode;
    (void)dir;
    return LOS_OK;
}

int VnodeClosedir(struct Vnode *vnode, struct fs_dirent_s *dir)
{
    (void)vnode;
    (void)dir;
    return LOS_OK;
}

int VnodeCreate(struct Vnode *parent, const char *name, int mode, struct Vnode **vnode)
{
    int ret;
    struct Vnode *newVnode = NULL;

    ret = VnodeAlloc(NULL, &newVnode);
    if (ret != 0) {
        return -ENOMEM;
    }

    newVnode->type = VNODE_TYPE_CHR;
    newVnode->vop = parent->vop;
    newVnode->fop = parent->fop;
    newVnode->data = NULL;
    newVnode->parent = parent;
    newVnode->originMount = parent->originMount;
    newVnode->uid = parent->uid;
    newVnode->gid = parent->gid;
    newVnode->mode = mode;
    /* The 'name' here is not full path, but for device we don't depend on this path, it's just a name for DFx.
       When we have devfs, we can get a fullpath. */
    newVnode->filePath = strdup(name);

    *vnode = newVnode;
    return 0;
}

int VnodeDevInit()
{
    struct Vnode *devNode = NULL;
    struct Mount *devMount = NULL;

    int retval = VnodeLookup("/dev", &devNode, V_CREATE | V_DUMMY);
    if (retval != LOS_OK) {
        PRINT_ERR("VnodeDevInit failed error %d\n", retval);
        return retval;
    }
    devNode->mode = DEV_VNODE_MODE | S_IFDIR;
    devNode->type = VNODE_TYPE_DIR;

    devMount = MountAlloc(devNode, NULL);
    if (devMount == NULL) {
        PRINT_ERR("VnodeDevInit failed mount point alloc failed.\n");
        return -ENOMEM;
    }
    devMount->vnodeCovered = devNode;
    devMount->vnodeBeCovered->flag |= VNODE_FLAG_MOUNT_ORIGIN;
    return LOS_OK;
}

int VnodeGetattr(struct Vnode *vnode, struct stat *buf)
{
    (void)memset_s(buf, sizeof(struct stat), 0, sizeof(struct stat));
    buf->st_mode = vnode->mode;
    buf->st_uid = vnode->uid;
    buf->st_gid = vnode->gid;

    return LOS_OK;
}

struct Vnode *VnodeGetRoot()
{
    return g_rootVnode;
}

static int VnodeChattr(struct Vnode *vnode, struct IATTR *attr)
{
    mode_t tmpMode;
    if (vnode == NULL || attr == NULL) {
        return -EINVAL;
    }
    if (attr->attr_chg_valid & CHG_MODE) {
        tmpMode = attr->attr_chg_mode;
        tmpMode &= ~S_IFMT;
        vnode->mode &= S_IFMT;
        vnode->mode = tmpMode | vnode->mode;
    }
    if (attr->attr_chg_valid & CHG_UID) {
        vnode->uid = attr->attr_chg_uid;
    }
    if (attr->attr_chg_valid & CHG_GID) {
        vnode->gid = attr->attr_chg_gid;
    }
    return LOS_OK;
}

int VnodeDevLookup(struct Vnode *parentVnode, const char *path, int len, struct Vnode **vnode)
{
    (void)parentVnode;
    (void)path;
    (void)len;
    (void)vnode;
    /* dev node must in pathCache. */
    return -ENOENT;
}

static struct VnodeOps g_devfsOps = {
    .Lookup = VnodeDevLookup,
    .Getattr = VnodeGetattr,
    .Readdir = VnodeReaddir,
    .Opendir = VnodeOpendir,
    .Closedir = VnodeClosedir,
    .Create = VnodeCreate,
    .Chattr = VnodeChattr,
};

void VnodeMemoryDump(void)
{
    struct Vnode *item = NULL;
    struct Vnode *nextItem = NULL;
    int vnodeCount = 0;

    LOS_DL_LIST_FOR_EACH_ENTRY_SAFE(item, nextItem, &g_vnodeActiveList, struct Vnode, actFreeEntry) {
        if ((item->useCount > 0) ||
            (item->flag & VNODE_FLAG_MOUNT_ORIGIN) ||
            (item->flag & VNODE_FLAG_MOUNT_NEW)) {
            continue;
        }

        vnodeCount++;
    }

    PRINTK("Vnode number = %d\n", vnodeCount);
    PRINTK("Vnode memory size = %d(B)\n", vnodeCount * sizeof(struct Vnode));
}

LIST_HEAD* GetVnodeFreeList()
{
    return &g_vnodeFreeList;
}

LIST_HEAD* GetVnodeVirtualList()
{
    return &g_vnodeVirtualList;
}

LIST_HEAD* GetVnodeActiveList()
{
    return &g_vnodeActiveList;
}

int VnodeClearCache()
{
    struct Vnode *item = NULL;
    struct Vnode *nextItem = NULL;
    int count = 0;

    VnodeHold();
    LOS_DL_LIST_FOR_EACH_ENTRY_SAFE(item, nextItem, &g_vnodeActiveList, struct Vnode, actFreeEntry) {
        if ((item->useCount > 0) ||
            (item->flag & VNODE_FLAG_MOUNT_ORIGIN) ||
            (item->flag & VNODE_FLAG_MOUNT_NEW)) {
            continue;
        }

        if (VnodeFree(item) == LOS_OK) {
            count++;
        }
    }
    VnodeDrop();

    return count;
}
