/*
Filesystem Lab disigned and implemented by Liang Junkai,RUC
*/
#include <stdint.h>
#include <stdio.h>
#include<stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fuse.h>
#include <errno.h>
#include "disk.h"

#ifndef BLOCK_SIZE
#define BLOCK_SIZE 4096
#define BLOCK_NUM 65536
#endif

#define DATA_BLOCK_NUM 64000
#define MAXPATHLEN 10000
#define MAXNAME 26
#define SUPER_BASE 0
#define IM_BASE 1
#define DM_BASE 2
#define INODE_BASE 4
#define DATA_BASE 644
#define UNUSE_BASE 64644
#define INODE_PER_BLOCK 64
#define BIT_PER_BLOCK (8*4096)
#define PTR_PER_BLOCK 2048
#define DIR_PER_BLOCK 128
#define DIRECT_PTR 11
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define DIRMODE ((S_IFDIR)|(0755))
#define REGMODE ((S_IFREG)|(0644))
int ERRORNO;

// size of inode is 64
struct inode {
  mode_t mode;//4+4
  off_t size;//8
  time_t atime;//8
  time_t mtime;//8
  time_t ctime;//8
  unsigned short single_ptr;//2
  unsigned short direct_ptr[DIRECT_PTR];//22
};

struct directory_entry {
  char filename[26];
  unsigned short inodeno;
  //unsigned short pading1;
  int padding;
};

int mymin(int a,int b){
  if(a<=b)
    return a;
  return b;
}
int mymax(int a,int b){
  if(a>=b)
    return a;
  return b;
}
int p_inodeno2c_inodeno(const char *childname, int parent_inodeno);
void map_operation(int op, int base, int no);
int path2inodeno(const char *path);
struct inode path2inode(const char *path);
struct inode inodeno2inode(int inodeno);

// op = clear 0 malloc 1
// base =IM_BASE DM_BASE
// no is the no of bit to operate
void map_operation(int op, int base, int no) {
  int map_index = base + no / BIT_PER_BLOCK;
  int map_offset = no % BIT_PER_BLOCK;
  int byte_index = map_offset / 8;
  int byte_offset=map_offset%8;
  char buf[BLOCK_SIZE + 1];
  memset(buf, 0, BLOCK_SIZE);
  disk_read(map_index, buf);
  if(op)
    buf[byte_index] |= 1 << byte_offset;
  else{
    buf[byte_index] &= ~(1 << byte_offset);
    if(base==DM_BASE){
      char buf1[BLOCK_SIZE+1];
      memset(buf1,-1,BLOCK_SIZE);
      disk_write(DATA_BASE+no,buf1);
    }
  }
  disk_write(map_index,buf);
}

//input DM_BASE or IM_BASE
// return number of free datablock or number of free inode block 
int map_count(int base){
  char buf[BLOCK_SIZE + 1];
  int cnt = 0;
  int i,j;
  if (base == IM_BASE)
  {
    memset(buf, 0, BLOCK_SIZE);
    disk_read(IM_BASE, buf);
    for (i = 0; i < BLOCK_SIZE;i++)
      for (j = 0; j < 8;j++)
        cnt += (buf[i] >> j) & 1;
    cnt = BIT_PER_BLOCK - cnt;
    return cnt;    
  }
  int cnt1=0,cnt2=0;
  memset(buf, 0, BLOCK_SIZE);
  disk_read(DM_BASE, buf);
  for (i = 0; i < BLOCK_SIZE;i++)
    for (j = 0; j < 8;j++)
      cnt1 += (buf[i] >> j) & 1;
  memset(buf, 0, BLOCK_SIZE);
  disk_read(DM_BASE + 1, buf);
  for (i = 0; i < BLOCK_SIZE;i++)
    for (j = 0; j < 8;j++)
      cnt2 += (buf[i] >> j) & 1;
  cnt = BIT_PER_BLOCK-cnt1+BIT_PER_BLOCK-cnt2;
  return cnt;
}

//find in inode map return the no
//don't change the inode map
int find_free_inode(){
  char buf[BLOCK_SIZE+1];
  int i, j = 0;
  int freeno=0;
  memset(buf, 0, BLOCK_SIZE);
  disk_read(IM_BASE,buf);
  for (i = 0; i < BLOCK_SIZE;i++){
    if(freeno)
      break;
    if(buf[i]==(char)0xff)
      continue;
    for (j = 0; j < 8; j++)
      if (!(buf[i] & (1 << j))) {
        freeno=i*8+j;
        break;
      }
  }
  if(freeno==0)
    return -1;
  memset(buf,0,BLOCK_SIZE);
  int inode_index=INODE_BASE+freeno/INODE_PER_BLOCK;
  int inode_offset=freeno%INODE_PER_BLOCK;
  disk_read(inode_index,buf);
  struct inode* inode_ptr=(struct inode*)buf;
  for(i=0;i<DIRECT_PTR;i++)
    (inode_ptr+inode_offset)->direct_ptr[i]=(unsigned short)-1;
  (inode_ptr+inode_offset)->single_ptr=(unsigned short)-1;
  disk_write(inode_index,buf);
  //printf("direct ptr is:");
  //for(i=0;i<4;i++)
    //printf("%d  ",(inode_ptr+inode_offset)->direct_ptr[i]);
  //printf("\n");
  //printf("free inode no is %d\n",freeno);
  int ans=freeno;
  return ans;
}


// freelist stores the the free data block no
// need=append block+1
// do not change the map 
int find_free_block(int need,int* freelist){
  if(need<=0){
    printf("need can't less than zero when finding free blocks\n");
  }
  char buf[BLOCK_SIZE + 1];
  int i,j=0,k=0;
  memset(buf, 0, BLOCK_SIZE);
  if(disk_read(DM_BASE, buf)){
    printf("disk_read error when find free block\n");
    return -1;
  }
  for (i = 0; i < BLOCK_SIZE;i++){
    if(buf[i]==(char)0xff)
      continue;
    if (need<=0)
      break;
    for (j = 0; j < 8; j++)
      if (!(buf[i] & (1 << j))) {
        freelist[k] = 8 * i + j;
        k++;
        need--;
      }
  }
  //printf("here is ok 1.5\n");

  memset(buf, 0, BLOCK_SIZE);
  if(disk_read(DM_BASE + 1, buf)){
    printf("disk_read error when find free block\n");
    return -1;
  }
  for (i = 0; i < BLOCK_SIZE; i++) {
    if (buf[i] == (char)0xff)
      continue;
    if (need<=0)
      break;
    if (8 * i + BIT_PER_BLOCK > DATA_BLOCK_NUM) {
      printf("not enough free blocks\n");
      return -1;
    }
    for (j = 0; j < 8; j++)
      if (!(buf[i] & (1 << j))) {
        freelist[k] = 8 * i + j + BIT_PER_BLOCK;
        k++;
        need--;
      }
  }
  for(i=0;i<k;i++){
    char buf[BLOCK_SIZE+1];
    memset(buf,-1,BLOCK_SIZE);
    disk_write(DATA_BASE+freelist[i],buf);
  }
   //printf("here is ok 1.75\n");
}



//a read-only copy. 
struct inode inodeno2inode(int inodeno) {
  int inode_index = inodeno / INODE_PER_BLOCK + INODE_BASE;
  int inode_offset = inodeno % INODE_PER_BLOCK;
  char buf[BLOCK_SIZE + 1];
  memset(buf, 0, BLOCK_SIZE);
  if(disk_read(inode_index, buf)){
    printf("disk_read error when inodeno2inode,error inode no is %d\n",inodeno);
  }
  struct inode *inode_ptr = (struct inode *)buf;
  struct inode ans=*(inode_ptr+inode_offset);
  return ans;
}
struct inode path2inode(const char * path){
  int inodeno = path2inodeno(path);
  return inodeno2inode(inodeno);
}
int path2inodeno(const char *path) {
  int pathlen = strlen(path);
  char mypath[MAXPATHLEN];
  char filename[24];
  int i,j,k;
  strncpy(mypath, path, MAXPATHLEN);
  if(pathlen==1 && path[0]=='/'){
    return 0;
  }
  if (path[pathlen - 1] == '/')
    pathlen--;
  mypath[pathlen] = '\0';
  int now_inodeno = 0;//root directory inodeno
  int start_slash = 0;//path must begin with /
  int end_slash = 0;
  for (i = start_slash + 1; i < pathlen;i++){
    if(mypath[i]=='/'){
      end_slash = i;
      for (j = start_slash + 1,k=0; j < end_slash;j++,k++)
        filename[k] = mypath[j];
      filename[k] = '\0';
      now_inodeno = p_inodeno2c_inodeno(filename, now_inodeno);
      if(now_inodeno==-1)
        return -1;
      start_slash=end_slash;
      i = start_slash;
    }
  }
  for (j = start_slash + 1,k=0; j < pathlen;j++,k++)
    filename[k] = mypath[j];
  filename[k] = '\0';
  now_inodeno = p_inodeno2c_inodeno(filename, now_inodeno);
  if(now_inodeno==-1)
    return -1;
  return now_inodeno;
}
void count_ptr(int size,int* ptr_cnt){
  if(size<DIRECT_PTR*BLOCK_SIZE){
    ptr_cnt[0] = size/BLOCK_SIZE+!!(size%BLOCK_SIZE);
    ptr_cnt[1] = 0;
  }
  else if (size < DIRECT_PTR*BLOCK_SIZE + 2048*BLOCK_SIZE){
    ptr_cnt[0] = DIRECT_PTR;
    ptr_cnt[1] = 1;
  }else  {
    printf("the size of a inode is too big when counting\n");
  }
}

