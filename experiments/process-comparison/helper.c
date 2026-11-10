// SPDX-License-Identifier: GPL-3.0-or-later
/* One operation per child. Internal cpmfs API experiment, not production ABI. */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cpmfs.h"
const char cmd[]="cpmimage_helper";
static void quoted(const char *s) {
  putchar('"'); for(const unsigned char *p=(const unsigned char*)s;*p;p++) {
    if(*p=='"'||*p=='\\') {putchar('\\');putchar(*p);}
    else if(*p<32 || *p>=127) printf("\\u%04x",*p); else putchar(*p);
  } putchar('"');
}
static int fail(const char *code,const char *message) {
  printf("{\"v\":1,\"ok\":false,\"error\":");quoted(code);
  printf(",\"message\":");quoted(message?message:"backend error");puts("}");return 2;
}
int main(int argc,char **argv) {
  struct cpmSuperBlock sb={0};struct cpmInode root,ino; const char *err;
  if(argc<5) return fail("arguments","expected operation, image, format, container");
  const char *op=argv[1];int rename=!strcmp(op,"rename");
#ifdef CPMIMAGE_READ_ONLY
  if(rename) return fail("read_only","this helper supports listing and export only");
#endif
  if(strcmp(op,"list") && strcmp(op,"export") && !rename) return fail("operation","unsupported operation");
  if((!strcmp(op,"list") && argc!=5) || ((!strcmp(op,"export")||rename) && argc!=7)) return fail("arguments","wrong argument count");
  if((err=Device_open(&sb.dev,argv[2],rename?O_RDWR:O_RDONLY,argv[4]))) return fail("open",err);
  if(cpmReadSuper(&sb,&root,argv[3],0)<0) {Device_close(&sb.dev);return fail("filesystem",boo);}
  if(!strcmp(op,"list")) {
    struct cpmFile dir;struct cpmDirent ent; struct cpmStatFS stats;
    struct {char name[16];long long size,atime,mtime,ctime;int attr;} entries[4096];int count=0,n;
    if(cpmOpendir(&root,&dir)<0) {cpmUmount(&sb);return fail("list",boo);}
    while((n=cpmReaddir(&dir,&ent))>0) {
      if(ent.name[0]=='.')continue;
      if(count==4096||cpmNamei(&root,ent.name,&ino)<0) {cpmUmount(&sb);return fail("list","entry or limit error");}
      snprintf(entries[count].name,sizeof entries[count].name,"%s",ent.name);
      entries[count].size=ino.size;entries[count].attr=ino.attr;entries[count].atime=ino.atime;entries[count].mtime=ino.mtime;entries[count].ctime=ino.ctime;count++;
    }
    cpmStatFS(&root,&stats);
    if(n<0||cpmUmount(&sb)<0)return fail("list","read or close failed");
    printf("{\"v\":1,\"ok\":true,\"entries\":[");
    for(int i=0;i<count;i++) {if(i)putchar(',');printf("{\"name\":");quoted(entries[i].name);printf(",\"size\":%lld,\"attr\":%d,\"atime\":%lld,\"mtime\":%lld,\"ctime\":%lld}",entries[i].size,entries[i].attr,entries[i].atime,entries[i].mtime,entries[i].ctime);}
    printf("],\"stats\":{\"blocks\":%ld,\"free\":%ld,\"blocksize\":%ld}}\n",stats.f_blocks,stats.f_bfree,stats.f_bsize);return 0;
  }
  if(rename) {
    if(cpmRename(&root,argv[5],argv[6])<0) {cpmUmount(&sb);return fail("rename",boo);}
  } else {
    struct cpmFile file;char buf[4096];ssize_t n;FILE *out;
    if(cpmNamei(&root,argv[5],&ino)<0) {cpmUmount(&sb);return fail("not_found",boo);}
    if(cpmOpen(&ino,&file,O_RDONLY)<0) {cpmUmount(&sb);return fail("export",boo);}
    if(!(out=fopen(argv[6],"wb"))) {cpmClose(&file);cpmUmount(&sb);return fail("export","cannot create staging output");}
    int bad=0;while((n=cpmRead(&file,buf,sizeof buf))>0)if(fwrite(buf,1,n,out)!=(size_t)n){bad=1;break;}
    if(n<0)bad=1;if(fclose(out))bad=1;if(cpmClose(&file)<0)bad=1;
    if(bad) {cpmUmount(&sb);return fail("export","read/write/close failed");}
  }
  if(cpmUmount(&sb)<0)return fail("sync",boo);
  puts("{\"v\":1,\"ok\":true}");return 0;
}
