#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* project 4 */
#include "include/filesys/fat.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
 * Must be exactly DISK_SECTOR_SIZE bytes long. */
/* strcut inode_disk는 총 512byte
   4 * 3 + (4*125) = 512 byte*/
struct inode_disk {
	disk_sector_t start;                /* First data sector. */
	off_t length;                       /* File size in bytes. */
	unsigned magic;                     /* Magic number. */
	uint32_t unused[125];               /* Not used. */
};

/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
/* SIZE 바이트 길이의 inode를 할당하기 위한 섹터의 수를 리턴합니다. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode {
	struct list_elem elem;              /* Element in inode list. */
	disk_sector_t sector;               /* Sector number of disk location. */
	int open_cnt;                       /* Number of openers. */
	bool removed;                       /* True if deleted, false otherwise. */
	int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
	struct inode_disk data;             /* Inode content. */
};

/* Returns the disk sector that contains byte offset POS within
 * INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
/* offset이 존재하는 disk_sector 번호를 반환*/
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {
	ASSERT (inode != NULL);

	// printf("==========byte_to_sector 진입 inode->data.length : %d \n",inode->data.length);
	if (pos < inode->data.length){
		#ifdef EFILESYS
			return get_sector(inode->data.start, pos);
		#else
			// printf("==========byte_to_sector 진입 #else\n");
			return inode->data.start + pos / DISK_SECTOR_SIZE;
		#endif
	}
	else
		// printf("==========byte_to_sector 진입 return -1\n");
		return -1;
}

/* List of open inodes, so that opening a single inode twice
 * returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) {
	list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * disk.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
/* LENGTH 바이트의 데이터를 가지고 inode를 초기화하고
 * 파일 시스템 디스크의 섹터 SECTOR에 새로운 inode를 write 합니다. 
 * 성공하면 true, 실패하면 false를 리턴합니다. */
bool
inode_create (disk_sector_t sector, off_t length) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 * one sector in size, and you should fix that. */
	/* 이 assertion이 실패하면, inode 구조체가 한 섹터 크기를 갖지 않는다는 말이므로
	 * 당신은 이부분을 고쳐야 합니다. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		/*길이 length만큼 저장 시 필요한 sector 수 반환*/
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		#ifdef EFILESYS
			/* 파일의 첫번째 클러스터 번호 받기 */
			cluster_t start = fat_create_chain(0);
			if (!start) {
				free(disk_inode);
				return false;
			}
			/* disk_inode->start에 inode를 가리키는 파일의 첫번째 섹터번호 저장 */
			disk_inode->start = cluster_to_sector(start);
			/* disk_inode를 inode 섹터번호위치에 write */
			disk_write (filesys_disk, sector, disk_inode);

			if (sectors > 0) {
				static char zeros[DISK_SECTOR_SIZE];

				size_t i;
				cluster_t clst = start;
				disk_write (filesys_disk, cluster_to_sector(start), zeros);

				/* file길이만큼의 sectors에 zeros로 초기화*/
				for (i = 0; i < sectors - 1; i++) {
					clst = fat_create_chain(clst);
					disk_write (filesys_disk, cluster_to_sector(clst), zeros);
				}
			}
			success = true;

		#else
			if (free_map_allocate (sectors, &disk_inode->start)) {
				disk_write (filesys_disk, sector, disk_inode);
				if (sectors > 0) {
					static char zeros[DISK_SECTOR_SIZE];
					size_t i;

					for (i = 0; i < sectors; i++) 
						disk_write (filesys_disk, disk_inode->start + i, zeros); 
				}
				success = true; 
			} 
		#endif
		free (disk_inode);
	}
	return success;
}

/* Reads an inode from SECTOR
 * and returns a `struct inode' that contains it.
 * Returns a null pointer if memory allocation fails. */
/* inode 구조체 생성 */
/* inode는 메타데이터 정보를 담고있는 512Byte의 inode_disk를 하나씩 가지고 있고, 
   inode_disk는 실제 파일에 대한 메타데이터를 포함하고 있음*/
/* 파일과 디렉토리 모두 inode를 하나씩 가리키는 inode 포인터를 가지고 있다 */
struct inode *
inode_open (disk_sector_t sector) {
	struct list_elem *e;
	struct inode *inode;

	/* Check whether this inode is already open. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen (inode);
			return inode; 
		}
	}

	/* Allocate memory. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	disk_read (filesys_disk, inode->sector, &inode->data);
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode) {
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode) {
	return inode->sector;
}

/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
/* INODE를 닫고 디스크에 내용을 씁니다.
 * 만약 INODE를 향한 마지막 참조였다면, 해당 메모리를 free 합니다. 
 * 만약 INODE가 이미 제거된 inode 였을 경우. 해당 블록을 free 합니다.*/
void
inode_close (struct inode *inode) {
	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0) {
		/* Remove from inode list and release lock. */
		list_remove (&inode->elem);

		/* Deallocate blocks if removed. */
		if (inode->removed) {
			// free_map_release (inode->sector, 1);
			// free_map_release (inode->data.start,
			// 		bytes_to_sectors (inode->data.length)); 
			fat_remove_chain(sector_to_cluster(inode->sector), 0);
			fat_remove_chain(sector_to_cluster(inode->data.start), 0);
		}

		free (inode); 
	}
}

