#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors;	//20,160개 (9.84MB)
	unsigned int fat_start;	//1
	unsigned int fat_sectors; /* Size of FAT in sectors. 157섹터(FAT자체의 크기)*/ 
	unsigned int root_dir_cluster;
};

/* FAT FS */
struct fat_fs {
	struct fat_boot bs;			//부팅 시 FAT 정보를 담는 구조체
	unsigned int *fat;			//FAT array
	unsigned int fat_length;	//20096 파일 시스템에 있는 클러스터 수
	disk_sector_t data_start;	//파일을 저장하기 위한 시작섹터번호
	cluster_t last_clst;		//마지막 클러스터
	struct lock write_lock;		
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

/* FAT 테이블 초기화하는 함수 */
void
fat_init (void) {
	// FAT 담을 공간을 커널풀에 할당 -> 구조체 자체는 위에 선언해둠.
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk
	// 디스크에서 FAT 읽어서 calloc담은 공간에 저장
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	// printf("=============total_sectors: %d\n",fat_fs->bs.total_sectors);
	// printf("=============fat_sectors: %d\n",fat_fs->bs.fat_sectors);
	fat_fs_init ();
}

void
fat_open (void) {
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
}

void
fat_close (void) {
	// Write FAT boot sector
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
}

void
fat_create (void) {
	// Create FAT boot
	fat_boot_create ();
	fat_fs_init ();

	// Create FAT table
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");
	
	// Set up ROOT_DIR_CLST
	fat_put (ROOT_DIR_CLUSTER, EOChain);
	
	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);
}

/* fat_boot bs 구조체 초기화*/
void
fat_boot_create (void) {
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),//20160 (9.84375MB)
	    .fat_start = 1,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

void
fat_fs_init (void) {
	/* TODO: Your code goes here. */
	/*fat_length: 파일 시스템에 얼마나 클러스터가 많은 지를 저장*/
	/*20003 (20160, 20096)*/
	// fat_fs->fat_length = fat_fs->bs.total_sectors-fat_fs->bs.fat_sectors;
	fat_fs->fat_length = fat_fs->bs.fat_sectors*(DISK_SECTOR_SIZE/sizeof(cluster_t));
	/*data_start: 파일 저장 시작할 수 있는 섹터 위치 저장 => DATA Sector 시작 지점*/
	fat_fs->data_start = fat_fs->bs.fat_start+fat_fs->bs.fat_sectors;
	// lock_init(&fat_fs->write_lock);
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
cluster_t
fat_create_chain (cluster_t clst) {
	/* TODO: Your code goes here. */
	cluster_t i;
	//fat_fs->bs.fat_start
	for(i = fat_fs->bs.fat_start; i <= fat_fs->fat_length; i++) {
		if (fat_get(i) == 0) {
			if(clst == 0) {
				fat_put(i, EOChain);
			}
			else {
				fat_put(i, EOChain);
				fat_put(clst, i);
			}
			return i;
		}
	}
	return 0;
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
void
fat_remove_chain (cluster_t clst, cluster_t pclst) {
	/* TODO: Your code goes here. */
	if(clst > fat_fs->fat_length) {
		return;
	}

	cluster_t i = clst;
	cluster_t val;
	while(i != EOChain) {
		val = fat_get(i);
		fat_put(i, 0);
		i = val;
	}
	if(pclst != 0) {
		fat_put(pclst, EOChain);
	}
}

/* Update a value in the FAT table. */
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */
	fat_fs->fat[clst] = val;

}

/* Fetch a value in the FAT table. */
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */
	return fat_fs->fat[clst];
}

/* Covert a cluster # to a sector number. */
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
	return clst+fat_fs->bs.fat_sectors;
	// return clst+fat_fs->data_start;
}
cluster_t
sector_to_cluster (disk_sector_t sect) {
	/* TODO: Your code goes here. */
	return sect-fat_fs->bs.fat_sectors;
	// return sect-fat_fs->data_start;
}

disk_sector_t
get_sector(disk_sector_t start, off_t pos){
	cluster_t start_clst = sector_to_cluster(start);
	
	int i=0;
	for (i=0; i<pos/DISK_SECTOR_SIZE;i++){
		start_clst=fat_get(start_clst);
		if (start_clst == EOChain || start_clst == 0) {
			return -1;
		}
	}
	return cluster_to_sector(start_clst);
}


void print_fat(){
	printf("\n===================print FAT====================================================================================\n");
	for(int i = 0; i < 200; i++){
		printf(" [%4d|%10d] ", i, fat_fs->fat[i]);
		if(i%5 == 4) printf("\n");
	}
	printf("\n================================================================================================================\n");
}