// SPDX-License-Identifier: GPL-3.0-or-later
/* One operation per child. Internal cpmfs API experiment, not production ABI. */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
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
#ifdef CPMIMAGE_READ_ONLY
extern int cpmImageReadDefinition(struct cpmSuperBlock *, const char *);

static int describe(const char *format)
{
  struct cpmSuperBlock sb={0};
  if (!strcmp(format,"amstrad"))
  {
    fputs("{\"v\":1,\"ok\":true,\"boot_geometry\":true}",stdout);
    return 0;
  }
  if (cpmImageReadDefinition(&sb,format)<0) return fail("invalid_definition","Incomplete definition");
  printf("{\"v\":1,\"ok\":true,\"sector_size\":%d,\"tracks\":%d,\"sectors_per_track\":%d,"
      "\"offset\":%lld,\"block_size\":%d,\"directory_entries\":%d,\"directory_blocks\":%d,"
      "\"boot_sectors\":%d,\"extent_factor\":%d,\"skew\":%d,\"os_flags\":%d,\"libdsk_format\":",
      sb.secLength,sb.tracks,sb.sectrk,(long long)sb.offset,sb.blksiz,sb.maxdir,sb.dirblks,
      sb.bootsec>=0?sb.bootsec:sb.boottrk*sb.sectrk,sb.extents,sb.skew,sb.type);
  quoted(sb.libdskGeometry);
  printf(",\"skew_table\":[");
  if (sb.skewtab)
    for (int i=0;i<sb.sectrk;++i) printf("%s%d",i?",":"",sb.skewtab[i]);
  fputs("]}",stdout);
  free(sb.skewtab);
  return 0;
}

/* Decode the already qualified standard, single-sided DSK sector map.
 * The broker checks header order and flags before asking LibDsk to read it. */
static int decode(const char *image, const char *destination)
{
  DSK_PDRIVER disk=NULL;
  DSK_GEOMETRY geometry;
  dsk_err_t error=dg_stdformat(&geometry,FMT_AMPRO200,NULL,NULL);
  if (!error) error=dsk_open(&disk,image,"dsk",NULL);
  if (error) return fail("unsupported_container",dsk_strerror(error));
  FILE *output=fopen(destination,"wb");
  if (!output) { dsk_close(&disk); return fail("open","Cannot stage RAW image"); }
  unsigned char sector[512];
  int failed=0;
  for (unsigned n=0;n<400;++n)
  {
    error=dsk_lread(disk,&geometry,sector,n);
    if (error || fwrite(sector,1,sizeof sector,output)!=sizeof sector) { failed=1; break; }
  }
  if (fclose(output)) failed=1;
  dsk_close(&disk);
  if (failed) return fail("image_incompatible",error?dsk_strerror(error):"Cannot stage RAW image");
  puts("{\"v\":1,\"ok\":true}");
  return 0;
}
static int encode(const char *image, const char *destination)
{
  FILE *input=fopen(image,"rb");
  if (!input) return fail("open","Cannot read staged RAW image");
  DSK_GEOMETRY geometry;
  DSK_PDRIVER disk=NULL;
  dsk_err_t error=dg_stdformat(&geometry,FMT_AMPRO200,NULL,NULL);
  if (!error) error=dsk_creat(&disk,destination,"dsk",NULL);
  unsigned char bytes[512];
  int bad=0;
  for (unsigned track=0;track<40 && !error && !bad;++track)
  {
    DSK_FORMAT sectors[10];
    for (unsigned sector=0;sector<10;++sector)
      sectors[sector]=(DSK_FORMAT){track,0,sector+1,512};
    error=dsk_pformat(disk,&geometry,track,0,sectors,0xe5);
    for (unsigned sector=0;sector<10 && !error;++sector)
    {
      if (fread(bytes,1,sizeof bytes,input)!=sizeof bytes) { bad=1; break; }
      error=dsk_pwrite(disk,&geometry,bytes,track,0,sector+1);
    }
  }
  if (!bad && (fgetc(input)!=EOF || ferror(input))) bad=1;
  fclose(input);
  if (disk)
  {
    dsk_err_t closed=dsk_close(&disk);
    if (!error) error=closed;
  }
  if (error || bad) return fail("image_incompatible",error?dsk_strerror(error):"RAW size differs from standard DSK geometry");
  puts("{\"v\":1,\"ok\":true}");
  return 0;
}

