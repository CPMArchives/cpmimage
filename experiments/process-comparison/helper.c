// SPDX-License-Identifier: GPL-3.0-or-later
/* One operation per child. Internal cpmfs API experiment, not production ABI. */
#include "config.h"
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "cpmfs.h"
#include "cpmdir.h"
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

#define CPMIMAGE_FORMAT_HELPER
#include "mkfs.cpm.c"

static int initialize(const char *path, const char *format, const char *label, int timestamps)
{
  struct cpmSuperBlock drive={0};
  struct cpmInode root;
  if (!strcmp(format,"amstrad")) return fail("unsupported_backend","Formatting requires a standalone disk definition");
  if (cpmReadSuper(&drive,&root,format,0)<0) return fail("invalid_definition",boo);
  if ((timestamps || *label) && !(drive.type & CPMFS_CPM3_OTHER))
    return fail("unsupported_backend","Labels and timestamps require a supported CP/M Plus definition");
  size_t reserved=(size_t)(drive.bootsec>=0 ? drive.bootsec : drive.boottrk*drive.sectrk)*drive.secLength;
  char *boot=malloc(reserved ? reserved : 1);
  FILE *input=fopen(path,"rb");
  if (!input || !boot || fseek(input,(long)drive.offset,SEEK_SET) || fread(boot,1,reserved,input)!=reserved)
  {
    if (input) fclose(input);
    free(boot);
    return fail("image_incompatible","Cannot read reserved system bytes");
  }
  fclose(input);
  /* mkfs initializes the upstream CP/M Plus records. The helper reader
   * suppresses high user areas on CP/M Plus, so restore the formatter flags. */
  if (drive.type & CPMFS_CPM3_OTHER) drive.type=CPMFS_DR3;
  int result=mkfs(&drive,path,format,*label?label:"unlabeled",boot,timestamps,0);
  free(boot);
  if (result<0) return fail("format_failed",boo);
  puts("{\"v\":1,\"ok\":true}");
  return 0;
}

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
  printf(",\"cpm_plus\":%s",(sb.type & CPMFS_CPM3_OTHER)?"true":"false");
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

/* The broker supplies one physical address for each consecutive RAW slot.
 * This routine only transfers bytes. cpmtools applies filesystem skew. */
