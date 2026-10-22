// Native OS/open/flash shim only. Production close/FD-release/rename and full lfs.c are retained.
#include <dirent.h>
void backend_remount(void) {
    struct lfs_config config=efs.cfg;
    assert(lfs_unmount(&fs)==0); esp_littlefs_free_fds(&efs);
    memset(&fs,0,sizeof(fs)); memset(&efs,0,sizeof(efs));efs.cfg=config;
    assert(lfs_mount(&fs,&efs.cfg)==0);efs.fs=&fs;
    efs.cache_size=CONFIG_LITTLEFS_FD_CACHE_MIN_SIZE;efs.cache=calloc(efs.cache_size,sizeof(*efs.cache));assert(efs.cache);
    for(unsigned i=0;i<MAX_FDS;++i)s_fd_table[i]=FD_TABLE_ENTRY_UNUSED;
}
int backend_lstat(const char *path,struct stat *out) {
    memset(out,0,sizeof(*out));
    if(strcmp(path,"/data")==0) {out->st_mode=S_IFDIR|0700;return 0;}
    struct lfs_info info;int rc=lfs_stat(&fs,relative_path(path),&info);
    if(rc<0){errno=lfs_errno_remap(rc);return -1;}
    out->st_mode=(info.type==LFS_TYPE_DIR?S_IFDIR:S_IFREG)|0600;out->st_size=info.size;return 0;
}
int backend_truncate(int fd,long length) {
    int rc=lfs_file_truncate(&fs,opened(fd),length);
    if(rc<0){errno=lfs_errno_remap(rc);return -1;}return 0;
}
int backend_bound_rename(const char *source,const char *dest) {
    // Native open shim recorded full paths; restore the production path hash.
    for(unsigned i=0;i<efs.cache_size;++i)if(efs.cache[i])efs.cache[i]->hash=compute_hash(efs.cache[i]->path);
    return vfs_littlefs_rename(&efs,relative_path(source),relative_path(dest));
}
struct native_dir {lfs_dir_t dir;struct dirent entry;};
void *backend_opendir(const char *path) {
    struct native_dir *p=calloc(1,sizeof(*p));assert(p);
    int rc=lfs_dir_open(&fs,&p->dir,strcmp(path,"/data")==0?"/":relative_path(path));
    if(rc<0){free(p);errno=lfs_errno_remap(rc);return NULL;}return p;
}
struct dirent *backend_readdir(void *opaque) {
    struct native_dir *p=opaque;struct lfs_info info;
    int rc=lfs_dir_read(&fs,&p->dir,&info);
    if(rc<0){errno=lfs_errno_remap(rc);return NULL;}if(rc==0)return NULL;
    assert(strlen(info.name)<sizeof(p->entry.d_name));strcpy(p->entry.d_name,info.name);
    p->entry.d_type=info.type==LFS_TYPE_DIR?DT_DIR:DT_REG;return &p->entry;
}
int backend_closedir(void *opaque) {
    struct native_dir *p=opaque;int rc=lfs_dir_close(&fs,&p->dir);free(p);
    if(rc<0){errno=lfs_errno_remap(rc);return -1;}return 0;
}