/* Marks INODE to be deleted when it is closed by the last caller who
 * has it open. */
void
inode_remove (struct inode *inode) {
	ASSERT (inode != NULL);
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	while (size > 0) {
		/* Disk sector to read, starting byte offset within sector. */
		// printf("!!!!!=========byte_to_sector 진입전 inode->data.length : %d \n",inode->data.length);
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* Read full sector directly into caller's buffer. */
			disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
		} else {
			/* Read sector into bounce buffer, then partially copy
			 * into caller's buffer. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			disk_read (filesys_disk, sector_idx, bounce);
			memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free (bounce);

	return bytes_read;
}

// /* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
//  * Returns the number of bytes actually written, which may be
//  * less than SIZE if end of file is reached or an error occurs.
//  * (Normally a write at end of file would extend the inode, but
//  * growth is not yet implemented.) */
// /* 파일의 현재 위치인 inode에서 SIZE 바이트를 BUFFER로 씁니다.*/
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
	// printf("********************inode_write_at\n");
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;
	uint8_t zero[512];
	memset(zero, 0, DISK_SECTOR_SIZE);
	if (inode->deny_write_cnt){
		return 0;
	}
	#ifdef EFILESYS
		disk_sector_t sect = byte_to_sector(inode, offset + size);
		disk_sector_t sectors=0;
		if (sect == -1) {
			sectors = DIV_ROUND_UP(offset + size - inode->data.length, 512);
			cluster_t clst = sector_to_cluster(byte_to_sector(inode, inode_length(inode) - 1));
			for (int i = 0; i < sectors; i++) {
				clst = fat_create_chain(clst);
				if(clst == 0) {
					break;
				}
				disk_write(filesys_disk, cluster_to_sector(clst), zero);
				// disk_write(filesys_disk, , zero);
			}
			inode->data.length=offset+size;
		}
		// printf("=========inode->data.length : %d\n",inode->data.length);

		// disk_sector_t write_sect = byte_to_sector(inode,offset);
		// cluster_t write_clst = sector_to_cluster(write_sect);
		// while(write_clst != EOChain){
		// 	disk_write(filesys_disk, cluster_to_sector(write_clst), buffer + bytes_written);
		// 	write_clst = fat_get(write_clst);
		// 	bytes_written += DISK_SECTOR_SIZE;
		// }

	#endif
		while (size > 0) {

			/* Sector to write, starting byte offset within sector. */
			// offest = 512+462 = 974  | 50
			disk_sector_t sector_idx = byte_to_sector (inode, offset);
			int sector_ofs = offset % DISK_SECTOR_SIZE;	//462 | 50

			/* Bytes left in inode, bytes left in sector, lesser of the two. */
			off_t inode_left = inode_length (inode) - offset;	//1024-974 = 50 | 1024-50=974
			int sector_left = DISK_SECTOR_SIZE - sector_ofs;	//50 | 512-50=462
			int min_left = inode_left < sector_left ? inode_left : sector_left;

			/* Number of bytes to actually write into this sector. */
			int chunk_size = size < min_left ? size : min_left;
			if (chunk_size <= 0)
				break;

			if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
				/* Write full sector directly to disk. */
				disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
			} else {
				/* We need a bounce buffer. */
				if (bounce == NULL) {
					bounce = malloc (DISK_SECTOR_SIZE);
					if (bounce == NULL)
						break;
				}

				/* If the sector contains data before or after the chunk
				we're writing, then we need to read in the sector
				first.  Otherwise we start with a sector of all zeros. */
				if (sector_ofs > 0 || chunk_size < sector_left) 
					disk_read (filesys_disk, sector_idx, bounce);
				else
					memset (bounce, 0, DISK_SECTOR_SIZE);
				memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
				disk_write (filesys_disk, sector_idx, bounce); 
			}

			/* Advance. */
			size -= chunk_size;
			offset += chunk_size;
			bytes_written += chunk_size;
		}

	free (bounce);
	disk_write (filesys_disk, inode->sector, &inode->data);
	return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
	inode->deny_write_cnt++;
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 * Must be called once by each inode opener who has called
 * inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) {
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode) {
	return inode->data.length;
}
