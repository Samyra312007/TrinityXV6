
#define ROOTINO 1
#define BSIZE   1024

struct superblock {
  uint magic;
  uint size;
  uint nblocks;
  uint ninodes;
  uint nlog;
  uint logstart;
  uint inodestart;
  uint bmapstart;
};

#define FSMAGIC 0x10203040

#define NDIRECT   12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE   (NDIRECT + NINDIRECT)
#define NLINK_MAX 32767

struct dinode {
  short type;
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT + 1];
};

#define IPB (BSIZE / sizeof(struct dinode))

#define IBLOCK(i, sb) ((i) / IPB + sb.inodestart)

#define BPB (BSIZE * 8)

#define BBLOCK(b, sb) ((b) / BPB + sb.bmapstart)

#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ] __attribute__((nonstring));
};
