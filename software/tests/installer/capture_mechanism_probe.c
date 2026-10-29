/* Deterministic counterexample to open-exit hash + resume. NOT a tracer. */
#define _GNU_SOURCE
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#if !defined(__x86_64__)
#error This exact register-level probe is qualified only on x86_64 Linux.
#endif
#define CHECK(x) do { if(!(x)) { perror(#x); exit(90); } } while(0)
int main(int argc,char **argv) {
 CHECK(argc==2 || (argc==3 && !strcmp(argv[2],"mmap")));
 int mapmode=argc==3;
 int dir=open(argv[1],O_RDONLY|O_DIRECTORY); CHECK(dir>=0);
 int original=openat(dir,"input",O_RDWR|O_CREAT|O_EXCL,0600); CHECK(original>=0);
 CHECK(write(original,"ORIGINAL",8)==8);
 void *mapping=mmap(NULL,8,PROT_READ|PROT_WRITE,MAP_SHARED,original,0); CHECK(mapping!=MAP_FAILED);
 struct stat before; CHECK(fstat(original,&before)==0);
 int pipefd[2]; CHECK(pipe(pipefd)==0);
 pid_t child=fork(); CHECK(child>=0);
 if(child==0) {
   close(pipefd[0]); close(original); CHECK(munmap(mapping,8)==0);
   CHECK(ptrace(PTRACE_TRACEME,0,NULL,NULL)==0); raise(SIGSTOP);
   int fd=(int)syscall(SYS_openat,dir,"input",O_RDONLY,0); CHECK(fd>=0);
   char bytes[8];
   if(mapmode) {
     volatile unsigned char *m=mmap(NULL,8,PROT_READ,MAP_SHARED,fd,0); CHECK(m!=MAP_FAILED);
     for(int i=0;i<8;i++) bytes[i]=(char)m[i];
   } else { CHECK(read(fd,bytes,8)==8); }
   CHECK(write(pipefd[1],bytes,8)==8);
   _exit(0);
 }
 close(pipefd[1]); int status; CHECK(waitpid(child,&status,0)==child && WIFSTOPPED(status));
 CHECK(ptrace(PTRACE_SETOPTIONS,child,0,PTRACE_O_TRACESYSGOOD|PTRACE_O_EXITKILL)==0);
 int entering=1,seen=0,identity=0; char bound[9]={0},pathbytes[9]={0};
 for(;;) {
   CHECK(ptrace(PTRACE_SYSCALL,child,0,0)==0);
   CHECK(waitpid(child,&status,0)==child);
   if(WIFEXITED(status)) { CHECK(WEXITSTATUS(status)==0); break; }
   CHECK(WIFSTOPPED(status) && WSTOPSIG(status)==(SIGTRAP|0x80));
   struct user_regs_struct regs; CHECK(ptrace(PTRACE_GETREGS,child,0,&regs)==0);
   if(!entering && regs.orig_rax==(unsigned long)(mapmode?SYS_mmap:SYS_openat) && (long)regs.rax>=0) {
     CHECK(!seen); seen=1;
     char proc[128]; snprintf(proc,sizeof(proc),"/proc/%ld/fd/%ld",(long)child,(long)(mapmode?regs.r8:regs.rax));
     int held=open(proc,O_RDONLY|O_CLOEXEC); CHECK(held>=0);
     struct stat st; CHECK(fstat(held,&st)==0);
     identity=before.st_dev==st.st_dev && before.st_ino==st.st_ino; CHECK(identity);
     CHECK(pread(held,bound,8,0)==8);
     CHECK(renameat(dir,"input",dir,"renamed-original")==0);
     int replacement=openat(dir,"input",O_RDWR|O_CREAT|O_EXCL,0600); CHECK(replacement>=0);
     CHECK(write(replacement,"PATHFAKE",8)==8); CHECK(pread(replacement,pathbytes,8,0)==8); close(replacement);
     struct stat after; CHECK(fstat(held,&after)==0);
     CHECK(after.st_dev==st.st_dev && after.st_ino==st.st_ino);
     /* A separate process owns a writable MAP_SHARED alias. It executes no
        read/write syscall on the traced fd. Tracee remains at the selected syscall-exit stop. */
     pid_t writer=fork(); CHECK(writer>=0);
     if(writer==0) { memcpy(mapping,"MUTATED!",8); _exit(0); }
     int ws; CHECK(waitpid(writer,&ws,0)==writer && WIFEXITED(ws) && WEXITSTATUS(ws)==0);
     char changed[8]; CHECK(pread(held,changed,8,0)==8 && !memcmp(changed,"MUTATED!",8));
     close(held);
   }
   entering=!entering;
 }
 CHECK(seen); char consumed[9]={0}; CHECK(read(pipefd[0],consumed,8)==8);
 printf("{\"bound_fd_bytes\":\"%s\",\"replacement_path_bytes\":\"%s\",\"consumed_bytes\":\"%s\",\"fd_identity_preserved_after_rename\":true,\"consumer_stopped_during_writer\":true,\"mechanism\":\"PTRACE_SYSCALL openat/mmap exit; proc FD identity; external MAP_SHARED writer\",\"consumption\":\"%s\"}\n",bound,pathbytes,consumed,mapmode?"userspace-load-after-mmap-exit":"read-syscall");
 CHECK(munmap(mapping,8)==0); close(original); close(dir); close(pipefd[0]);
 return 0;
}