int p_inodeno2c_inodeno(const char* childname,int parent_inodeno){
  struct inode parent_inode = inodeno2inode(parent_inodeno);
  int i,j,k;
  int ptr_cnt[3]={0};
  char buf[BLOCK_SIZE + 1];
  char ptr_buf[BLOCK_SIZE + 1];
  memset(buf, 0, BLOCK_SIZE);
  count_ptr(parent_inode.size, ptr_cnt);

  for (i = 0; i < ptr_cnt[0];i++){
    if(parent_inode.direct_ptr[i]==(unsigned short)-1){
      printf("ptr contradict with size\n");
      return -1;
    }
    if(disk_read(parent_inode.direct_ptr[i]+DATA_BASE, buf)){
      printf("disk read error when p_inodeno2c_inodeno\n");
      return -1;
    }
    struct directory_entry *dir_ptr = (struct directory_entry *)buf;
    for (j = 0; j < DIR_PER_BLOCK;j++){
      if((dir_ptr+j)->inodeno==(unsigned short)-1)
        break;
      if(strcmp((dir_ptr+j)->filename,childname)==0)
        return (dir_ptr + j)->inodeno;
    }
  }

  if(ptr_cnt[1]==0){
    printf("child :%s not found in parent directory\n",childname);
    return -1;
  }
  memset(ptr_buf, 0, BLOCK_SIZE);
  if(disk_read(parent_inode.single_ptr+DATA_BASE, ptr_buf)){
    printf("disk read error when p_inodeno2c_inodeno\n");
    return -1;
  }
  unsigned short *pptr = (unsigned short *)ptr_buf;
  for (j = 0; j < PTR_PER_BLOCK;j++){
    if(*(pptr+j)==(unsigned short)-1)
      break;
    memset(buf, 0, BLOCK_SIZE);
    if(disk_read(*(pptr + j)+DATA_BASE, buf)){
      printf("disk_read errpr when p_inodeno2c_inodeno\n");
      return -1;
    }
    struct directory_entry *dir_ptr = (struct directory_entry *)buf;
    for (k = 0; k <DIR_PER_BLOCK;k++){
      if((dir_ptr+k)->inodeno==(unsigned short)-1)
        break;
      if(strcmp((dir_ptr+k)->filename,childname)==0){
        return (dir_ptr + k)->inodeno;
      }
    }
  }

  printf("child not found in parent\n");
  return -1;
}

// add datablock ptr in inode and revise data block map
// won't write the content;
// dosen't change the inode's atime ctime size
void add_ptr(int inodeno,int append_block,int* freelist){
  printf("add ptr begin,append block=%d\n",append_block);
  struct inode _inode=inodeno2inode(inodeno);
  int ptr_cnt[2]={0};
  int i,j;
  int ori_append_block=append_block;
  int tempblock=freelist[append_block];
  if(freelist==NULL || !append_block)
    return;
  count_ptr(_inode.size,ptr_cnt);
  for(i=ptr_cnt[0],j=0;i<DIRECT_PTR;i++){
    if(append_block<=0)
      break;
    _inode.direct_ptr[i]=freelist[j];
    append_block--;
    map_operation(1,DM_BASE,freelist[j]);
    j++;
  }
  if(append_block){
    if(_inode.single_ptr==(unsigned short)-1){
      _inode.single_ptr=tempblock;
      map_operation(1,DM_BASE,tempblock);
      printf("temp block is %d\n",tempblock);
    }
    char ptr_buf[BLOCK_SIZE+1];
    memset(ptr_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+_inode.single_ptr,ptr_buf);
    unsigned short * pptr=(unsigned short*)ptr_buf;
    for(i=0;i<PTR_PER_BLOCK;i++)
      if(*(pptr+i)==(unsigned short)-1)
        break;
    for(;i<PTR_PER_BLOCK;i++){
      if(append_block<=0)
        break;
      if(j==ori_append_block){
        printf("tempblock abused!\n");
      }
      *(pptr+i)=(unsigned short)freelist[j];
      map_operation(1,DM_BASE,freelist[j]);
      j++;
      append_block--;
    }
    disk_write(DATA_BASE+_inode.single_ptr,ptr_buf);
  }
  int inode_index=inodeno/INODE_PER_BLOCK+INODE_BASE;
  int inode_offset=inodeno%INODE_PER_BLOCK;
  char buf[BLOCK_SIZE+1];
  memset(buf, 0, BLOCK_SIZE);
  disk_read(inode_index, buf);
  struct inode *inode_ptr = (struct inode *)buf;
  *(inode_ptr + inode_offset) = _inode;
  disk_write(inode_index,buf);
  printf("add ptr finished\n");
}

void rm_ptr(int inodeno,int delete_block){
  printf("rm ptr begin, delete block=%d\n",delete_block);
  struct inode _inode=inodeno2inode(inodeno);
  int ptr_cnt[2]={0};
  count_ptr(_inode.size,ptr_cnt);
  if(ptr_cnt[1] && _inode.single_ptr!=(unsigned short)-1){
    char ptr_buf[BLOCK_SIZE+1];
    char buf[BLOCK_SIZE+1];
    memset(buf,-1,BLOCK_SIZE);
    memset(ptr_buf,BLOCK_SIZE,0);
    disk_read(DATA_BASE+_inode.single_ptr,ptr_buf);
    unsigned short* pptr=(unsigned short*)ptr_buf;
    int i;
    for(i=0;i<PTR_PER_BLOCK;i++)
      if(*(pptr+i)==(unsigned short)-1)
        break;
    for(i=i-1;i>=0;i--){
      if(delete_block <=0)
        break;
      disk_write(DATA_BASE+*(pptr+i),buf);
      map_operation(0,DM_BASE,*(pptr+i));
      *(pptr+i)==(unsigned short)-1;
      delete_block--;
    }
  }
  if(delete_block){
    int i;
    char buf[BLOCK_SIZE+1];
    memset(buf,-1,BLOCK_SIZE);
    for(i=ptr_cnt[0]-1;i>=0;i--){
      if(delete_block<=0)
        break;
      disk_write(DATA_BASE+_inode.direct_ptr[i],buf);
      map_operation(0,DM_BASE,_inode.direct_ptr[i]);
      _inode.direct_ptr[i]=(unsigned short)-1;
      delete_block--;
    }
  }
  int inode_index=INODE_BASE+inodeno/INODE_PER_BLOCK;
  int inode_offset=inodeno%INODE_PER_BLOCK;
  char buf[BLOCK_SIZE+1];
  memset(buf,0,BLOCK_SIZE);
  disk_read(inode_index,buf);
  struct inode* inode_ptr=(struct inode*)buf;
  *(inode_ptr+inode_offset)=_inode;
  disk_write(inode_index,buf);
  printf("rm ptr finished\n");
}

