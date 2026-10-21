/* Trusted static outer-namespace bundle materializer, NOT a flash tool.
 * The external protected launcher must approve the exact compiled executable.
 * Build for local tests: cc -static -O2 installer_materializer.c -lcrypto -o ...
 * No archive links, ownership, timestamps or arbitrary extraction paths exist.
 */
#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <openssl/sha.h>

static void fail(const char *s) { dprintf(2,"materializer: %s (errno=%d)\n",s,errno); _exit(111); }
static void exact(int fd,void *p,size_t n) {
  while(n) { ssize_t k=read(fd,p,n); if(k<=0) fail("short bundle"); p=(char*)p+k; n-=(size_t)k; }
}
static uint32_t u32(int fd) { unsigned char p[4]; exact(fd,p,4); return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint64_t u64(int fd) { uint64_t lo=u32(fd); return lo|((uint64_t)u32(fd)<<32); }
static int safe_file(int root,char *name,unsigned mode) {
  int parent=dup(root); if(parent<0) fail("dup root");
  char *save=NULL,*part=strtok_r(name,"/",&save);
  if(!part) fail("empty target");
  while(part) {
    if(!strcmp(part,".")||!strcmp(part,"..")||!*part) fail("unsafe target");
    char *next=strtok_r(NULL,"/",&save);
    if(next) {
      if(mkdirat(parent,part,0700) && errno!=EEXIST) fail("mkdir component");
      int fd=openat(parent,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
      if(fd<0) fail("open component");
      close(parent); parent=fd;
    } else {
      int fd=openat(parent,part,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
      close(parent); if(fd<0) fail("create exclusive file");
      if(fchmod(fd,mode)) fail("file mode");
      return fd;
    }
    part=next;
  }
  fail("target parse"); return -1;
}
int main(int argc,char **argv) {
  /* New launchers require this protocol token: older approved helpers reject
   * argc=3 instead of silently running payloads with the transport allowance.
   * Legacy direct qualification invocations also get the fixed hard cap. */
  if((argc!=2&&argc!=3)||strlen(argv[1])!=64) fail("expected SHA256 required");
  if(argc==3&&strcmp(argv[2],"--payload-fsize-512m")) fail("payload limit protocol");
  for(int i=0;i<64;i++) if(!((argv[1][i]>='0'&&argv[1][i]<='9')||(argv[1][i]>='a'&&argv[1][i]<='f'))) fail("invalid SHA256");
  int fd=open("/run/snapshot.bundle",O_RDONLY|O_NOFOLLOW|O_CLOEXEC); if(fd<0) fail("open readonly bundle");
  struct stat st; struct statvfs fs;
  if(fstat(fd,&st)||!S_ISREG(st.st_mode)||fstatvfs(fd,&fs)||!(fs.f_flag&ST_RDONLY)) fail("bundle is not readonly regular file");
  if(st.st_size<8||(uint64_t)st.st_size>8ULL*1024*1024*1024) fail("bundle size limit");
  SHA256_CTX hash; unsigned char digest[32],buf[65536]; SHA256_Init(&hash);
  for(;;) { ssize_t n=read(fd,buf,sizeof buf); if(n<0) fail("bundle digest read"); if(!n) break; SHA256_Update(&hash,buf,(size_t)n); }
  SHA256_Final(digest,&hash); char actual[65];
  for(int i=0;i<32;i++) sprintf(actual+2*i,"%02x",digest[i]);
  if(strcmp(actual,argv[1])) fail("BUNDLE_DIGEST_MISMATCH");
  if(lseek(fd,0,SEEK_SET)<0) fail("rewind bundle");
  unsigned char magic[4]; exact(fd,magic,4); if(memcmp(magic,"W4B1",4)) fail("bundle magic");
  uint32_t count=u32(fd); if(!count||count>1000000) fail("bundle file count");
  if(mkdir("/scratch/closure",0700)) fail("fresh closure root");
  int root=open("/scratch/closure",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC); if(root<0) fail("open closure root");
  for(uint32_t i=0;i<count;i++) {
    uint32_t length=u32(fd),mode=u32(fd); uint64_t size=u64(fd);
    if(!length||length>4095||(mode!=0444&&mode!=0555)||size>(uint64_t)st.st_size) fail("bundle record schema");
    char path[4096]; exact(fd,path,length); path[length]=0;
    if(strlen(path)!=length||path[0]=='/'||path[length-1]=='/'||strstr(path,"//")) fail("bundle target encoding");
    if(strncmp(path,"snapshot/",9)&&strcmp(path,"run/verified-context.json")&&strcmp(path,"args")&&strcmp(path,"payload")) fail("bundle target prefix");
    int output=safe_file(root,path,mode);
    while(size) { size_t chunk=size>sizeof buf?sizeof buf:(size_t)size; exact(fd,buf,chunk); size-=chunk;
      size_t off=0; while(off<chunk) { ssize_t n=write(output,buf+off,chunk-off); if(n<=0) fail("extract write"); off+=(size_t)n; }
    }
    if(close(output)) fail("extract close");
  }
  unsigned char extra; if(read(fd,&extra,1)!=0) fail("bundle trailing bytes"); close(fd); close(root);
  int args=open("/scratch/closure/args",O_RDONLY|O_NOFOLLOW); if(args<0) fail("open inner args");
  char number[32]; snprintf(number,sizeof number,"%d",args);
  int command=open("/scratch/closure/payload",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
  if(command<0) fail("open payload");
  struct stat cs; if(fstat(command,&cs)||cs.st_size<1||cs.st_size>65536) fail("payload size");
  char *data=malloc((size_t)cs.st_size); if(!data) fail("payload allocation");
  exact(command,data,(size_t)cs.st_size); close(command);
  if(data[cs.st_size-1]!=0) fail("payload terminator");
  char *inner[40]={"/scratch/closure/snapshot/system_tools/bwrap","--args",number,"--"};
  size_t offset=0; int countargs=4;
  while(offset<(size_t)cs.st_size) {
    if(countargs>=39||!data[offset]) fail("payload argument limit");
    inner[countargs++]=data+offset; offset+=strlen(data+offset)+1;
  }
  inner[countargs]=NULL;
  if(strcmp(inner[4],"/snapshot/python/bin/python")) fail("fixed Python payload required");
  /* Fixed trusted handoff, not a caller-controlled limit or executable.
   * Outer Bubblewrap must materialize an up-to-8GiB immutable transport.
   * No payload byte executes until BOTH soft and hard limits are lowered.
   * Inner bwrap only mounts the extracted readonly files; it does not copy
   * the bundle. A too-small inherited hard limit fails closed here.
   */
  const struct rlimit payload_limit={512ULL*1024*1024,512ULL*1024*1024};
  if(setrlimit(RLIMIT_FSIZE,&payload_limit)) fail("payload file limit handoff");
  execv(inner[0],inner); fail("exec approved inner bwrap");
}
