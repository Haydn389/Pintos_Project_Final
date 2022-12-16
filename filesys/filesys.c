#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "include/filesys/fat.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
/* 주어진 INITIAL_SIZE으로 NAME 이라는 이름의 파일을 생성합니다.
 * 성공시 true를 반환하며, 실패시 false를 반환합니다.
 * NAME이라는 파일이 이미 존재할 경우 또는 내부 메모리 할당이 실패할 경우 실패합니다. */
bool
filesys_create (const char *name, off_t initial_size) {
	disk_sector_t inode_sector = 0;
	struct dir *dir = dir_open_root ();
	/* 추후 삭제 예정 */
	cluster_t clst = fat_create_chain(0);
	inode_sector = cluster_to_sector(clst);
	bool success = (dir != NULL
			//&& free_map_allocate (1, &inode_sector)
			&& inode_sector
			&& inode_create (inode_sector, initial_size)
			&& dir_add (dir, name, inode_sector));
	if (!success && inode_sector != 0)
		// free_map_release (inode_sector, 1);
		fat_remove_chain(sector_to_cluster(inode_sector), 0);
	dir_close (dir);
	return success;
	//------project4-start------------------------
	// cluster_t new_cluster = fat_create_chain(0);	// inode를 위한 새로운 cluster 만들기
	// if (new_cluster == 0) return false; 
	// disk_sector_t inode_sector = cluster_to_sector(new_cluster);	// 새로 만든 cluster의 disk sector
	// struct dir *dir = dir_open_root();				// dir open
	// bool success = (dir != NULL 
	// 			&& inode_create(inode_sector, initial_size) 
	// 			&& dir_add(dir, name, inode_sector));	// inode 만들고, dir에 inode 추가
	// if (!success && new_cluster != 0) {
	// 	fat_remove_chain(new_cluster, 0);	// 성공 못했을 시 예외처리
	// }
	// dir_close(dir);	
	//------project4-end--------------------------

	/*기존코드*/
	// return success;
	// 	disk_sector_t inode_sector = 0;
	// struct dir *dir = dir_open_root ();
	// bool success = (dir != NULL
	// 		&& free_map_allocate (1, &inode_sector)
	// 		&& inode_create (inode_sector, initial_size)
	// 		&& dir_add (dir, name, inode_sector));
	// if (!success && inode_sector != 0)
	// 	free_map_release (inode_sector, 1);
	// dir_close (dir);

	// return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
/* 주어진 NAME으로 파일을 엽니다.
 * 성공 시 새로운 파일을 리턴하고, 실패 시 null을 리턴합니다. 
 * NAME 이라는 파일이 존재하지 않거나, 내부 메모리 할당이 실패한 경우 실패합니다. */
struct file *
filesys_open (const char *name) {
	struct dir *dir = dir_open_root ();
	struct inode *inode = NULL;

	if (dir != NULL)
		dir_lookup (dir, name, &inode);
	dir_close (dir);
	return file_open (inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	return success;
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create();

	/* Root Directory 생성 */
	disk_sector_t root = cluster_to_sector(ROOT_DIR_CLUSTER);
	if (!dir_create(root, 16))
		PANIC("root directory creation failed");
	fat_close();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}