void rm_name(const char *path) {
  char filename[MAXNAME];
  char parent_path[MAXPATHLEN];
  int flag=0;
  // deal with path;
  if (strlen(path) == 1 && path[0] == '/') {
    printf("root directory can't be rm");
    return;
  }
  int i,j;
  j = strlen(path) - 1;
  if (path[j] == '/')
    j--;
  for (; j >= 0; j--)
    if (path[j] == '/')
      break;
  if (j < 0) {
    printf("can't find / in path when rm name\n");
    return;
  }
  for (i = 0; i < j; i++)
    parent_path[i] = path[i];
  parent_path[i]='\0';
  if(!j){
    parent_path[0]='/';
    parent_path[1]='\0';
  }
  for (j=j+1,i=0; j < strlen(path); i++,j++) {
    if (path[j] == '/')
      break;
    filename[i]=path[j];
  }
  filename[i] = '\0';
  printf("rm name parent path is %s\t,filename is %s\n",parent_path,filename);
  int parent_inodeno=path2inodeno(parent_path);
  struct inode parent_inode = inodeno2inode(parent_inodeno);
  int ptr_cnt[2] = {0};
  count_ptr(parent_inode.size, ptr_cnt);
  struct directory_entry replace;
  
  //the dir only has one entry
  if(parent_inode.size<=sizeof(struct directory_entry)){
    map_operation(0,DM_BASE,parent_inode.direct_ptr[0]);
    char buf[BLOCK_SIZE+1];
    memset(buf,-1,BLOCK_SIZE);
    disk_write(DATA_BASE+parent_inode.direct_ptr[0],buf);
    parent_inode.direct_ptr[0]=(unsigned short)-1;
    parent_inode.ctime=time(NULL);
    parent_inode.mtime=time(NULL);
    parent_inode.size=0;
    int inode_index=INODE_BASE+parent_inodeno/INODE_PER_BLOCK;
    int inode_offset=parent_inodeno%INODE_PER_BLOCK;
    memset(buf,0,BLOCK_SIZE);
    disk_read(inode_index,buf);
    struct inode* inode_ptr=(struct inode*)buf;
    *(inode_ptr+inode_offset)=parent_inode;
    disk_write(inode_index,buf);
    return;
  }
  //find last directory entry to replace
  
  if(parent_inode.single_ptr!=(unsigned short)-1 && ptr_cnt[1]){
    char ptr_buf[BLOCK_SIZE+1];
    memset(ptr_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+parent_inode.single_ptr,ptr_buf);
    unsigned short * pptr=(unsigned short*)ptr_buf;
    for(i=1;i<PTR_PER_BLOCK;i++)
      if(*(pptr+i)==(unsigned short)-1)
        break;
    i--; // if a block has single ptr,it must have a block in single ptr
    char dir_buf[BLOCK_SIZE+1];
    memset(dir_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+*(pptr+i),dir_buf);
    struct directory_entry* dir_ptr=(struct directory_entry*)dir_buf;
    for(j=0;j<DIR_PER_BLOCK;j++)
      if((dir_ptr+j)->inodeno==(unsigned short)-1)
        break;
    j--;//the first in a block can't be empty
    replace=*(dir_ptr+j);
    if(strcmp(replace.filename,filename)==0)
      flag=1;
    (dir_ptr+j)->inodeno=(unsigned short)-1;
    memset(dir_ptr+j,-1,MAXNAME);
    disk_write(DATA_BASE+*(pptr+i),dir_buf);
    if(!j){
      map_operation(0,DM_BASE,*(pptr+i));
      *(pptr+i)=(unsigned short)-1;
      if(!i){
        parent_inode.single_ptr=(unsigned short)-1;
        map_operation(0,DM_BASE,parent_inode.single_ptr);
        ptr_cnt[1]=0;
      }
      disk_write(DATA_BASE+parent_inode.single_ptr,ptr_buf);
    }
  }
  else{
    char dir_buf[BLOCK_SIZE+1];
    memset(dir_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+parent_inode.direct_ptr[ptr_cnt[0]-1],dir_buf);
    struct directory_entry* dir_ptr=(struct directory_entry*)dir_buf;
    for(i=0;i<DIR_PER_BLOCK;i++)
      if((dir_ptr+i)->inodeno==(unsigned short)-1)
        break;
    i--;
    replace=*(dir_ptr+i);
    (dir_ptr+i)->inodeno=(unsigned short)-1;
    if(strcmp(replace.filename,filename)==0)
      flag=1;
    disk_write(DATA_BASE+parent_inode.direct_ptr[ptr_cnt[0]-1],dir_buf);
    if(!i){
      map_operation(0,DM_BASE,parent_inode.direct_ptr[ptr_cnt[0]-1]);
      ptr_cnt[0]--;
    }
  }
  
  //find the one need to delete
  for (i = 0; i < ptr_cnt[0]; i++) {
    if (flag)
      break;
    char buf[BLOCK_SIZE+1];
    memset(buf, 0, BLOCK_SIZE);
    if (disk_read(parent_inode.direct_ptr[i]+DATA_BASE, buf)) {
      printf("disk read error when rm name\n");
    }
    struct directory_entry *dir_ptr;
    dir_ptr = (struct directory_entry *)buf;
    for (j = 0; j < DIR_PER_BLOCK; j++) {
      if ((dir_ptr + j)->inodeno == (unsigned short)-1)
        break;
      if (strcmp((dir_ptr + j)->filename, filename) == 0) {
        if((dir_ptr+j)->inodeno==replace.inodeno){
          (dir_ptr+j)->inodeno==(unsigned short)-1;
          memset((dir_ptr+j)->filename,-1,MAXNAME);
        }else
          *(dir_ptr+j)=replace;
        disk_write(parent_inode.direct_ptr[i]+DATA_BASE, buf);
        flag=1;
        break;
      }
    }
    
  }
  if(ptr_cnt[1] &&parent_inode.single_ptr!=(unsigned short)-1 && !flag){
    char ptr_buf[BLOCK_SIZE+1];
    memset(ptr_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+parent_inode.single_ptr,ptr_buf);
    unsigned short* pptr=(unsigned short*)ptr_buf;
    for(i=0;i<PTR_PER_BLOCK;i++){
      if(*(pptr+i)==(unsigned short)-1)
        break;
      if(flag)
        break;
      char buf[BLOCK_SIZE+1];
      memset(buf,0,BLOCK_SIZE);
      disk_read(DATA_BASE+*(pptr+i),buf);
      struct directory_entry* dir_ptr=(struct directory_entry*)buf;
      for(j=0;j<DIR_PER_BLOCK;j++){
        if((dir_ptr+j)->inodeno==(unsigned short)-1)
          break;
        if(flag)
          break;
        if(strcmp((dir_ptr+j)->filename,filename)==0){
          if((dir_ptr+j)->inodeno==replace.inodeno){
            (dir_ptr+j)->inodeno==(unsigned short)-1;
            memset((dir_ptr+j)->filename,-1,MAXNAME);
          }else
            *(dir_ptr+j)=replace;
          flag=1;
          disk_write(DATA_BASE+*(pptr+i),buf);
        }
      }
    }
  }
  if(!flag && parent_inode.single_ptr==(unsigned short)-1){
    printf("can't find name in parent dir when delete\n");
    return;
  }

  //change the parent inode and write back
  
  parent_inode.ctime=time(NULL);
  parent_inode.mtime=time(NULL);
  parent_inode.size-=sizeof(struct directory_entry);
  int inode_index=INODE_BASE+parent_inodeno/INODE_PER_BLOCK;
  int inode_offset=parent_inodeno%INODE_PER_BLOCK;
  char buf[BLOCK_SIZE+1];
  disk_read(inode_index,buf);
  struct inode* inode_ptr=(struct inode*)buf;
  *(inode_ptr+inode_offset)=parent_inode;
  disk_write(inode_index,buf);
}

