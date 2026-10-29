/* Synthetic stage fixture, never firmware. */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <sys/stat.h>
int main(int argc, char **argv) {
 if(argc != 2) return 1;
 if(!strcmp(argv[1], "symlink")) return symlink("/snapshot/probe","/output/value.h")!=0;
 if(!strcmp(argv[1], "fifo")) return mkfifo("/output/value.h",0600)!=0;
 if(!strcmp(argv[1], "hardlink")) {
   FILE *f=fopen("/output/value.h","w"); if(!f) return 18; fclose(f);
   return link("/output/value.h","/output/alias")!=0;
 }
 if(!strcmp(argv[1], "header")) {
   FILE *f=fopen("/output/value.h","w"); if(!f) return 17;
   fputs("#define VALUE 7\n",f); fclose(f); return 0;
 }
 if(!strcmp(argv[1], "extra")) {
   FILE *f=fopen("/output/unmodeled","w"); if(!f) return 15; fclose(f);
   f=fopen("/output/value.h","w"); if(!f) return 16; fclose(f); return 0;
 }
 if(!strcmp(argv[1], "produce")) {
   int fd=open("/output/value.h",O_WRONLY|O_CREAT|O_EXCL,0600);
   if(fd<0 || write(fd,"GENERATION-ONE",14)!=14) return 2;
   close(fd); return 0;
 }
 if(!strcmp(argv[1], "consume")) {
   char b[32]={0}; int fd=open("/generation/value.h",O_RDONLY);
   if(fd<0 || read(fd,b,32)!=14 || strcmp(b,"GENERATION-ONE")) return 3;
   int dir=open("/generation",O_RDONLY|O_DIRECTORY); if(dir<0) return 19;
   int fd2=openat(dir,"value.h",O_RDONLY); if(fd2<0) return 20;
   int fd3=dup(fd2); if(fd3<0) return 21;
   void *m=mmap(NULL,14,PROT_READ,MAP_SHARED,fd3,0);
   if(m==MAP_FAILED || memcmp(m,"GENERATION-ONE",14)) return 22;
   void *w=mmap(NULL,14,PROT_READ|PROT_WRITE,MAP_SHARED,fd3,0);
   if(w!=MAP_FAILED) return 23;
   if(renameat(dir,"value.h",dir,"replacement")==0) return 24;
   if(ftruncate(fd3,0)==0) return 25;
   if(memcmp(m,"GENERATION-ONE",14)) return 26;
   munmap(m,14); close(fd3); close(fd2); close(dir); close(fd);
   fd=open("/generation/value.h",O_WRONLY|O_TRUNC);
   if(fd>=0) return 4;
   FILE *f=fopen("/output/result","w"); if(!f) return 5;
   fputs(b,f); fclose(f); return 0;
 }
 if(!strcmp(argv[1], "daemon")) {
   int fd=open("/output/value.h",O_RDWR|O_CREAT|O_EXCL,0600);
   if(fd<0 || write(fd,"DAEMON-READY",12)!=12) return 7;
   void *m=mmap(NULL,12,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
   if(m==MAP_FAILED) return 8;
   int sync[2]; if(pipe(sync)) return 9;
   pid_t child=fork(); if(child<0) return 10;
   if(child==0) {
     close(sync[0]); if(setsid()<0) _exit(11);
     /* Keep stdout/stderr and writable shared mapping live forever. A mere
        process-group kill would miss this new session. PID teardown must kill
        us before broker EOF/output promotion. No sleeps or timing assumptions. */
     if(write(sync[1],"x",1)!=1) _exit(12);
     for(;;) pause();
   }
   close(sync[1]); char ready; if(read(sync[0],&ready,1)!=1) return 13;
   return 0;
 }
 return 6;
}