#endif
static int edit(struct cpmSuperBlock *sb, struct cpmInode *root, const char *op,
                const char *name, const char *argument)
{
  struct cpmInode ino;
  int result=-1;
  if (!strcmp(op,"edit_rename")) result=cpmRename(root,name,argument);
  else if (!strcmp(op,"edit_delete")) result=cpmUnlink(root,name);
  else if (!strcmp(op,"edit_attrs"))
  {
    if (cpmNamei(root,name,&ino)==0)
    {
      cpm_attr_t attributes;
      if (cpmAttrGet(&ino,&attributes)==0)
        result=cpmAttrSet(&ino,(attributes & ~0x700) | (atoi(argument) & 0x700));
    }
  }
  else if (!strcmp(op,"edit_import"))
  {
    FILE *input=fopen(argument,"rb");
    if (!input) { cpmUmount(sb); return fail("import","Cannot open staged input"); }
    struct cpmFile file;
    if (cpmCreat(root,name,&ino,0666)==0 && cpmOpen(&ino,&file,O_WRONLY)==0)
    {
      unsigned char bytes[4096];
      size_t count;
      result=0;
      while ((count=fread(bytes,1,sizeof bytes,input))>0)
        if (cpmWrite(&file,(const char *)bytes,count)!=(ssize_t)count) { result=-1; break; }
      if (ferror(input) || cpmClose(&file)<0) result=-1;
    }
    if (result==0)
    {
      struct stat info;
      if (stat(argument,&info)==0)
      {
        struct utimbuf times;
        times.actime=info.st_atime;
        times.modtime=info.st_mtime;
        cpmUtime(&ino,&times);
      }
    }
    fclose(input);
  }
  if (cpmUmount(sb)<0 || result<0) return fail("edit_failed",boo);
  puts("{\"v\":1,\"ok\":true}");
  return 0;
}
int main(int argc,char **argv) {
  struct cpmSuperBlock sb={0};struct cpmInode root,ino; const char *err;
#ifdef CPMIMAGE_READ_ONLY
  if (argc==3 && !strcmp(argv[1],"describe")) return describe(argv[2]);
  if (argc>=3 && !strcmp(argv[1],"describe-catalogue"))
  {
    fputs("{\"v\":1,\"ok\":true,\"definitions\":[",stdout);
    for (int i=2;i<argc;++i)
    {
      if (i>2) putchar(',');
      if (describe(argv[i])) return 2;
    }
    puts("]}");
    return 0;
  }
  if (argc==4 && !strcmp(argv[1],"encode")) return encode(argv[2],argv[3]);
  if (argc==4 && !strcmp(argv[1],"decode")) return decode(argv[2],argv[3]);
#endif
  if(argc<5) return fail("arguments","expected operation, image, format, container");
  const char *op=argv[1];int rename=!strcmp(op,"rename");
  int editing=!strncmp(op,"edit_",5) && argc==7 && !strcmp(argv[4],"raw");
#ifdef CPMIMAGE_READ_ONLY
  if(rename) return fail("read_only","this helper supports listing and export only");
#endif
  if(strcmp(op,"list") && strcmp(op,"export") && !rename && !editing) return fail("operation","unsupported operation");
  if((!strcmp(op,"list") && argc!=5) || ((!strcmp(op,"export")||rename) && argc!=7)) return fail("arguments","wrong argument count");
  const char *device=argv[4];
#ifdef CPMIMAGE_READ_ONLY
  if (!strcmp(device,"raw")) device="cpmimage-raw";
#endif
  if((err=Device_open(&sb.dev,argv[2],(rename||editing)?O_RDWR:O_RDONLY,device))) return fail("open",err);
  if(cpmReadSuper(&sb,&root,argv[3],0)<0)
  {
    Device_close(&sb.dev);
    return fail(boo && !strncmp(boo,"Missing ",8) ? "image_truncated" : "image_incompatible",boo);
  }
  if(editing) return edit(&sb,&root,op,argv[5],argv[6]);
  if(!strcmp(op,"list")) {
    struct cpmFile dir;struct cpmDirent ent; struct cpmStatFS stats;
    struct {char name[16];long long size,atime,mtime,ctime;int attr;} entries[4096];int count=0,n;
    if(cpmOpendir(&root,&dir)<0) {cpmUmount(&sb);return fail("list",boo);}
    while((n=cpmReaddir(&dir,&ent))>0) {
      if(ent.name[0]=='.' || ent.name[0]=='[')continue;
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