//Format the virtual block device in the following function
int mkfs() {
  //printf("begin mkfs\n");
  //printf("sizeof inode is %d\n", sizeof(struct inode));
  char buf[BLOCK_SIZE + 1];
  memset(buf,0,BLOCK_SIZE);
  if (buf == NULL) {
    printf("initial failed\n");
    return(-1);
  }
  struct statvfs * super_info=(struct statvfs*)buf;
  super_info->f_bavail = 64000;
  super_info->f_bfree = 64000;
  super_info->f_ffree = 640;
  super_info->f_favail = 640;
  super_info->f_namemax = 24;
  super_info->f_files=0;
  super_info->f_bsize = BLOCK_SIZE;
  super_info->f_blocks = BLOCK_NUM;
  disk_write(0, buf);
  
  int freelist[3];
  find_free_block(2, freelist);
  memset(buf, 0, BLOCK_SIZE);
  int i;
  for (i = IM_BASE; i < INODE_BASE; i++)
    disk_write(i, buf);
  memset(buf, -1, BLOCK_SIZE);
  for (i = INODE_BASE; i < UNUSE_BASE; i++)
    disk_write(i, buf);

  struct inode *inodeptr = (struct inode*)buf;
  inodeptr->atime = time(NULL);
  inodeptr->ctime = time(NULL);
  inodeptr->mtime = time(NULL);
  inodeptr->size = 0;
  inodeptr->mode =DIRMODE;
  inodeptr->direct_ptr[0]=(unsigned short)-1;/*freelist[0];*///?????
  disk_write(INODE_BASE, buf); 
  map_operation(1, IM_BASE, 0);

  /*
  memset(buf, 0, BLOCK_SIZE);
  disk_read(DATA_BASE + freelist[0], buf);
  struct directory_entry *dir_ptr = (struct directory_entry *)buf;
  strncpy((dir_ptr)->filename,"testfile1",MAXNAME);
  dir_ptr->inodeno = find_free_inode();
  int testfile_inodeno = dir_ptr->inodeno;
  printf("test file inodeno %d\n", testfile_inodeno);
  map_operation(1, IM_BASE, dir_ptr->inodeno);

  strncpy((dir_ptr + 1)->filename, "testdir1", MAXNAME);
  (dir_ptr + 1)->inodeno = find_free_inode();
  int testdir_inodeno = (dir_ptr + 1)->inodeno;
  printf("testdir_inodeno %d\n", testdir_inodeno);
  map_operation(1, IM_BASE, (dir_ptr + 1)->inodeno);
  disk_write(DATA_BASE + freelist[0], buf);
  */
  map_operation(1, DM_BASE, freelist[0]);

  /*
  int inode_index = testfile_inodeno / INODE_PER_BLOCK + INODE_BASE;
  int inode_offset = testfile_inodeno % INODE_PER_BLOCK;
  memset(buf,0,BLOCK_SIZE);
  disk_read(inode_index,buf);
  inodeptr = (struct inode *)buf;
  (inodeptr+inode_offset)->atime = time(NULL);
  (inodeptr+inode_offset)->ctime = time(NULL);
  (inodeptr+inode_offset)->mtime = time(NULL);
  (inodeptr+inode_offset)->mode =REGMODE;
  (inodeptr+inode_offset)->size = (off_t)10;
  (inodeptr+inode_offset)->direct_ptr[0]=freelist[1];
  char testbuf[BLOCK_SIZE+1];
  memset(testbuf,0,BLOCK_SIZE);
  for(i=0;i<5;i++)
    testbuf[i]='a';
  for(;i<10;i++)
    testbuf[i]='b';
  testbuf[i]='\0';
  if(disk_write(inode_index,buf)){
    printf("test inode write error");
  }
  disk_write(DATA_BASE+freelist[1],testbuf);
  map_operation(1, DM_BASE, freelist[1]);
  */
  
  /*
  inode_index = testdir_inodeno / INODE_PER_BLOCK + INODE_BASE;
  inode_offset = testdir_inodeno % INODE_PER_BLOCK;
  memset(buf,0,BLOCK_SIZE);
  disk_read(inode_index,buf);
  inodeptr = (struct inode *)buf;
  (inodeptr+inode_offset)->atime = time(NULL);
  (inodeptr+inode_offset)->ctime = time(NULL);
  (inodeptr+inode_offset)->mtime = time(NULL);
  (inodeptr+inode_offset)->mode =DIRMODE;
  (inodeptr+inode_offset)->size = 0;
  disk_write(inode_index, buf);
  */

  //printf("finish initialize\n");
  return 0;
}


//Filesystem operations that you need to implement
int fs_getattr (const char *path, struct stat *attr)
{
  printf("fs_getattr begin %s\n", path);
  int inodeno = path2inodeno(path);
  if(inodeno==-1){
    printf("get attr not found\n");
    return -ENOENT;
  }
  struct inode info = inodeno2inode(inodeno);
  printf("get attr inodeno %d\n",inodeno);
  printf("direct ptr:  ");
  for(int i=0;i<4;i++)
    printf("%d  ",info.direct_ptr[i]);
  printf("\n");
  attr->st_atime = info.atime;
  attr->st_ctime = info.ctime;
  attr->st_mode = info.mode;
  attr->st_mtime = info.mtime;
  attr->st_nlink = 1;
  attr->st_size = info.size;
  printf("get attr size %d\n",info.size);
  attr->st_gid = getgid();
  attr->st_uid = getuid();
  printf("Getattr is finished:%s\n", path);
  return 0;
}

int fs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
  printf("begin fs_readdir,path=%s\n",path);

  struct inode dir_inode = path2inode(path);
  char parent_path[MAXPATHLEN];
  strncpy(parent_path,path,MAXPATHLEN);
  if(parent_path[strlen(path)-1]=='/')
    parent_path[strlen(path)-1]='\0';
  int ptr_cnt[2]={0};
  count_ptr(dir_inode.size, ptr_cnt);
  char buf[BLOCK_SIZE + 1];
  memset(buf, 0, BLOCK_SIZE);
  int i, j;
  for (i = 0; i < ptr_cnt[0];i++){
    disk_read(dir_inode.direct_ptr[i]+DATA_BASE,buf);
    struct directory_entry *dir_ptr = (struct directory_entry *)buf;
    for (j = 0; j < DIR_PER_BLOCK;j++){
      if((dir_ptr+j)->inodeno==(unsigned short)-1 ||(dir_ptr+j)->inodeno==0)
        break;
      if(strcmp((dir_ptr+j)->filename,"/")==0){
        printf("found root in directory\n");
        return -1;
      }
      char child_path[MAXPATHLEN];
      memset(child_path, 0, MAXPATHLEN);
      strcat(child_path,parent_path);
      strcat(child_path,"/");
      strcat(child_path, (dir_ptr + j)->filename);
      //fs_getattr(childpath ,&attr);
      printf("child path %s / %s\n",parent_path,(dir_ptr+j)->filename);
      printf("child inode no is %d\n ",(dir_ptr+j)->inodeno);
      filler(buffer, (dir_ptr + j)->filename,NULL, 0);
    }
  }
  if(ptr_cnt[1]){
    unsigned short *pptr;
    char ptr_buf[BLOCK_SIZE + 1];
    memset(ptr_buf, 0, BLOCK_SIZE);
    disk_read(dir_inode.single_ptr+DATA_BASE, ptr_buf);
    pptr = (unsigned short *)ptr_buf;
    for (i = 0; i < PTR_PER_BLOCK;i++){
      if(*(pptr+i)==(unsigned short)-1)
        break;
      memset(buf, 0, BLOCK_SIZE);
      disk_read(*(pptr + i)+DATA_BASE, buf);
      struct directory_entry *dir_ptr = (struct directory_entry *)buf;
      for (j = 0; j < DIR_PER_BLOCK;j++){
        if((dir_ptr+j)->inodeno==(unsigned short)-1)
          break;
        struct stat child_stat;
        char child_path[MAXPATHLEN];
        memset(child_path, 0, MAXPATHLEN);
        strcat(child_path, parent_path);
        strcat(child_path,"/");
        strcat(child_path, (dir_ptr + j)->filename);
        if(strlen(child_path)==1 && child_path[0]=='/'){
          printf("root cann't be found as child path\n");
          return -1;
        }
        struct inode child_node = path2inode(child_path);
        child_stat.st_atime = child_node.atime;
        child_stat.st_ctime = child_node.ctime;
        child_stat.st_gid = getgid();
        child_stat.st_mode = child_node.mode;
        child_stat.st_mtime = child_node.mtime;
        child_stat.st_nlink = 1;
        child_stat.st_size = child_node.size;
        filler(buffer, (dir_ptr + j)->filename, &child_stat, 0);
      }
    }
  }

  int inodeno = path2inodeno(path);
  struct inode new_inode = dir_inode;
  new_inode.atime = time(NULL);
  struct inode *inode_ptr;
  int inode_index = INODE_BASE + inodeno / INODE_PER_BLOCK;
  int inode_offset = inodeno % INODE_PER_BLOCK;
  memset(buf, 0, BLOCK_SIZE);
  disk_read(inode_index, buf);
  inode_ptr = (struct inode*)buf;
  *(inode_ptr+inode_offset) = new_inode;
  disk_write(inode_index, buf);

  printf("Readdir is finished:%s\n", path);

  return 0;
}

