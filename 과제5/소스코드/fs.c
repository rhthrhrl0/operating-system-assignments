// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb; 

void csSet(struct inode* ip, uint index, uint bn, uint count);
int csGetBn(struct inode* ip,uint index);
int csGetCount(struct inode* ip, uint index);
int csCountIncrement(struct inode* ip,uint index);

// Read the super block.
void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){ //모든 비트맵 검사. 단, 블록 단위로 검사. 비트맵이 블록을 여러개 사용할 수 있음.
    bp = bread(dev, BBLOCK(b, sb)); //b번째 블록(데이터 블록만이 아님)을 버퍼로 읽어옴.
	// 디스크의 블록 한개에 있는 비트들 모두 검사. BPB는 한 블록당 존재하는 비트 개수임.
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8); //bi%8은 바이트 단위로 하고 남은 비트수를 말함. 
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi; // 블록넘버를 반환함.
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

static int isBlockFree(uint dev){
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){ //모든 비트맵 검사. 단, 블록 단위로 검사. 비트맵이 블록을 여러개 사용할 수 있음.
    bp = bread(dev, BBLOCK(b, sb)); //b번째 블록(데이터 블록만이 아님)을 버퍼로 읽어옴.
	// 디스크의 블록 한개에 있는 비트들 모두 검사. BPB는 한 블록당 존재하는 비트 개수임.
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8); //bi%8은 바이트 단위로 하고 남은 비트수를 말함. 
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        brelse(bp);
        return 1; // 여유 블록 있음을 의미.
      }
    }
    brelse(bp);
  }
  cprintf("error: There is no more free memory. Stop saving data.\n");
  return -1; // 여유 블록없음.
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in cache: an entry in the inode cache
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a cache entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   cache entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays cached and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The icache.lock spin-lock protects the allocation of icache
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold icache.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
iinit(int dev)
{
  int i = 0;
  
  initlock(&icache.lock, "icache");
  for(i = 0; i < NINODE; i++) {
	// 절전 블록을 초기화
    initsleeplock(&icache.inode[i].lock, "inode");
  }

  readsb(dev, &sb);
  cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d\
 inodestart %d bmap start %d\n", sb.size, sb.nblocks,
          sb.ninodes, sb.nlog, sb.logstart, sb.inodestart,
          sb.bmapstart);
}

static struct inode* iget(uint dev, uint inum);

