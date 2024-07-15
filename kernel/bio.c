// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;

  struct buf buckets[NBUCKETS];
  struct spinlock bucketsLocks[NBUCKETS];
} bcache;

int haskBlock(int block)
{
  return block % NBUCKETS;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Init all buckets
  for (int i = 0; i < NBUCKETS; ++i)
  {
    initlock(&bcache.bucketsLocks[i], "bcache");

    // Create linked list of buffers
    bcache.buckets[i].prev = &bcache.buckets[i];
    bcache.buckets[i].next = &bcache.buckets[i];
  }

  for (b = bcache.buf; b < bcache.buf + NBUF; ++b)
  {
    int hashId = haskBlock(b->blockno);

    // Insert b next to the head
    b->next = bcache.buckets[hashId].next;
    b->prev = &bcache.buckets[hashId];
    initsleeplock(&b->lock, "buffer");
    bcache.buckets[hashId].next->prev = b;
    bcache.buckets[hashId].next = b;

    b->ticks = ticks;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int hashId = haskBlock(blockno);
  acquire(&bcache.bucketsLocks[hashId]);

  // Is the block already cached?
  for(b = bcache.buckets[hashId].next; b != &bcache.buckets[hashId]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucketsLocks[hashId]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  struct buf *bMin = &bcache.buckets[hashId];
  release(&bcache.bucketsLocks[hashId]);
  int valid = 0;
  int minId = 0;

  for (int i = 0; i < NBUCKETS; ++i) {
    acquire(&bcache.bucketsLocks[i]);
    for(b = bcache.buckets[i].next; b != &bcache.buckets[i]; b = b->next){
      if(b->refcnt == 0 && (b->ticks <= bMin->ticks)) {
        bMin = b;
        valid = 1;
        minId = i;
      }
    }
    release(&bcache.bucketsLocks[i]);
  }

  
  if (!valid)
    panic("bget: no buffers");

  acquire(&bcache.bucketsLocks[minId]);

  bMin->dev = dev;
  bMin->blockno = blockno;
  bMin->valid = 0;
  bMin->refcnt = 1;
  bMin->ticks = ticks;
  release(&bcache.bucketsLocks[minId]);
  acquiresleep(&bMin->lock);

  return bMin;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int hashId = haskBlock(b->blockno);
  acquire(&bcache.bucketsLocks[hashId]);

  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // Delete b from the list
    b->next->prev = b->prev;
    b->prev->next = b->next;
    // Insert b next to the head
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.buckets[hashId].next->prev = b;
    bcache.buckets[hashId].next = b;
  }
  
  release(&bcache.bucketsLocks[hashId]);
}

void
bpin(struct buf *b) {
  int hashId = haskBlock(b->blockno);
  acquire(&bcache.bucketsLocks[hashId]);
  b->refcnt++;
  release(&bcache.bucketsLocks[hashId]);
}

void
bunpin(struct buf *b) {
  int hashId = haskBlock(b->blockno);
  acquire(&bcache.bucketsLocks[hashId]);
  b->refcnt--;
  release(&bcache.bucketsLocks[hashId]);
}