int fs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
  printf("fs_read is begin path=%s,offset=%d,size=%d\n", path,offset,size);
  int file_inodeno=path2inodeno(path);
  struct inode file_inode=inodeno2inode(file_inodeno);
  if(file_inodeno==-1){
    printf("read file doesn't exist\n");
    return -1;
  }
  if(offset>file_inode.size ){
    printf("offset greater than file size\n");
    return -1;
  }
  int ptr_cnt[2] = {0};
  int i;
  count_ptr(file_inode.size, ptr_cnt);
  if(offset+size>file_inode.size)
    size = file_inode.size-offset;
  int start_block=offset/BLOCK_SIZE;
  int start_offset=offset%BLOCK_SIZE;
  int already=0;
  int left=size;
  for(i=start_block;i<ptr_cnt[0];i++){
    char buf[BLOCK_SIZE+1];
    memset(buf,0,BLOCK_SIZE);
    if(already==size || left<=0)
      break;
    disk_read(DATA_BASE+file_inode.direct_ptr[i],buf);
    if(i==start_block){
      printf("start block is%d,real block no is%d\n",start_block,file_inode.direct_ptr[i]);
      memcpy(buffer,buf+start_offset,mymin(BLOCK_SIZE-start_offset,left));
      //printf("%s\n ", buffer);
      already += mymin(BLOCK_SIZE - start_offset, left);
      left-=mymin(BLOCK_SIZE-start_offset,left);
    }
    if(i!=start_block){
      memcpy(buffer+already,buf,mymin(BLOCK_SIZE,left));
      already+=mymin(BLOCK_SIZE,left);
      left-=mymin(BLOCK_SIZE,left);
    }
  }
  if(ptr_cnt[1] &&file_inode.single_ptr!=(unsigned short)-1 && left>0){
    char ptr_buf[BLOCK_SIZE+1];
    memset(ptr_buf,BLOCK_SIZE,0);
    disk_read(DATA_BASE+file_inode.single_ptr,ptr_buf);
    unsigned short * pptr=(unsigned short *)ptr_buf;
    for(i=mymax(0,start_block-DIRECT_PTR);i<PTR_PER_BLOCK;i++){
      if(*(pptr+i)==(unsigned short)-1)
        break;
      if(already==size || left <=0)
        break;
      char buf[BLOCK_SIZE+1];
      memset(buf,0,BLOCK_SIZE);
      disk_read(DATA_BASE+*(pptr+i),buf);
      if(i==start_block-DIRECT_PTR){
        memcpy(buffer+already,buf+start_offset,mymin(BLOCK_SIZE-start_offset,left));
        already+=mymin(BLOCK_SIZE-start_offset,left);
        left-=mymin(BLOCK_SIZE-start_offset,left);
      }
      if(i!=start_block-DIRECT_PTR){
        memcpy(buffer+already,buf,mymin(BLOCK_SIZE,left));
        already+=mymin(BLOCK_SIZE,left);
        left-=mymin(BLOCK_SIZE,left);
      }
    }
  }

  //deal with inode
  file_inode.atime=time(NULL);
  int inode_index=file_inodeno/INODE_PER_BLOCK+INODE_BASE;
  int inode_offset=file_inodeno%INODE_PER_BLOCK;
  char buf[BLOCK_SIZE+1];
  memset(buf,0,BLOCK_SIZE);
  disk_read(inode_index,buf);
  struct inode* inode_ptr=(struct inode*)buf;
  *(inode_ptr+inode_offset)=file_inode;
  disk_write(inode_index,buf);
  printf("fs_read is finished path=%s,offset=%d,size=%d\n", path,offset,size);
  return size;
}

// not consider the parent dir may need a new block
int mkdir_mknod (const char *path, int is_dir)
{
  char mypath[MAXPATHLEN];
  char filename[MAXNAME];
  char parent_path[MAXPATHLEN];
  strncpy(mypath, path, MAXPATHLEN);
  int pathlen=strlen(path);

  //deal with parent path and childpath;
  if(pathlen==1 && path[0]=='/'){
    printf("can't fs_mknod root\n");
  }
  if (path[pathlen - 1] == '/')
    pathlen--;
  mypath[pathlen] = '\0';
  int i,j;
  for(i=pathlen-1;i>=0;i--)
    if(mypath[i]=='/')
      break;
  if(i<0){
    printf("not found / in path");
  }
  for(j=0;j<i;j++)
    parent_path[j]=mypath[j];
  parent_path[j]='\0';
  if(i==0){
    parent_path[0]='/';
    parent_path[1]='\0';
  }
  for(j=i+1,i=0;j<pathlen;j++,i++)
    filename[i]=mypath[j];
  filename[i]='\0';
  if(strlen(filename)>=MAXNAME)
    filename[MAXNAME]='\0';

  //deal with child inode
  printf("here is ok2\n");
  printf("mknew parent path %s,child file name %s\n",parent_path,filename);
  int child_inodeno=0;
  printf("here is ok 2.01\n");
  child_inodeno=find_free_inode();
  if(child_inodeno==-1)
    return -ENOSPC;
  printf("free inode no is %d\n",child_inodeno);
  struct inode child_inode;
  child_inode.size=0;
  child_inode.atime=child_inode.ctime=child_inode.mtime=time(NULL);
  child_inode.mode = is_dir?DIRMODE:REGMODE;
  for(i=0;i<DIRECT_PTR;i++)
    child_inode.direct_ptr[i]=(unsigned short)-1;
  child_inode.single_ptr=(unsigned short)-1;
  printf("here is ok 2.1\n");
  int inode_index = INODE_BASE + child_inodeno / INODE_PER_BLOCK;
  int inode_offset = child_inodeno % INODE_PER_BLOCK;
  char buf[BLOCK_SIZE + 1];
  memset(buf, 0, BLOCK_SIZE);
  disk_read(inode_index, buf);
  struct inode *inode_ptr=(struct inode*)buf;
  *(inode_ptr + inode_offset) = child_inode;
  disk_write(inode_index, buf);
  map_operation(1, IM_BASE, child_inodeno);
  
  printf("here is ok 2.2\n");
  //deal with parent directory,find the last position to add
  int parent_inodeno=path2inodeno(parent_path);
  struct inode parent_inode=inodeno2inode(parent_inodeno);
  int ptr_cnt[2]={0};
  count_ptr(parent_inode.size,ptr_cnt);
  if(ptr_cnt[1] && parent_inode.single_ptr!=(unsigned short)-1){
    printf("here is ok 2.3\n");
    char ptr_buf[BLOCK_SIZE+1];
    memset(ptr_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+parent_inode.single_ptr,ptr_buf);
    unsigned short * pptr=(unsigned short*)ptr_buf;
    for(i=1;i<PTR_PER_BLOCK;i++)
      if(*(pptr+i)==(unsigned short)-1)
        break;
    i--; // if a block has single ptr,it must have a block in single ptr
    char dir_buf[BLOCK_SIZE+1];
    memset(dir_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+*(pptr+i),dir_buf);
    struct directory_entry* dir_ptr=(struct directory_entry*)dir_buf;
    for(j=0;j<DIR_PER_BLOCK;j++)
      if((dir_ptr+j)->inodeno==(unsigned short)-1)
        break;//the first in a block can't be empty
    if(j==DIR_PER_BLOCK){// add a directory entry need to add a new block in single ptr
      int freelist[2]={0};
      find_free_block(1,freelist);
      i++;
      *(pptr+i)=freelist[0];
      map_operation(1,DM_BASE,freelist[0]);
      char dir_buf[BLOCK_SIZE+1];
      memset(dir_ptr,0,BLOCK_SIZE);
      disk_read(DATA_BASE+freelist[0],dir_buf);
      struct directory_entry* dir_ptr=(struct directory_entry*)dir_buf;
      strncpy(dir_ptr->filename,filename,MAXNAME);
      dir_ptr->inodeno=child_inodeno;
      disk_write(DATA_BASE+freelist[0],dir_buf);
      disk_write(DATA_BASE+parent_inode.single_ptr,ptr_buf);
    }
    if(j<DIR_PER_BLOCK){
      strncpy((dir_ptr+j)->filename,filename,MAXNAME);
      (dir_ptr+j)->inodeno=child_inodeno;
      disk_write(DATA_BASE+*(pptr+i),dir_buf);
    }
  }else if(parent_inode.size==0 || parent_inode.direct_ptr[0]==(unsigned short)-1){
    printf("here is ok 2.4\n");
    int freelist[2]={0};
    find_free_block(1,freelist);
    map_operation(1,DM_BASE,freelist[0]);
    parent_inode.direct_ptr[0]=freelist[0];
    char dir_buf[BLOCK_SIZE+1];
    memset(dir_buf,-1,BLOCK_SIZE);
    struct directory_entry* dir_ptr=(struct directory_entry*)dir_buf;
    dir_ptr->inodeno=child_inodeno;
    strncpy(dir_ptr->filename,filename,MAXNAME);
    disk_write(DATA_BASE+freelist[0],dir_buf);
  }
  else{//the last end in direct ptr part
    char dir_buf[BLOCK_SIZE+1];
    memset(dir_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+parent_inode.direct_ptr[ptr_cnt[0]-1],dir_buf);
    struct directory_entry* dir_ptr=(struct directory_entry*)dir_buf;
    for(i=0;i<DIR_PER_BLOCK;i++)
      if((dir_ptr+i)->inodeno==(unsigned short)-1)
        break;
    printf("here is ok2.5\n");
    if(i==DIR_PER_BLOCK && ptr_cnt[0]<DIRECT_PTR){
      int freelist[2]={0};
      find_free_block(1,freelist);
      parent_inode.direct_ptr[ptr_cnt[0]]=freelist[0];
      map_operation(1,DM_BASE,freelist[0]);
      char buf[BLOCK_SIZE+1];
      memset(buf,0,BLOCK_SIZE);
      disk_read(DATA_BASE+freelist[0],buf);
      struct directory_entry* dir_ptr=(struct directory_entry*)buf;
      strncpy(dir_ptr->filename,filename,MAXNAME);
      dir_ptr->inodeno=child_inodeno;
      disk_write(DATA_BASE+freelist[0],buf);
    }
    if(i==DIR_PER_BLOCK && ptr_cnt[0]==DIRECT_PTR){
      
      int freelist[3]={0};
      find_free_block(2,freelist);
      parent_inode.single_ptr=freelist[0];
      char ptr_buf[BLOCK_SIZE+1];
      memset(ptr_buf,-1,BLOCK_SIZE);
      unsigned short* pptr=(unsigned short*)ptr_buf;
      *(pptr)=(unsigned short)freelist[1];
      printf("here is ok 2.521\n");
      char dir_buf[BLOCK_SIZE+1];
      memset(dir_buf,-1,BLOCK_SIZE);
      struct directory_entry* dir_ptr=(struct directory_entry*)dir_buf;
      strncpy((dir_ptr->filename),filename,MAXNAME);
      (dir_ptr->inodeno)=child_inodeno;
      disk_write(DATA_BASE+freelist[0],ptr_buf);
      disk_write(DATA_BASE+freelist[1],dir_buf);
      map_operation(1,DM_BASE,freelist[0]);
      map_operation(1,DM_BASE,freelist[1]);
      printf("here is ok 2.522\n");
    }
    if(i<DIR_PER_BLOCK){
      printf("here is ok 2.53\n");
      strncpy((dir_ptr+i)->filename,filename,MAXNAME);
      (dir_ptr+i)->inodeno=child_inodeno;
      disk_write(DATA_BASE+parent_inode.direct_ptr[ptr_cnt[0]-1],dir_buf);
      printf("mknew add %s ,inodeno is %d\n",filename,child_inodeno);
    }
  }
  parent_inode.ctime = time(NULL);
  parent_inode.mtime = time(NULL);
  parent_inode.size+=sizeof(struct directory_entry);
  inode_index = parent_inodeno / INODE_PER_BLOCK + INODE_BASE;
  inode_offset = parent_inodeno % INODE_PER_BLOCK;
  memset(buf, 0, BLOCK_SIZE);
  disk_read(inode_index, buf);
  inode_ptr = (struct inode *)buf;
  *(inode_ptr + inode_offset) = parent_inode;
  disk_write(inode_index,buf);
  /*
	*/
}

