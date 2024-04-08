// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int ref[PHYSTOP / PGSIZE];
} kref;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&kref.lock, "kref");
  freerange(end, (void*)PHYSTOP);
}

int 
increref(uint64 pa, int count) {
  acquire(&kref.lock);
  int n = pa / PGSIZE;
  kref.ref[n] += count;
  int temp = kref.ref[n];
  // if (pa == 0x0000000087f44000)
  //   printf("address ref incre func: %p\n", pa);
  release(&kref.lock);
  return temp;
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kref.ref[((uint64)p) / PGSIZE] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kref.lock);
  int n = ((uint64)pa) / PGSIZE;
  if (kref.ref[n] <= 0) {
    printf("panic address %p\n", pa);
    panic("Ref less than or equal 0!\n");
  }
  kref.ref[n] -= 1;
  // if ((uint64)pa == 0x0000000087f44000)
  //     printf("pa ref decre: %p\n", pa);
  int temp = kref.ref[n];
  release(&kref.lock);

  if (temp > 0) {
    // printf("pa: %p\n", pa);
    return ;
  }

  // if (increref((uint64)pa, -1) > 0) {
  //   printf("pa: %p\n", pa);
  //   return ;
  // } 

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    acquire(&kref.lock);
    int n = (uint64)r / PGSIZE;
    if (kref.ref[n] != 0) {
      panic("Alloc a existed page");
    }
    // if ((uint64)r == 0x0000000087f44000)
    //   printf("address ref incre: %p\n", r);
    kref.ref[n] = 1;
    release(&kref.lock);
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
