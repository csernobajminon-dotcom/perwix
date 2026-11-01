#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "kernel/storage/storage.h"
#include "kernel/fs/fat32.h"
#include "kernel/fs/vfs.h"

static uint8_t disk_image[512][512];
static storage_partition_info_t part = { .first_lba=1, .sectors=200, .mbr_type=0x0b, .bootable=false, .type_name="FAT32", .filesystem="FAT32" };
uint32_t storage_disk_count(void) { return 1; }
uint32_t storage_get_partition_count(uint32_t d) { return d==0 ? 1 : 0; }
bool storage_get_partition(uint32_t d,uint32_t p,storage_partition_info_t *out) { if(d||p||!out)return false; *out=part; return true; }
bool storage_read_sectors(uint32_t d,uint64_t lba,uint8_t n,void *buf) { if(d||!n||lba+n>512)return false; memcpy(buf,&disk_image[lba][0],(size_t)n*512); return true; }

static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static uint8_t checksum(const uint8_t *n){uint8_t s=0;for(int i=0;i<11;i++)s=(uint8_t)(((s&1)?0x80:0)+(s>>1)+n[i]);return s;}
int main(void){
 uint8_t *b=disk_image[1]; put16(b+11,512);b[13]=1;put16(b+14,1);b[16]=1;put16(b+17,0);put16(b+19,200);put16(b+22,0);put32(b+32,200);put32(b+36,2);put32(b+44,2);put16(b+510,0xaa55);
 uint8_t *fat=disk_image[2];put32(fat+0,0x0ffffff8);put32(fat+4,0x0fffffff);put32(fat+8,0x0fffffff);put32(fat+12,0x0fffffff);
 uint8_t *root=disk_image[4];uint8_t sfn[11]={'P','R','I','V','I','T',' ',' ','T','X','T'};uint8_t *lfn=root;lfn[0]=0x41;lfn[11]=0x0f;lfn[13]=checksum(sfn);lfn[26]=0;lfn[27]=0;
 uint16_t name[]={0x041f,0x0440,0x0438,0x0432,0x0456,0x0442,'.','t','x','t',0,0xffff,0xffff};
 const uint8_t pos[]={1,3,5,7,9,14,16,18,20,22,24,28,30};for(int i=0;i<13;i++)put16(lfn+pos[i],name[i]);
 uint8_t *de=root+32;memcpy(de,sfn,11);de[11]=0x20;put16(de+20,0);put16(de+26,3);const char *txt="UTF-8 file: Привіт, Україно!\n";put32(de+28,(uint32_t)strlen(txt));
 memcpy(disk_image[5],txt,strlen(txt));
 if(!fat32_init()||!vfs_is_mounted()){puts("mount failed");return 1;}
 vfs_dirent_t entries[8];uint32_t count=0;if(!vfs_list("/",entries,8,&count)||count!=1){printf("list failed count=%u\n",count);return 2;}
 if(strcmp(entries[0].name,"Привіт.txt")!=0){printf("wrong name: %s\n",entries[0].name);return 3;}
 char out[128]={0};uint32_t got=0,size=0;if(!vfs_read("/Привіт.txt",0,out,sizeof(out)-1,&got,&size)){puts("read failed");return 4;}out[got]=0;
 if(size!=strlen(txt)||got!=strlen(txt)||strcmp(out,txt)!=0){printf("wrong data size=%u got=%u: %s\n",size,got,out);return 5;}
 printf("FAT32 smoke test OK: %s (%u bytes)\n",entries[0].name,got);return 0;
}