int fs_mknod(const char *path, mode_t mode, dev_t dev) {
  printf("mknod is begin %s\n", path);
  return mkdir_mknod(path, 0);
}
int fs_mkdir(const char *path, mode_t mode) {
  printf("fs_mkdir is called %s\n", path);
  return mkdir_mknod(path, 1);
}

int fs_rmdir (const char *path)
{
  printf("Rmdir is begin:%s\n",path);
  int inodeno=path2inodeno(path);
  struct inode _inode=inodeno2inode(inodeno);
  rm_name(path);
  int i;
  int ptr_cnt[2]={0};
  count_ptr(_inode.size,ptr_cnt);

  //deal with direct ptr for case
  for(i=0;i<ptr_cnt[0];i++){
    char buf[BLOCK_SIZE+1];
    memset(buf,-1,BLOCK_SIZE);
    disk_write(DATA_BASE+_inode.direct_ptr[i],buf);
    map_operation(0,DM_BASE, _inode.direct_ptr[i]);
  }

  //deal with inode
  int inode_index=inodeno/INODE_PER_BLOCK+INODE_BASE;
  int inode_offset=inodeno%INODE_PER_BLOCK;
  char buf[BLOCK_SIZE+1];
  memset(buf,0,BLOCK_SIZE);
  disk_read(inode_index,buf);
  struct inode* inode_ptr=(struct inode*)buf;
  memset(inode_ptr+inode_offset,-1,sizeof(struct inode));
  disk_write(inode_index,buf);
  map_operation(0,IM_BASE, inodeno);

	printf("Rmdir is finished:%s\n",path);
	return 0;
}

int fs_unlink (const char *path)
{
  printf("unlink is called %s\n",path);
  int inodeno=path2inodeno(path);
  struct inode _inode=inodeno2inode(inodeno);
  rm_name(path);
  int i;
  int ptr_cnt[2]={0};
  count_ptr(_inode.size,ptr_cnt);

  //deal with data block
  for(i=0;i<ptr_cnt[0];i++){
    char buf[BLOCK_SIZE+1];
    memset(buf,-1,BLOCK_SIZE);
    disk_write(DATA_BASE+_inode.direct_ptr[i],buf);
    map_operation(0,DM_BASE, _inode.direct_ptr[i]);
  }
  if(ptr_cnt[1] && _inode.single_ptr!=(unsigned short)-1){
    char buf[BLOCK_SIZE+1];
    memset(buf,-1,BLOCK_SIZE);
    char ptr_buf[BLOCK_SIZE+1];
    memset(ptr_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+_inode.single_ptr,ptr_buf);
    unsigned short* pptr=(unsigned short*)ptr_buf;
    for(i=0;i<PTR_PER_BLOCK;i++){
      if(*(pptr+i)==(unsigned short)-1)
        break;
      disk_write(DATA_BASE+*(pptr+i),buf);
      map_operation(0, DM_BASE, *(pptr+i));
    }
    disk_write(DATA_BASE+_inode.single_ptr,buf);
    map_operation(0, DM_BASE, _inode.single_ptr);
  }

  // deal with inode
  int inode_index=inodeno/INODE_PER_BLOCK+INODE_BASE;
  int inode_offset=inodeno%INODE_PER_BLOCK;
  char buf[BLOCK_SIZE+1];
  memset(buf,0,BLOCK_SIZE);
  disk_read(inode_index,buf);
  struct inode* inode_ptr=(struct inode*)buf;
  memset(inode_ptr+inode_offset,-1,sizeof(struct inode));
  disk_write(inode_index,buf);
  map_operation(0,IM_BASE,inodeno);

	printf("Unlink is finished:%s\n",path);
	return 0;
}