static int mapped_transfer(const char *driver, const char *map_path,
                           const char *raw_path, const char *image_path, int writing)
{
  FILE *map=fopen(map_path,"r");
  FILE *raw=fopen(raw_path,writing?"rb":"wb");
  DSK_PDRIVER disk=NULL;
  dsk_err_t error=DSK_ERR_OK;
  int bad=!map || !raw;
  if (!bad)
    error=writing ? dsk_creat(&disk,image_path,driver,NULL) : dsk_open(&disk,image_path,driver,NULL);
  unsigned cylinder, head, mode, size, count;
  unsigned char bytes[8192];
  int fields=EOF;
  unsigned tracks=0;
  while (!bad && !error && (fields=fscanf(map,"%u %u %u %u %u",
                                          &cylinder,&head,&mode,&size,&count))==5)
  {
    DSK_FORMAT sectors[255];
    DSK_GEOMETRY geometry={0};
    if (cylinder>255 || head>1 || mode>5 || size<128 || size>sizeof bytes ||
        !count || count>255 || ++tracks>512) { bad=1; break; }
    geometry.dg_cylinders=256;
    geometry.dg_heads=2;
    geometry.dg_sectors=count;
    geometry.dg_secsize=size;
    geometry.dg_datarate=mode%3==0?RATE_HD : mode%3==1?RATE_DD : RATE_SD;
    geometry.dg_fm=mode<3?RECMODE_FM:RECMODE_MFM;
    for (unsigned i=0;i<count;++i)
    {
      unsigned id;
      if (fscanf(map,"%u",&id)!=1 || id>255) { bad=1; break; }
      sectors[i]=(DSK_FORMAT){cylinder,head,id,size};
    }
    if (bad) break;
    if (writing) error=dsk_pformat(disk,&geometry,cylinder,head,sectors,0xe5);
    for (unsigned i=0;i<count && !error;++i)
    {
      if (writing)
      {
        if (fread(bytes,1,size,raw)!=size) { bad=1; break; }
        error=dsk_pwrite(disk,&geometry,bytes,cylinder,head,sectors[i].fmt_sector);
      }
      else
      {
        int deleted=0;
        error=dsk_xread(disk,&geometry,bytes,cylinder,head,cylinder,head,
                        sectors[i].fmt_sector,size,&deleted);
        if (!error && (deleted || fwrite(bytes,1,size,raw)!=size)) { bad=1; break; }
      }
    }
  }
  if (!error && (!tracks || fields!=EOF || (writing && fgetc(raw)!=EOF))) bad=1;
  if (map) fclose(map);
  if (raw && fclose(raw)) bad=1;
  if (disk)
  {
    dsk_err_t closed=dsk_close(&disk);
    if (!error) error=closed;
  }
  if (bad || error) return fail("image_incompatible",error?dsk_strerror(error):"Invalid mapped sector transfer");
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
#ifdef CPMIMAGE_TELEDISK
extern dsk_err_t cpmimage_tele_scan(DSK_PDRIVER, FILE *);

static int teledisk_records(const char *source, const char *destination)
{
  DSK_PDRIVER disk = NULL;
  dsk_err_t error = dsk_open(&disk, source, "tele", NULL);
  if (error) return fail("teledisk", dsk_strerror(error));
  FILE *out = fopen(destination, "wb");
  if (!out) error = DSK_ERR_SYSERR;
  else
  {
    error = cpmimage_tele_scan(disk, out);
    if (fclose(out) && !error) error = DSK_ERR_SYSERR;
  }
  dsk_err_t closed = dsk_close(&disk);
  if (!error) error = closed;
  if (error)
  {
    if (out) remove(destination);
    return fail("teledisk", dsk_strerror(error));
  }
  puts("{\"v\":1,\"ok\":true}");
  return 0;
}
#endif

static int structure_number(const char *text, int maximum)
{
  if (*text < '0' || *text > '9') return -1;
  char *end;
  errno = 0;
  long value = strtol(text, &end, 10);
  return !errno && !*end && value <= maximum ? (int)value : -1;
}

static const char *slot_kind(const struct cpmSuperBlock *sb, unsigned char status)
{
  if (status == 0xe5) return "unused";
  if (status <= (sb->type & CPMFS_HI_USER ? 31 : 15)) return "file";
  if ((sb->type & CPMFS_CPM3_OTHER) && status >= 16 && status <= 31) return "password";
  if ((sb->type & CPMFS_CPM3_OTHER) && status == 0x20) return "label";
  if ((sb->type & CPMFS_CPM3_DATES) && status == 0x21) return "timestamp";
  return "unknown";
}

static int allocation_pointer(const struct PhysDirectoryEntry *entry, int index, int width)
{
  const unsigned char *pointer = (const unsigned char *)entry->pointers + index * width;
  return width == 2 ? pointer[0] | (pointer[1] << 8) : pointer[0];
}

static void structure_slot(const struct cpmSuperBlock *sb, int slot, int width, const char *kind, int extent_index)
{
  const struct PhysDirectoryEntry *entry = sb->dir + slot;
  unsigned char status = entry->status;
  printf("{\"slot\":%d,\"status\":%u,\"kind\":", slot, (unsigned)status);
  quoted(kind);
  if (status <= (sb->type & CPMFS_HI_USER ? 31 : 15))
  {
    char name[9], extension[4];
    int n = 8, e = 3, attrs = 0;
    for (int i = 0; i < 8; ++i) name[i] = entry->name[i] & 0x7f;
    for (int i = 0; i < 3; ++i) extension[i] = entry->ext[i] & 0x7f;
    while (n > 0 && name[n-1] == ' ') --n;
    while (e > 0 && extension[e-1] == ' ') --e;
    name[n] = extension[e] = '\0';
    const int name_flags[] = {CPM_ATTR_F1, CPM_ATTR_F2, CPM_ATTR_F3, CPM_ATTR_F4};
    const int extension_flags[] = {CPM_ATTR_RO, CPM_ATTR_SYS, CPM_ATTR_ARCV};
    for (int i = 0; i < 4; ++i)
      if (entry->name[i] & 0x80) attrs |= name_flags[i];
    for (int i = 0; i < 3; ++i)
      if (entry->ext[i] & 0x80) attrs |= extension_flags[i];
    const int extent = EXTENT(entry->extnol, entry->extnoh);
    printf(",\"extent_index\":%d,\"user\":%u,\"name\":", extent_index, (unsigned)status);
    quoted(name);
    fputs(",\"extension\":", stdout);
    quoted(extension);
    printf(",\"attributes\":%d,\"extent\":%d,\"physical_extent\":%d,\"record_count\":%u,\"pointers\":[",
           attrs, extent, extent / sb->extents, (unsigned)(unsigned char)entry->blkcnt);
    for (int index = 0; index < 16 / width; ++index)
    {
      if (index) putchar(',');
      const int block = allocation_pointer(entry, index, width);
      /* Zero is the empty pointer sentinel, not a reference to directory block 0. */
      if (block) printf("%d", block);
      else fputs("null", stdout);
    }
    putchar(']');
  }
  putchar('}');
}

static int filesystem_structure(struct cpmSuperBlock *sb, const char *view, int start, int limit, int block)
{
  if (!strcmp(view, "sector_slots"))
  {
    if (block >= sb->size || sb->offset)
    {
      Device_close(&sb->dev);
      return fail("unknown_block", "The block has no supported RAW sector address.");
    }
    const int count = sb->blksiz / sb->secLength;
    const long long reserved = sb->bootsec >= 0 ? sb->bootsec : (long long)sb->boottrk * sb->sectrk;
    const long long first = reserved + (long long)block * count;
    printf("{\"v\":1,\"ok\":true,\"slots\":[");
    for (int i = 0; i < count; ++i)
    {
      const long long sector = first + i;
      printf("%s[%lld,%d]", i ? "," : "", sector / sb->sectrk,
             sb->skewtab[sector % sb->sectrk]);
    }
    puts("]}");
    Device_close(&sb->dev);
    return 0;
  }
  const int blocks = !strcmp(view, "blocks");
  const int references = !strcmp(view, "references");
  const int extents = !strcmp(view, "extents");
  const int width = sb->size > 256 ? 2 : 1;
  if (references && block >= sb->size)
  {
    Device_close(&sb->dev);
    return fail("unknown_block", "Allocation block is outside the filesystem.");
  }
  if (sb->extents <= 0)
  {
    Device_close(&sb->dev);
    return fail("image_incompatible", "Invalid logical extent factor.");
  }

  printf("{\"v\":1,\"ok\":true,\"directory_slots\":%d,\"block_count\":%d,\"block_size\":%d,"
         "\"directory_blocks\":%d,\"pointer_bytes\":%d,\"extent_factor\":%d,\"rows\":[",
         sb->maxdir, sb->size, sb->blksiz, sb->dirblks, width, sb->extents);
  int total = 0, emitted = 0;
  if (blocks)
  {
    const int bits = CHAR_BIT * sizeof(*sb->alv);
    total = sb->size;
    for (int number = start; number < total && emitted < limit; ++number)
    {
      const int used = ((unsigned)sb->alv[number / bits] & (1u << (number % bits))) != 0;
      if (emitted++) putchar(',');
      printf("{\"block\":%d,\"state\":", number);
      quoted(number < sb->dirblks ? "directory" : used ? "allocated" : "free");
      putchar('}');
    }
  }
  else
  {
    int extent_index = -1;
    for (int slot = 0; slot < sb->maxdir; ++slot)
    {
      const struct PhysDirectoryEntry *entry = sb->dir + slot;
      const int file = (unsigned char)entry->status <= (sb->type & CPMFS_HI_USER ? 31 : 15);
      if (file) ++extent_index;
      if (references)
      {
        if (!file || block == 0) continue;
        for (int index = 0; index < 16 / width; ++index)
        {
          if (allocation_pointer(entry, index, width) != block) continue;
          if (total++ < start || emitted >= limit) continue;
          if (emitted++) putchar(',');
          printf("{\"slot\":%d,\"pointer\":%d}", slot, index);
        }
      }
      else
      {
        if (extents && !file) continue;
        if (total++ < start || emitted >= limit) continue;
        if (emitted++) putchar(',');
        structure_slot(sb, slot, width, slot_kind(sb, (unsigned char)entry->status), extent_index);
      }
    }
  }
  /* cpmUmount calls cpmSync. Close without syncing in this one-operation child. */
  if (Device_close(&sb->dev)) return 2;
  printf("],\"total\":%d}\n", total);
  return 0;
}

#ifdef CPMIMAGE_READ_ONLY
extern int cpmImageReadDirectory(struct cpmSuperBlock *, const char *, const char **);
extern int cpmImageBlockPresent(const struct cpmSuperBlock *, int);

enum { MAX_CHECK_FINDINGS = 256 };

struct CheckExtent
{
  unsigned char key[12];
  int group, slot;
};

struct CheckOwner
{
  int slot, pointer;
};

static int compare_check_extents(const void *left, const void *right)
{
  const struct CheckExtent *a = left, *b = right;
  int file = memcmp(a->key, b->key, sizeof a->key);
  if (file) return file;
  if (a->group != b->group) return a->group - b->group;
  return a->slot - b->slot;
}

static void check_finding(unsigned *total, const char *code, int slot, int observed,
                          const char *expected, int block, int pointer, int related, int relatedPointer)
{
  if ((*total)++ >= MAX_CHECK_FINDINGS) return;
  if (*total > 1) putchar(',');
  fputs("{\"code\":", stdout);
  quoted(code);
  printf(",\"slot\":%d,\"observed\":%d,\"expected\":", slot, observed);
  quoted(expected);
  if (block >= 0) printf(",\"block\":%d", block);
  if (pointer >= 0) printf(",\"pointer\":%d", pointer);
  if (related >= 0) printf(",\"related_slot\":%d", related);
  if (relatedPointer >= 0) printf(",\"related_pointer\":%d", relatedPointer);
  putchar('}');
}

static void check_field(unsigned *total, const char *code, int slot, int observed, const char *expected)
{
  check_finding(total, code, slot, observed, expected, -1, -1, -1, -1);
}

static int incomplete_check(const char *phase, const char *message)
{
  fputs("{\"v\":1,\"ok\":true,\"status\":\"incomplete\",\"phase\":", stdout);
  quoted(phase);
  fputs(",\"message\":", stdout);
  quoted(message ? message : "Filesystem data is unavailable.");
  puts(",\"findings\":[],\"finding_count\":0,\"omitted\":0}");
  return 0;
}

static int filesystem_check(const char *path, const char *format)
{
  struct cpmSuperBlock sb = {0};
  const char *phase = "definition";
  const char *error = Device_open(&sb.dev, path, O_RDONLY, "cpmimage-raw");
  if (error) return incomplete_check("open", error);
  if (cpmImageReadDirectory(&sb, format, &phase) < 0)
  {
    /* Return the directory read error before closing the device. */
    int result = incomplete_check(phase, boo);
    Device_close(&sb.dev);
    return result;
  }
  if (sb.extents <= 0)
  {
    Device_close(&sb.dev);
    return incomplete_check("geometry", "Invalid logical extent factor.");
  }
  struct CheckOwner *owners = calloc((size_t)sb.size, sizeof *owners);
  struct CheckExtent *extents = malloc((size_t)sb.maxdir * sizeof *extents);
  if (!owners || !extents)
  {
    free(owners);
    free(extents);
    Device_close(&sb.dev);
    return fail("memory", "Could not allocate filesystem check state.");
  }

  const int width = sb.size > 256 ? 2 : 1;
  const int maxUser = sb.type & CPMFS_HI_USER ? 31 : 15;
  unsigned total = 0;
  int extentCount = 0, labelSlot = -1;
  printf("{\"v\":1,\"ok\":true,\"directory_slots\":%d,\"block_count\":%d,\"findings\":[",
         sb.maxdir, sb.size);
  for (int slot = 0; slot < sb.maxdir; ++slot)
  {
    const struct PhysDirectoryEntry *entry = sb.dir + slot;
    const unsigned char status = entry->status;
    if (status == 0xe5) continue;
    if (status > maxUser)
    {
      const char *kind = slot_kind(&sb, status);
      if (!strcmp(kind, "unknown"))
        check_field(&total, "invalid_status", slot, status, "File user or directory status supported by this variant");
      else if (!strcmp(kind, "timestamp") && slot % 4 != 3)
        check_field(&total, "misplaced_timestamp", slot, slot % 4, "Timestamp in the fourth slot of its directory group");
      else if (!strcmp(kind, "label"))
      {
        if (labelSlot >= 0)
          check_finding(&total, "duplicate_label", slot, status, "One disk label", -1, -1, labelSlot, -1);
        else labelSlot = slot;
      }
      continue;
    }

    struct CheckExtent *extent = extents + extentCount++;
    extent->key[0] = status;
    extent->group = EXTENT(entry->extnol, entry->extnoh) / sb.extents;
    extent->slot = slot;
    for (int i = 0; i < 11; ++i)
    {
      const unsigned char value = (i < 8 ? entry->name[i] : entry->ext[i-8]) & 0x7f;
      extent->key[i+1] = (unsigned char)toupper(value);
      if (!ISFILECHAR(i != 0, value))
        check_field(&total, "invalid_name_character", slot, value, "Valid CP/M filename character");
    }
    const unsigned char low = entry->extnol, high = entry->extnoh;
    if (low > 31)
      check_field(&total, "invalid_extent_low", slot, low, "Low extent byte in the range 0 to 31");
    if (high > 63)
      check_field(&total, "invalid_extent_high", slot, high, "High extent byte in the range 0 to 63");
    if ((unsigned char)entry->blkcnt > 128)
      check_field(&total, "invalid_record_count", slot, (unsigned char)entry->blkcnt, "Record count in the range 0 to 128");
    if ((unsigned char)entry->lrc > 128)
      check_field(&total, "invalid_last_record_count", slot, (unsigned char)entry->lrc, "Last-record byte count in the range 0 to 128");

    for (int pointer = 0; pointer < 16 / width; ++pointer)
    {
      const int block = allocation_pointer(entry, pointer, width);
      if (!block) continue;
      if (block < sb.dirblks || block >= sb.size)
      {
        check_finding(&total, block < sb.dirblks ? "reserved_block" : "block_out_of_range",
                      slot, block, "Block in the filesystem data area", block, pointer, -1, -1);
        continue;
      }
      struct CheckOwner *owner = owners + block;
      if (owner->slot)
        check_finding(&total, "shared_block", slot, block, "One file pointer per data block",
                      block, pointer, owner->slot - 1, owner->pointer);
      else
      {
        owner->slot = slot + 1;
        owner->pointer = pointer;
        if (!cpmImageBlockPresent(&sb, block))
          check_finding(&total, "missing_allocated_sectors", slot, block, "All sectors of an allocated block present",
                        block, pointer, -1, -1);
      }
    }
  }

  qsort(extents, (size_t)extentCount, sizeof *extents, compare_check_extents);
  const struct CheckExtent *first = extents;
  for (int i = 1; i < extentCount; ++i)
  {
    const struct CheckExtent *entry = extents + i;
    if (entry->group == first->group && !memcmp(entry->key, first->key, sizeof entry->key))
      check_finding(&total, "duplicate_extent", entry->slot, entry->group,
                    "One physical extent per file and extent group", -1, -1, first->slot, -1);
    else first = entry;
  }
  free(owners);
  free(extents);
  /* cpmUmount syncs filesystems. This child only reads and closes its device. */
  if (Device_close(&sb.dev)) return 2;
  printf("],\"status\":\"%s\",\"phase\":\"complete\",\"finding_count\":%u,\"omitted\":%u}\n",
         total ? "findings" : "clean", total, total > MAX_CHECK_FINDINGS ? total - MAX_CHECK_FINDINGS : 0);
  return 0;
}
#endif

int main(int argc,char **argv) {
  struct cpmSuperBlock sb={0};struct cpmInode root,ino; const char *err;
#ifdef CPMIMAGE_READ_ONLY
#ifdef CPMIMAGE_TELEDISK
  if (argc==4 && !strcmp(argv[1],"teledisk-records"))
    return teledisk_records(argv[2], argv[3]);
#endif
  if (argc==6 && (!strcmp(argv[1],"mapped-read") || !strcmp(argv[1],"mapped-write")) &&
      (!strcmp(argv[2],"imd") || !strcmp(argv[2],"jv3")))
    return mapped_transfer(argv[2],argv[3],argv[4],argv[5],!strcmp(argv[1],"mapped-write"));
  if (argc==6 && !strcmp(argv[1],"initialize"))
    return initialize(argv[2],argv[3],argv[4],!strcmp(argv[5],"1"));
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
  if (argc==5 && !strcmp(argv[1],"filesystem-check") && !strcmp(argv[4],"raw"))
    return filesystem_check(argv[2], argv[3]);
  if (argc==4 && !strcmp(argv[1],"encode")) return encode(argv[2],argv[3]);
  if (argc==4 && !strcmp(argv[1],"decode")) return decode(argv[2],argv[3]);
#endif
  if(argc<5) return fail("arguments","expected operation, image, format, container");
  const char *op=argv[1];int rename=!strcmp(op,"rename");
  int editing=!strncmp(op,"edit_",5) && argc==7 && !strcmp(argv[4],"raw");
#ifdef CPMIMAGE_READ_ONLY
  if(rename) return fail("read_only","this helper supports listing and export only");
#endif
  const int preview = !strcmp(op,"preview");
  const int structures = !strcmp(op, "filesystem-structure");
  int start = 0, limit = 0, block = 0;
  if (structures)
  {
    if (argc != 9 || strcmp(argv[4], "raw") ||
        (strcmp(argv[5], "directory") && strcmp(argv[5], "extents") &&
         strcmp(argv[5], "blocks") && strcmp(argv[5], "references") && strcmp(argv[5], "sector_slots")))
      return fail("arguments", "Expected a RAW image and a filesystem structure view.");
    start = structure_number(argv[6], INT_MAX);
    limit = structure_number(argv[7], 128);
    block = structure_number(argv[8], INT_MAX);
    if (start < 0 || limit < 1 || block < 0) return fail("arguments", "Invalid filesystem structure page.");
  }
  if(strcmp(op,"list") && strcmp(op,"export") && !preview && !rename && !editing && !structures) return fail("operation","unsupported operation");
  if((!strcmp(op,"list") && argc!=5) || ((!strcmp(op,"export")||preview||rename) && argc!=7)) return fail("arguments","wrong argument count");
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
  if (structures) return filesystem_structure(&sb, argv[5], start, limit, block);
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
    const size_t preview_limit = 1024 * 1024;
    size_t remaining = (size_t)ino.size;
    if (preview && remaining > preview_limit) remaining = preview_limit;
    int bad = 0;
    n = 0;
    while (remaining && (n = cpmRead(&file, buf,
           remaining < sizeof buf ? remaining : sizeof buf)) > 0) {
      if (fwrite(buf, 1, n, out) != (size_t)n) { bad = 1; break; }
      remaining -= n;
    }
    if(n<0 || remaining)bad=1;if(fclose(out))bad=1;if(cpmClose(&file)<0)bad=1;
    if(bad) {cpmUmount(&sb);return fail("export","read/write/close failed");}
  }
  if(cpmUmount(&sb)<0)return fail("sync",boo);
  if (preview) printf("{\"v\":1,\"ok\":true,\"truncated\":%s}\n",
                      ino.size > 1024 * 1024 ? "true" : "false");
  else puts("{\"v\":1,\"ok\":true}");
  return 0;
}
