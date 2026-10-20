// Native RAM block device: full untouched production lfs.c handles all I/O.
static uint8_t disk[64][4096];
static lfs_t fs;
static esp_littlefs_t efs;
static unsigned close_calls;
static bool fail_close, fail_sync;
static int dev_read(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,void *p,lfs_size_t n) {
    (void)c;assert(b<64 && off+n<=4096);memcpy(p,disk[b]+off,n);return 0;
}
static int dev_prog(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,const void *p,lfs_size_t n) {
    (void)c;assert(b<64 && off+n<=4096);for(unsigned i=0;i<n;++i) { disk[b][off+i]&=((const uint8_t *)p)[i]; }return 0;
}
static int dev_erase(const struct lfs_config *c,lfs_block_t b) { (void)c;assert(b<64);memset(disk[b],255,4096);return 0; }
static int dev_sync(const struct lfs_config *c) { (void)c;return fail_sync?LFS_ERR_IO:0; }
static int counted_close(void *ctx,int fd) {
    ++close_calls;
    if(fail_close) {
        // Force a real LittleFS metadata/device-sync error after a previously
        // successful explicit fsync. This is not a synthetic lfs_file_close rc.
        efs.cache[fd]->file.flags|=LFS_F_DIRTY;fail_sync=true;
    }
    const int r=vfs_littlefs_close(ctx,fd);fail_sync=false;return r;
}
static const esp_vfs_fs_ops_t ops={.close_p=counted_close};
static vfs_entry_t entry={.flags=ESP_VFS_FLAG_CONTEXT_PTR,.vfs=&ops,.ctx=&efs};
void backend_setup(void) {
    memset(disk,255,sizeof(disk));memset(&fs,0,sizeof(fs));memset(&efs,0,sizeof(efs));
    efs.cfg=(struct lfs_config){.read=dev_read,.prog=dev_prog,.erase=dev_erase,.sync=dev_sync,
        .read_size=16,.prog_size=16,.block_size=4096,.block_count=64,.cache_size=512,.lookahead_size=16,.block_cycles=500};
    assert(lfs_format(&fs,&efs.cfg)==0 && lfs_mount(&fs,&efs.cfg)==0);
    assert(lfs_mkdir(&fs,"/config")==0);
    efs.fs=&fs;efs.cache_size=CONFIG_LITTLEFS_FD_CACHE_MIN_SIZE;efs.cache=calloc(efs.cache_size,sizeof(*efs.cache));assert(efs.cache);
    s_vfs[0]=&entry;for(unsigned i=0;i<MAX_FDS;++i) { s_fd_table[i]=FD_TABLE_ENTRY_UNUSED; }
    close_calls=0;fail_close=fail_sync=false;
}
static const char *relative_path(const char *path) { assert(!strncmp(path,"/data/",6));return path+5; }
int backend_open(const char *path,int flags) {
    path=relative_path(path);vfs_littlefs_file_t *f=NULL;
    const int local=esp_littlefs_allocate_fd(&efs,&f,strlen(path)+1);if(local<0) { errno=ENOMEM;return -1; }
    strcpy(f->path,path);
    const int lflags=(flags&O_WRONLY)?LFS_O_WRONLY|LFS_O_CREAT|LFS_O_TRUNC:LFS_O_RDONLY;
    const int r=lfs_file_opencfg(&fs,&f->file,path,lflags,&f->lfs_file_config);
    if(r<0) { esp_littlefs_free_fd(&efs,local);errno=lfs_errno_remap(r);return -1; }
    for(unsigned fd=3;fd<MAX_FDS;++fd) { if(s_fd_table[fd].vfs_index<0) {
        s_fd_table[fd]=(fd_table_t){.vfs_index=0,.local_fd=local};return fd;
    } }
    assert(false);return -1;
}
int backend_close(int fd) { struct _reent r;return esp_vfs_close(&r,fd); }
int backend_retry_detached(int fd) { return vfs_littlefs_close(&efs,fd); }
void backend_fail_close(bool fail) { fail_close=fail; }
unsigned backend_close_calls(void) { return close_calls; }
unsigned backend_private_fds(void) { return efs.fd_count; }
unsigned backend_open_files(void) { unsigned n=0;for(struct lfs_mlist *f=fs.mlist;f;f=f->next) { ++n; }return n; }
unsigned backend_public_fds(void) { unsigned n=0;for(unsigned i=0;i<MAX_FDS;++i) { n+=s_fd_table[i].vfs_index>=0; }return n; }
bool backend_inline_cache_owned(void) { return efs.file && efs.file->lfs_file_config.buffer==efs.file->lfs_buffer && efs.cache[0]==efs.file; }
static lfs_file_t *opened(int fd) { assert(fd_valid(fd) && s_fd_table[fd].vfs_index==0);return &efs.cache[s_fd_table[fd].local_fd]->file; }
long backend_write(int fd,const void *p,size_t n) { return lfs_file_write(&fs,opened(fd),p,n); }
long backend_read(int fd,void *p,size_t n) { return lfs_file_read(&fs,opened(fd),p,n); }
int backend_fsync(int fd) { return lfs_file_sync(&fs,opened(fd)); }
int backend_fstat(int fd,struct stat *st) { st->st_mode=S_IFREG;st->st_size=lfs_file_size(&fs,opened(fd));return st->st_size<0?-1:0; }
int backend_stat(const char *p,struct stat *st) { struct lfs_info i;const int r=lfs_stat(&fs,relative_path(p),&i);if(r<0) { errno=lfs_errno_remap(r);return -1; }st->st_mode=S_IFREG;st->st_size=i.size;return 0; }
int backend_rename(const char *a,const char *b) { const int r=lfs_rename(&fs,relative_path(a),relative_path(b));if(r<0) { errno=lfs_errno_remap(r);return -1; }return 0; }
int backend_unlink(const char *p) { const int r=lfs_remove(&fs,relative_path(p));if(r<0) { errno=lfs_errno_remap(r);return -1; }return 0; }
void backend_teardown(void) {
    // Test fixture whole-mount teardown ONLY. Not an application recovery API.
    assert(!fs.mlist);esp_littlefs_free_fds(&efs);assert(lfs_unmount(&fs)==0);
}