int fs_rename (const char *oldpath, const char *newpath)
{
  printf("begin fs_rename, oldpath=%s, newpath=%s\n",oldpath,newpath);
  char oldparent_path[MAXPATHLEN];
  char newparent_path[MAXPATHLEN];
  char oldchild_name[MAXNAME];
  char newchild_name[MAXNAME];
  int i=strlen(oldpath)-1,j;

  //deal with old path and new path;

  i=strlen(newpath)-1;
  if(newpath[i]=='/')
    i--;
  for(;i>=0;i--)
    if(newpath[i]=='/')
      break;
  if(i<0){
    printf("not found / in newpath\n");
    return -1;
  }
  for(j=0;j<i;j++)
    newparent_path[j]=newpath[j];
  newparent_path[j]='\0';
  if(!i){
    newparent_path[0]='/';
    newparent_path[1]='\0';
  }
  for(j=i+1,i=0;j<strlen(newpath);j++,i++){
    if(newpath[j]=='/')
      break;
    newchild_name[i]=newpath[j];
  }
  newchild_name[i]='\0';
  printf("new child path: %s\nnew child name: %s\n",newparent_path,newchild_name);

  //find if there is a same name in new parent directory
  int child_inodeno=path2inodeno(oldpath);
  printf("here is ok3\n");
  int newparent_inodeno=path2inodeno(newparent_path);
  rm_name(oldpath);
  struct inode newparent_inode=inodeno2inode(newparent_inodeno);
  int samename=0;
  int ptr_cnt[2]={0};
  count_ptr(newparent_inode.size,ptr_cnt);
  for(i=0;i<ptr_cnt[0];i++){
    if(samename)
      break;
    char dir_buf[BLOCK_SIZE+1];
    memset(dir_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+newparent_inode.direct_ptr[i],dir_buf);
    struct directory_entry* dir_ptr=(struct directory_entry*)dir_buf;
    for(j=0;j<DIR_PER_BLOCK;j++){
      if((dir_ptr+j)->inodeno==(unsigned short)-1)
        break;
      if(strcmp((dir_ptr+j)->filename,newchild_name)==0){
        samename=1;
        break;
      }
    }
  }
  if(ptr_cnt[1] && newparent_inode.single_ptr!=(unsigned short)-1 && !samename){
    char ptr_buf[BLOCK_SIZE+1];
    memset(ptr_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+newparent_inode.single_ptr,ptr_buf);
    unsigned short* pptr=(unsigned short*)ptr_buf;
    for(i=0;i<PTR_PER_BLOCK;i++){
      if(*(pptr+i)==(unsigned short)-1)
        break;
      if(samename)
        break;
      char dir_buf[BLOCK_SIZE+1];
      memset(dir_buf,0,BLOCK_SIZE);
      disk_read(DATA_BASE+*(pptr+i),dir_buf);
      struct directory_entry* dir_ptr=(struct directory_entry*)dir_buf;
      for(j=0;j<DIR_PER_BLOCK;j++){
        if((dir_ptr+j)->inodeno==(unsigned short)-1)
          break;
        if(samename)
          break;
        if(strcmp((dir_ptr+j)->filename,newchild_name)==0)
          samename=1;
      } 
    }
  }
  if(samename)
    strcat(newchild_name,"1");

  // add name in new parent directory
  if(ptr_cnt[1] &&  newparent_inode.single_ptr!=(unsigned short)-1){
    char ptr_buf[BLOCK_SIZE+1];
    memset(ptr_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+newparent_inode.single_ptr,ptr_buf);
    unsigned short * pptr=(unsigned short*)ptr_buf;
    for(i=1;i<PTR_PER_BLOCK;i++)
      if(*(pptr+i)==(unsigned short)-1)
        break;
    i--; // if a block has single ptr,it must have a block in single ptr
    char dir_buf[BLOCK_SIZE+1];
    memset(dir_buf,0,BLOCK_SIZE);
    disk_read(DATA_BASE+*(pptr+i),dir_buf);
    struct directory_entry* dir_ptr=(struct directory_entry*)dir_buf;
    for(j=0;j<DIR_PER_BLOCK;j++)
      if((dir_ptr+j)->inodeno==(unsigned short)-1)
        break;//the first in a block can't be empty
    if(j==DIR_PER_BLOCK){// add a directory entry need to add a new block in single ptr
      int freelist[2]={0};
      find_free_block(1,freelist);
      i++;
      *(pptr+i)=freelist[0];
      map_operation(1,DM_BASE,freelist[0]);
      char dir_buf[BLOCK_SIZE+1];
      memset(dir_ptr,0,BLOCK_SIZE);
      disk_read(DATA_BASE+freelist[0],dir_buf);
      struct directory_entry* dir_ptr=(struct directory_entry*)dir_buf;
      strncpy(dir_ptr->filename,newchild_name,MAXNAME);
      dir_ptr->inodeno=child_inodeno;
      disk_write(DATA_BASE+freelist[0],dir_buf);
    }
    if(j<DIR_PER_BLOCK){
      strncpy((dir_ptr+j)->filename,newchild_name,MAXNAME);
      (dir_ptr+j)->inodeno=child_inodeno;
      disk_write(DATA_BASE+*(pptr+i),dir_buf);
    }
  }
  else{
    printf("new parent inode no is %d\n",newparent_inodeno);
    char dir_buf[BLOCK_SIZE+1];
    memset(dir_buf,0,BLOCK_SIZE);
    printf("the direct ptr number is %d\n",ptr_cnt[0]);//need to deal when new parent is empty and need to malloc 
    if(!ptr_cnt[0]){
      int freelist[2]={0};
      find_free_block(1,freelist);
      map_operation(1,DM_BASE,freelist[0]);
      newparent_inode.direct_ptr[0]=freelist[0];
      char buf[BLOCK_SIZE+1];
      memset(buf,0,BLOCK_SIZE);
      struct directory_entry* dir_ptr=(struct directory_entry*)buf;
      strncpy(dir_ptr->filename,newchild_name,MAXNAME);
      (dir_ptr->inodeno)=child_inodeno;
      disk_write(DATA_BASE+freelist[0],buf);
      printf("here is ok 3.51\n");
    }
    else{
      disk_read(DATA_BASE+newparent_inode.direct_ptr[ptr_cnt[0]-1],dir_buf);
      struct directory_entry* dir_ptr=(struct directory_entry*)dir_buf;
      for(i=0;i<DIR_PER_BLOCK;i++)
        if((dir_ptr+i)->inodeno==(unsigned short)-1)
          break;
        else 
          printf("file name %s inode no %d\n",(dir_ptr+i)->filename,(dir_ptr+i)->inodeno);
      if(i==DIR_PER_BLOCK && ptr_cnt[0]<DIRECT_PTR){
        int freelist[2]={0};
        find_free_block(1,freelist);
        newparent_inode.direct_ptr[ptr_cnt[0]]=freelist[0];
        map_operation(1,DM_BASE,freelist[0]);
        char buf[BLOCK_SIZE+1];
        memset(buf,0,BLOCK_SIZE);
        disk_read(DATA_BASE+freelist[0],buf);
        struct directory_entry* dir_ptr=(struct directory_entry*)buf;
        strncpy(dir_ptr->filename,newchild_name,MAXNAME);
        dir_ptr->inodeno=child_inodeno;
        disk_write(DATA_BASE+freelist[0],buf);
      }
      if(i==DIR_PER_BLOCK && ptr_cnt[0]==DIRECT_PTR){
        printf("not deal with\n");
      }
      if(i<DIR_PER_BLOCK){
        printf("the empty appear at %d\n",i);
        printf("here is ok3.5\n");
        strncpy((dir_ptr+i)->filename,newchild_name,MAXNAME);
        (dir_ptr+i)->inodeno=child_inodeno;
        disk_write(DATA_BASE+newparent_inode.direct_ptr[ptr_cnt[0]-1],dir_buf);
      }
    }
  }
  newparent_inode.ctime = time(NULL);
  newparent_inode.mtime = time(NULL);
  newparent_inode.size+=sizeof(struct directory_entry);
  int inode_index = newparent_inodeno / INODE_PER_BLOCK + INODE_BASE;
  int inode_offset = newparent_inodeno % INODE_PER_BLOCK;
  char buf[BLOCK_SIZE+1];
  memset(buf, 0, BLOCK_SIZE);
  disk_read(inode_index, buf);
  struct inode* inode_ptr = (struct inode *)buf;
  *(inode_ptr + inode_offset) = newparent_inode;
  disk_write(inode_index,buf);
  

	printf("Rename is finished:%s\n",oldpath);
	return 0;
}

