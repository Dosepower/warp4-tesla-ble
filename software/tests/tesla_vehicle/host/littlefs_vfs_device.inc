// Native RAM block device + public FD table. Every file hook used by the
// production adapter is the extracted vfs_littlefs_* body over full lfs.c.
// Production WARP4 data partition geometry: 0x360000 bytes, 4 KiB blocks.
#define VFS_BLOCKS 864
static uint8_t disk[VFS_BLOCKS][4096];
static lfs_t fs;
static esp_littlefs_t efs;
unsigned harness_locks;
static int public_fds[32];
static int dev_read(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,void *p,lfs_size_t n) {
    (void)c;assert(b<VFS_BLOCKS && off+n<=4096);memcpy(p,disk[b]+off,n);return 0;
}
static int dev_prog(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,const void *p,lfs_size_t n) {
    (void)c;assert(b<VFS_BLOCKS && off+n<=4096);for(unsigned i=0;i<n;++i) { disk[b][off+i]&=((const uint8_t *)p)[i]; }return 0;
}
static int dev_erase(const struct lfs_config *c,lfs_block_t b) { (void)c;assert(b<VFS_BLOCKS);memset(disk[b],255,4096);return 0; }
static int dev_sync(const struct lfs_config *c) { (void)c;return 0; }
void vfs_backend_setup(void) {
    memset(disk,255,sizeof(disk));memset(&fs,0,sizeof(fs));memset(&efs,0,sizeof(efs));
    efs.cfg=(struct lfs_config){.read=dev_read,.prog=dev_prog,.erase=dev_erase,.sync=dev_sync,
        .read_size=128,.prog_size=16,.block_size=4096,.block_count=VFS_BLOCKS,.cache_size=512,.lookahead_size=128,.block_cycles=512,
        .disk_version=0x00020000}; // CONFIG_LITTLEFS_DISK_VERSION_2_0
    assert(lfs_format(&fs,&efs.cfg)==0 && lfs_mount(&fs,&efs.cfg)==0);
    assert(lfs_mkdir(&fs,"/config")==0);
    efs.fs=&fs;efs.cache_size=CONFIG_LITTLEFS_FD_CACHE_MIN_SIZE;efs.cache=calloc(efs.cache_size,sizeof(*efs.cache));assert(efs.cache);
    for(unsigned i=0;i<32;++i) { public_fds[i]=-1; }
    harness_locks=0;
}
// VFS prefix stripping as IDF does for the "/data" mount.
static const char *relative_path(const char *path) { assert(!strncmp(path,"/data/",6));return path+5; }
static int local_fd(int fd) { assert(fd>=3 && fd<32 && public_fds[fd]>=0);return public_fds[fd]; }
int vfs_backend_open(const char *path,int flags) {
    const int local=vfs_littlefs_open(&efs,relative_path(path),flags,0600);
    if(local<0) { return -1; }
    for(int fd=3;fd<32;++fd) { if(public_fds[fd]<0) { public_fds[fd]=local;return fd; } }
    assert(false);return -1;
}
int vfs_backend_close(int fd) { const int local=local_fd(fd);public_fds[fd]=-1;return vfs_littlefs_close(&efs,local); }
long vfs_backend_read(int fd,void *p,size_t n) { return vfs_littlefs_read(&efs,local_fd(fd),p,n); }
long vfs_backend_write(int fd,const void *p,size_t n) { return vfs_littlefs_write(&efs,local_fd(fd),p,n); }
int vfs_backend_fsync(int fd) { return vfs_littlefs_fsync(&efs,local_fd(fd)); }
int vfs_backend_fstat(int fd,struct stat *st) { return vfs_littlefs_fstat(&efs,local_fd(fd),st); }
int vfs_backend_stat(const char *p,struct stat *st) { return vfs_littlefs_stat(&efs,relative_path(p),st); }
int vfs_backend_rename(const char *a,const char *b) { return vfs_littlefs_rename(&efs,relative_path(a),relative_path(b)); }
int vfs_backend_unlink(const char *p) { return vfs_littlefs_unlink(&efs,relative_path(p)); }
unsigned vfs_backend_open_fds(void) { return efs.fd_count; }
unsigned vfs_backend_locks(void) { return harness_locks; }
void vfs_backend_teardown(void) { assert(!fs.mlist && !efs.fd_count);esp_littlefs_free_fds(&efs);assert(lfs_unmount(&fs)==0); }