//PAGEBREAK!
// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode.
struct inode*
ialloc(uint dev, short type) // 파일인지 디렉토리인지에 따라.. 
{
  int inum;
  struct buf *bp;
  struct dinode *dip;// 디스크 상의 inode를 의미.
	
  // 0은 원래 건너뜀. 안쓰는 inode.
  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB; // IPB는 블록 당 들어갈 수 잇는 inode의 개수임.
	// 즉, 해당 inode번호가 포함된 블록을 디스크에서 읽어서 버퍼에 올림
	// 그리고 거기에서 해당 inode가 위치하는 메모리 주소를 찾음. 그게 dip임.
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // mark it allocated on the disk. bp에 type적은거 디스크에 반영시킴.
      brelse(bp);
      return iget(dev, inum); // 해당 inode의 캐시된 복사본을 반환함.
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk, since i-node cache is write-through.
// Caller must hold ip->lock.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
	// 여기서  ref는 해당 inode의 사용여부임.
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode cache entry.
  if(empty == 0) // 만약 돌았는데 icashe에 해당 inode번호도 없고 안쓴 inode가 없다면 문제 있는거임.
    panic("iget: no inodes");

  // 찾는 inode 번호가 지금 캐쉬에 안올라와있으면 캐쉬 빈자리에 
  // 해당 inode 번호를 넣고 해당 inode를 반환해줌.
  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){ // 디스크로부터 inode 정보를 
		  			//아직 실질적으로 가져오지 않았다면
    bp = bread(ip->dev, IBLOCK(ip->inum, sb)); 
    dip = (struct dinode*)bp->data + ip->inum%IPB; // 블록에서 읽어야 하는 위치
	// 블록에서 읽어서 캐쉬로 가져옴.
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void
iput(struct inode *ip)
{ // 메모리 내 캐쉬된 inode 내용을 제거하는거임. 이 파일을 모두 다 쓴경우
  acquiresleep(&ip->lock);
  if(ip->valid && ip->nlink == 0){ // 해당 inode 즉 이 파일을 가리키는 디렉토리가 없다면...
    acquire(&icache.lock);
    int r = ip->ref;
    release(&icache.lock);
    if(r == 1){ // 이 참조가 마지막 참조면 이걸 끝으로 더이상 이 파일은 의미없음.
      // inode has no links and no other references: truncate and free.
      itrunc(ip); // 블록 비움,
      ip->type = 0; 
      iupdate(ip); // 해당 메모리 상 캐쉬된 inode의 내용을 실제 디스크에 반영시킴.
      ip->valid = 0;
    }
  }
  releasesleep(&ip->lock);

  acquire(&icache.lock);
  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

//PAGEBREAK!
// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  // inode가 가리키는 블록들 중 n번째 addrs가 가리키는 블록의 주소반환
  // 만약 가리키는게 없으면 블록을 할당해서 반환해줌.
  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
		if(ip->type==T_CS){
			if(isBlockFree(ip->dev)<0){
				return 0; // 데이터블록없음.
			}
		}
		addr = balloc(ip->dev); //블록을 할당
		if(ip->type==T_CS){
      		csSet(ip,bn,addr,1); 
			return addr;
		}
		else{
      		ip->addrs[bn] = addr;
			return addr;
		}
	}
	else{
		if(ip->type==T_CS){
      		return csGetBn(ip,bn);
		}
		else{
      		return addr;
		} 
	}
  }
  bn -= NDIRECT; // inode가 가리키는 블록중 13번째 이후를 찾는 경우
  // 14번째를 찾는다는 것은 인덱스 13을 찾는 것이고 이는 
  // 간접포인터를 따라 나오는 0번 인덱스 블록을 찾는 것을 말함.
  
  // 만약 이게 256을 넘는다면 문제가 있는거임.
  // 데이터 디스크 블록에는 256개의 포인터가 있을 수 있음.
  if(bn < NINDIRECT && ip->type!=T_CS){ //예외처리 추가.............
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr); //해당 데이터 블록을 읽어옴.
    a = (uint*)bp->data; // 이 블록에서 데이터가 존재하는 위치로 이동.
    if((addr = a[bn]) == 0){ //그 중 bn번째 데이터가 있는 주소를 봄.
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Only called when the inode has no links
// to it (no directory entries referring to it)
// and has no in-memory reference to it (is
// not an open file or current directory).
static void
itrunc(struct inode *ip)
{ // 해당 inode가 가리키는 데이터들을 모두 비움. 
  // 직접, 간접 비우고 비트맵 업데이트 시킴.
  int i, j;
  struct buf *bp;
  uint *a;

  // 해당 inode가 가리키는 데이터 블록들 중 직접으로 가리키는 놈들에 대해서 모두 프리시키고 데이터비트맵을 변경해야함. 
  // 여기 수정해야함.

  if(ip->type==T_CS){
 	 for(i = 0; i < NDIRECT; i++){
 	   if(ip->addrs[i]){
		 uint startBlock=csGetBn(ip,i); //
		 uint bCount=csGetCount(ip,i); // 
		 for(int k=0;k<bCount;k++){
		 	bfree(ip->dev, startBlock+k);
		 }
 	     ip->addrs[i] = 0;
 	   }
 	 }
  }
  else{
 	 for(i = 0; i < NDIRECT; i++){
 	   if(ip->addrs[i]){
 	     bfree(ip->dev, ip->addrs[i]);
 	     ip->addrs[i] = 0;
 	   }
 	 }

 	 if(ip->addrs[NDIRECT]){
 	   bp = bread(ip->dev, ip->addrs[NDIRECT]);
 	   a = (uint*)bp->data;
 	   for(j = 0; j < NINDIRECT; j++){
 	     if(a[j])
 	       bfree(ip->dev, a[j]);
 	   }
 	   brelse(bp);
  	  bfree(ip->dev, ip->addrs[NDIRECT]);
  	  ip->addrs[NDIRECT] = 0;
  	}
  }

  // inode 크기를 0으로 바꾸고
  ip->size = 0;
  iupdate(ip); // 디스크에 해당 inode 비운거 반영시킴.
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)
{
  // 캐시에 잇는 inode의 정보를 st에 복사해줌.
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}


void csSet(struct inode* ip, uint index, uint bn, uint count){
	uint blockN=bn<<8;
	ip->addrs[index]=blockN|count;
}

int csGetBn(struct inode* ip,uint index){
	return (ip->addrs[index]>>8);
}

int csGetCount(struct inode* ip, uint index){
	return (ip->addrs[index]<<24)>>24;
}

int csCountIncrement(struct inode* ip,uint index){
	uint blockN=csGetBn(ip,index);
	uint count=csGetCount(ip,index);
	csSet(ip,index,blockN,count+1);
	return count+1;
}

//PAGEBREAK!
// Read data from inode.
// Caller must hold ip->lock.
// 디스크에서 데이터를 읽는 용도
int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size) //읽어올 크기를 재지정.
    n = ip->size - off;

  if(ip->type==T_CS){
	  //tot은 현재까지 읽은 바이트 크기. 
	  //n은 이번에 읽어야 하는 총 크기.
	  //off는 오프셋 위치임. 읽은 만큼 동적으로 계속 이동함.
	  //m은 이번 반복에서 읽은양.
	  //bmap은 ip의 addrs중 해당 번째가 읽어올 주소를 말함. 
      
	  uint offBn=off/BSIZE; //0번부터 시작해서 i번째로 등장하는 블록 안의

	  uint idx=0;
	  uint k=0;
	  uint curBn=0;
	  uint curCount=0;
	  uint totCount=0;
	  for(idx=0;idx<NDIRECT;idx++){
	  	curBn=csGetBn(ip,idx);
		curCount=csGetCount(ip,idx);
		for(k=0;k<curCount;k++){
			if(k!=0){
				curBn++;
			}
			if(totCount==offBn)
				break;
			totCount++;
		}
		if(totCount==offBn)
			break;
	  } 

	  //curBn의 j번째 위치부터 시작해서 n만큼 읽으면 됨. curBn은 addrs[idx]가 가리키는 연속된 블록 중
	  // k+1번째 블록임. (1번부터 숫자를 센다)
		
	  if(idx==NDIRECT){
	  	cprintf("error...CS_FILE_SYSTEM dont have ndirect pointer\n");
		return -1;
	  }
	  

	  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
		bp = bread(ip->dev, curBn); //curBn번째 블록의 내용을 bp에 가져옴
  	  	m = min(n - tot, BSIZE - off%BSIZE); // m은 남은 읽을 양과 
  	  	memmove(dst, bp->data + off%BSIZE, m);
  	  	brelse(bp);
		if (k<curCount){
			k++;
			curBn++;
		}else{
			idx++;
			curBn=csGetBn(ip,idx); //다음 직접블록의 시작블록번호.
			curCount=csGetCount(ip,idx);
			k=0;
		}
  	  }
  }
  else{
	  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
		uint curBn=bmap(ip,off/BSIZE); //이번 offset위치가 addrs 중 어디에 있는지 확인하고 할당받은 적 없다면 할당해주고 그 블록 넘버를 반환
  	  	bp = bread(ip->dev, curBn); //curBn번째 블록의 내용을 bp에 가져옴
  	  	m = min(n - tot, BSIZE - off%BSIZE); // m은 남은 읽을 양과 
  	  	memmove(dst, bp->data + off%BSIZE, m);
  	  	brelse(bp);
  	  }
  }
  return n;
}