int fs_write(const char *path, const char *buffer, size_t size, off_t offset,
             struct fuse_file_info *fi) {
  int inodeno = path2inodeno(path);
  if(inodeno==-1){
    printf("write inode missing\n");
    return -1;
  }
  struct inode _inode = inodeno2inode(inodeno);
  if(fi->flags==O_APPEND){
    printf("mode is append\n");
    offset=_inode.size;
  }
  if(fi->flags==O_TRUNC){
    printf("mode is trunc\n");
    offset=0;
  }
  if(offset>_inode.size){
    printf("offset greater than size when writing\n");
    return -1;
  }
   printf("fs write begin path=%s,size=%d,offset=%d\n", path,size,offset);
  if(offset+size>_inode.size){
    int append_block=(size-(!!(offset%BLOCK_SIZE))*(BLOCK_SIZE-offset%BLOCK_SIZE))/BLOCK_SIZE;
    append_block+=!!((size-(!!(offset%BLOCK_SIZE))*(BLOCK_SIZE-offset%BLOCK_SIZE))%BLOCK_SIZE);
    int freelist[append_block+2];
    find_free_block(append_block+1,freelist);
    add_ptr(inodeno,append_block,freelist);
    _inode=inodeno2inode(inodeno);
    _inode.size=offset+size;
  }

  int start_block=offset/BLOCK_SIZE;
  int start_offset=offset%BLOCK_SIZE;//bug point1
  int already=0;//bug point 1
  int left=size;//bug point 1
  int i;
  int ptr_cnt[2]={0};
  count_ptr(_inode.size,ptr_cnt);
  for(i=start_block;i<ptr_cnt[0];i++){
    printf("here is ok4\n");
    char buf[BLOCK_SIZE + 1];
    memset(buf,0,BLOCK_SIZE);
    if(already==size || left<=0)
      break;
    disk_read(DATA_BASE+_inode.direct_ptr[i],buf);
    if(i==start_block){
      printf("start block is%d,real block no is %d\n",start_block,_inode.direct_ptr[i]);
      memcpy(buf+start_offset,buffer,mymin(BLOCK_SIZE-start_offset,left));
      //printf("now the buf is %s\n", buf);
      already += mymin(BLOCK_SIZE - start_offset, left);
      left-=mymin(BLOCK_SIZE-start_offset,left);
    }
    if(i!=start_block){
      memcpy(buf,buffer+already,mymin(BLOCK_SIZE,left));
      already+=mymin(BLOCK_SIZE,left);
      left-=mymin(BLOCK_SIZE,left);
      //printf("now the buf is %s\n",buf);
    }
    disk_write(DATA_BASE+_inode.direct_ptr[i],buf);
  }
  if(left >0 && ptr_cnt[1]==0){
    printf("abnormal!!!!\n");
  }
  if(ptr_cnt[1] && _inode.single_ptr!=(unsigned short)-1 && left>0){
    char ptr_buf[BLOCK_SIZE+1];
    memset(ptr_buf,BLOCK_SIZE,0);
    disk_read(DATA_BASE+_inode.single_ptr,ptr_buf);
    unsigned short * pptr=(unsigned short *)ptr_buf;
    for(i=mymax(0,start_block-DIRECT_PTR);i<PTR_PER_BLOCK;i++){
      if(already==size || left <=0)
        break;
      if(*(pptr+i)==(unsigned short)-1){
        printf("ABNOMAL !!!!!! ABNOMAL !!!!!\n");
        break;
      }
      printf("indirect %d\n",*(pptr+i));
      char buf[BLOCK_SIZE+1];
      memset(buf,0,BLOCK_SIZE);
      disk_read(DATA_BASE+*(pptr+i),buf);
      if(i==start_block-DIRECT_PTR){
        memcpy(buf,buffer+already,mymin(BLOCK_SIZE-start_offset,left));
        already+=mymin(BLOCK_SIZE-start_offset,left);
        left-=mymin(BLOCK_SIZE-start_offset,left);
      }
      if(i!=start_block-DIRECT_PTR){
        memcpy(buf,buffer+already,mymin(BLOCK_SIZE,left));
        already+=mymin(BLOCK_SIZE,left);
        left-=mymin(BLOCK_SIZE,left);
      }
      disk_write(DATA_BASE+*(pptr+i),buf);
      printf("now the buf is%s\n",buf);
    }
  }
  //deal with inode
  _inode.mtime=time(NULL);
  _inode.ctime=time(NULL);
  _inode.size=offset+size;//bug point2
  int inode_index=inodeno/INODE_PER_BLOCK+INODE_BASE; //bug point2
  int inode_offset=inodeno%INODE_PER_BLOCK;//bug point2
  char buf[BLOCK_SIZE+1];
  memset(buf,0,BLOCK_SIZE);
  disk_read(inode_index,buf);
  struct inode* inode_ptr=(struct inode*)buf;
  *(inode_ptr+inode_offset)=_inode;
  disk_write(inode_index,buf);
  printf("direct ptr is:\n");
  for(i=0;i<DIRECT_PTR;i++)
    printf("%d  ",_inode.direct_ptr[i]);
  printf("\n");
  printf("single ptr is %d\n",_inode.single_ptr);
  printf("fs write finished path=%s,size=%d,offset=%d\n", path,size,offset);
  return size;
  /*
  */
}

//use find_free_block and add_ptr
//change inode size and atime ctime
int fs_truncate (const char *path, off_t size)
{
  printf("fs_truncate is begin,path=%s,newsize=%d\n",path,size);
  int append_block = 0;
  int delete_block=0;
  int inodeno=path2inodeno(path);
  if(inodeno==-1){
    printf("truncate inode not found\n");
    return -1;
  }
  struct inode _inode=inodeno2inode(inodeno);
  if(size==_inode.size)
    return 0;
  if(size <_inode.size){
    delete_block=(_inode.size/BLOCK_SIZE+!!(_inode.size%BLOCK_SIZE))-((size/BLOCK_SIZE)+!!(size%BLOCK_SIZE));
    rm_ptr(inodeno,delete_block);
    _inode=inodeno2inode(inodeno);
  }
  if(size >_inode.size){
    append_block=((size/BLOCK_SIZE)+!!(size%BLOCK_SIZE))-((_inode.size/BLOCK_SIZE)+!!(_inode.size%BLOCK_SIZE));
    int ptr_cnt[2] = {0};
    int freelist[append_block+1];
    find_free_block(append_block+1,freelist);
    add_ptr(inodeno,append_block,freelist);
    _inode=inodeno2inode(inodeno);
  }
  char buf[BLOCK_SIZE+1];
  memset(buf,0,BLOCK_SIZE);
  int inode_index=INODE_BASE+inodeno/INODE_PER_BLOCK;
  int inode_offset=inodeno%INODE_PER_BLOCK;
  disk_read(inode_index,buf);
  struct inode* inode_ptr=(struct inode*)buf;
  _inode.ctime=time(NULL);
  _inode.size=size;
  *(inode_ptr+inode_offset)=_inode;
  disk_write(inode_index,buf);
	printf("Truncate is finished:%s\n",path);
	return 0;//if not enough space, return ENOSPC
}

int fs_utime (const char *path, struct utimbuf *buffer)
{
  printf("fs_utime begin,path=%s \n",path);
  int inodeno = path2inodeno(path);
  int inode_index = inodeno / INODE_PER_BLOCK + INODE_BASE;
  int inode_offset = inodeno % INODE_PER_BLOCK;
  char buf[BLOCK_SIZE + 1];
  memset(buf, 0, BLOCK_SIZE);
  disk_read(inode_index, buf);
  struct inode *inode_ptr = (struct inode *)buf;
  (inode_ptr + inode_offset)->ctime = time(NULL);//bug point3
  (inode_ptr + inode_offset)->mtime = buffer->modtime;//bug point3
  (inode_ptr + inode_offset)->atime = buffer->actime;//bug point3
  disk_write(inode_index, buf);
  printf("Utime is finished:%s\n", path);
  return 0;
}

int fs_statfs (const char *path, struct statvfs *stat)
{
  char buf[BLOCK_SIZE+1];
  memset(buf,0,BLOCK_SIZE);
  disk_read(0,buf);
  struct statvfs* stat_ptr=(struct statvfs*)buf;
  stat->f_bsize=BLOCK_SIZE;
  stat->f_blocks=BLOCK_NUM;
  stat->f_bfree=map_count(DM_BASE);
  stat->f_bavail=map_count(DM_BASE);
  stat->f_files=find_free_inode()+1;
  stat->f_ffree=map_count(IM_BASE);
  stat->f_favail=map_count(IM_BASE);
  stat->f_namemax=MAXNAME;
	printf("Statfs is called:%s\n",path);
	return 0;
}

int fs_open (const char *path, struct fuse_file_info *fi)
{
	printf("Open is called:%s\n",path);
	return 0;
}

//Functions you don't actually need to modify
int fs_release (const char *path, struct fuse_file_info *fi)
{
	printf("Release is called:%s\n",path);
	return 0;
}

int fs_opendir (const char *path, struct fuse_file_info *fi)
{
	printf("Opendir is called:%s\n",path);
	return 0;
}

int fs_releasedir (const char * path, struct fuse_file_info *fi)
{
	printf("Releasedir is called:%s\n",path);
	return 0;
}

static struct fuse_operations fs_operations = {
	.getattr    = fs_getattr,
	.readdir    = fs_readdir,
	.read       = fs_read,
	.mkdir      = fs_mkdir,
	.rmdir      = fs_rmdir,
	.unlink     = fs_unlink,
	.rename     = fs_rename,
	.truncate   = fs_truncate,
	.utime      = fs_utime,
	.mknod      = fs_mknod,
	.write      = fs_write,
	.statfs     = fs_statfs,
	.open       = fs_open,
	.release    = fs_release,
	.opendir    = fs_opendir,
	.releasedir = fs_releasedir
};

int main(int argc, char *argv[])
{
	if(disk_init())
		{
		printf("Can't open virtual disk!\n");
		return -1;
		}
	if(mkfs())
		{
		printf("Mkfs failed!\n");
		return -2;
		}
    return fuse_main(argc, argv, &fs_operations, NULL);
}