// PAGEBREAK!
// Write data to inode.
// Caller must hold ip->lock.
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;
  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write){
			return -1;
	}
    return devsw[ip->major].write(ip, src, n);
  }

  if(off > ip->size || off + n < off){
		  return -1;
  }
  if(ip->type!=T_CS && off + n > MAXFILE*BSIZE){
		  return -1;
  }

  if(ip->type==T_CS && off + n > 12*255*BSIZE){
		  return -1;
  }


  //
  if (ip->type==T_CS){
  	//해당 쓸 위치의 오프셋이 존재하는 디스크 블록 번호를 구해옴.
	// 만약 그 다음 블록에 대한 비트맵이 프리하다면... 할당받고
	// 기존의 arrds 수정하고 계속 연속될때까지 써나감.
      uint i=0;
	  uint j=0;
	  for(i=0;i<NDIRECT;i++){
		j=csGetCount(ip,i); //연속된 블록의 개수.
		if(ip->addrs[i+1]==0)
				break;
	  } 
	  uint bi=i;
	  uint bj=j;

	  if(j==255 && off%BSIZE==0){
	  	i++;
		j=1;
	  } else if(off%BSIZE==0) {
	  	j++;
	  }

	  if(i==NDIRECT){
	  	cprintf("error...dont have ndirect pointer\n");
		return -1;
	  }

	  uint beforeBn=0;
	  uint newBn=0;

	  if(!(bi==0 && bj==0)){
	  	beforeBn=csGetBn(ip,bi)+bj-1;
	  }

	 
	  m=0;
	  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
			if (j==1){
				newBn=bmap(ip,i); // 이전에 있었다면 그거 가져올거임. 새로 가져와야 하면 새로 가져옴.
				if(newBn==0){ //데이터 블록 더 없음을 의미.
  					ip->size = off;
  					iupdate(ip);
					return tot;
				}
				bp = bread(ip->dev, newBn);
				m = min(n - tot, BSIZE - off%BSIZE);
				memmove(bp->data + off%BSIZE, src, m);
 				log_write(bp);
 				brelse(bp);
				j++;
				beforeBn=newBn;
			}else if (i<NDIRECT){ //직접블록의 첫 블록이 아닌경우.
				if(off%BSIZE==0){	
					if(ip->type==T_CS){
						if(isBlockFree(ip->dev)<0){ // 데이터 블록 없음.
  							ip->size = off;
  							iupdate(ip);
							return tot;
						}
					}
					newBn=balloc(ip->dev); 
				} else {
					newBn=bmap(ip,i)+j-1; //오프셋이 있는 위치의 블록을 가져옴. 
				}

				if(newBn == beforeBn + 1){
					bp = bread(ip->dev, newBn);
					m = min(n - tot, BSIZE - off%BSIZE);
					memmove(bp->data + off%BSIZE, src, m);
 					log_write(bp);
 					brelse(bp);
					j++;
					csCountIncrement(ip,i);
					beforeBn=newBn;
				} else if(newBn == beforeBn){ // 오프셋이 beforeBn 블록에 위치한 경우.
					bp = bread(ip->dev, newBn);
					m = min(n - tot, BSIZE - off%BSIZE);
					memmove(bp->data + off%BSIZE, src, m);
 					log_write(bp);
 					brelse(bp);
					j++; // 어차피 이번 블록 다 안썼다는 것은 쓸 n만큼 다 썼다는것임. 어차피 더이상 쓰기 안함.
					beforeBn=newBn;
				}else {
					bfree(ip->dev,newBn);
					i++;
					j=1;
				}
			} else{
				cprintf("error:....Exceeds the number of direct blocks\n");
				return -1;
			}


			if(j==256){
				i++;
				j=1;
			}
 	  }
  }
  else{
	for(tot=0; tot<n; tot+=m, off+=m, src+=m){
		bp = bread(ip->dev, bmap(ip, off/BSIZE));
		m = min(n - tot, BSIZE - off%BSIZE);
		memmove(bp->data + off%BSIZE, src, m);
 		log_write(bp);
 		brelse(bp);
 	}
  }


  if(n > 0 && off > ip->size){
  	ip->size = off;
  	iupdate(ip);
  }
  
  return n;
}

//PAGEBREAK!
// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

//PAGEBREAK!
// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}

