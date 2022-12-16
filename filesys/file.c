#include "filesys/file.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"


/* Opens a file for the given INODE, of which it takes ownership,
 * and returns the new file.  Returns a null pointer if an
 * allocation fails or if INODE is null. */
/* INODE에 대한 소유권을 갖고있는 파일을 열고 새로운 파일을 리턴합니다. 
 * INODE가 null 이라면 할당을 실패합니다. */
struct file *
file_open (struct inode *inode) {
	struct file *file = calloc (1, sizeof *file);
	if (inode != NULL && file != NULL) {
		file->inode = inode;
		file->pos = 0;
		file->deny_write = false;
		file->dup_count = 0;
		return file;
	} else {
		inode_close (inode);
		free (file);
		return NULL;
	}
}

/* Opens and returns a new file for the same inode as FILE.
 * Returns a null pointer if unsuccessful. */
struct file *
file_reopen (struct file *file) {
	return file_open (inode_reopen (file->inode));
}

/* Duplicate the file object including attributes and returns a new file for the
 * same inode as FILE. Returns a null pointer if unsuccessful. */
struct file *
file_duplicate (struct file *file) {
	struct file *nfile = file_open (inode_reopen (file->inode));
	if (nfile) {
		nfile->pos = file->pos;
		nfile->dup_count = file->dup_count;
		if (file->deny_write)
			file_deny_write (nfile);
	}
	return nfile;
}

/* Closes FILE. */
/* FILE을 닫습니다. */
void
file_close (struct file *file) {
	if (file != NULL) {
		file_allow_write (file);
		inode_close (file->inode);
		free (file);
	}
}

/* Returns the inode encapsulated by FILE. */
/* FILE에 의해 캡슐화된 inode를 반환합니다. */
struct inode *
file_get_inode (struct file *file) {
	return file->inode;
}

/* Reads SIZE bytes from FILE into BUFFER,
 * starting at the file's current position.
 * Returns the number of bytes actually read,
 * which may be less than SIZE if end of file is reached.
 * Advances FILE's position by the number of bytes read. */
/* 파일의 현재 위치인 FILE에서 SIZE 바이트를 BUFFER로 읽어들입니다.
 * 실제로 읽어들인 바이트 수를 리턴하며, 이는 파일의 끝에 도달했다면 SIZE보다 작을 수 있습니다. 
 * FILE의 위치를 읽어들인 바이트 수 만큼 옮김니다. */
off_t
file_read (struct file *file, void *buffer, off_t size) {
	off_t bytes_read = inode_read_at (file->inode, buffer, size, file->pos);
	file->pos += bytes_read;
	return bytes_read;
}

/* Reads SIZE bytes from FILE into BUFFER,
 * starting at offset FILE_OFS in the file.
 * Returns the number of bytes actually read,
 * which may be less than SIZE if end of file is reached.
 * The file's current position is unaffected. */
/* 파일의 현재 위치인 FILE에서 SIZE 바이트를 BUFFER로 읽어들입니다.
 * 실제로 읽어들인 바이트 수를 리턴하며, 이는 파일의 끝에 도달했다면 SIZE보다 작을 수 있습니다. 
 * file_read() 함수에서와 다르게 파일의 위치는 영향을 받지 않습니다.*/
off_t
file_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs) {
	return inode_read_at (file->inode, buffer, size, file_ofs);
}

/* Writes SIZE bytes from BUFFER into FILE,
 * starting at the file's current position.
 * Returns the number of bytes actually written,
 * which may be less than SIZE if end of file is reached.
 * (Normally we'd grow the file in that case, but file growth is
 * not yet implemented.)
 * Advances FILE's position by the number of bytes read. */
/* 파일의 현재 위치인 FILE에서 SIZE 바이트를 BUFFER로 씁니다. 
 * 실제로 쓰인 바이트 수를 리턴하며, 이는 파일의 끝에 도달했다면 SIZE보다 작을 수 있습니다. 
 * (이러한 경우 일반적으로는 파일의 크기를 키우지만, 아직 파일 growth는 구현되어있지 않습니다.)
 * FILE의 위치를 읽어들인 바이트 수 만큼 옮김니다. */
off_t
file_write (struct file *file, const void *buffer, off_t size) {
	off_t bytes_written = inode_write_at (file->inode, buffer, size, file->pos);
	file->pos += bytes_written;
	return bytes_written;
}

/* 물리 프레임에 변경된 데이터를 다시 디스크 파일에 업데이트해주는 함수. buffer에 있는 데이터를 size만큼, file의 file_ofs부터 써준다. */
/* Writes SIZE bytes from BUFFER into FILE,
 * starting at offset FILE_OFS in the file.
 * Returns the number of bytes actually written,
 * which may be less than SIZE if end of file is reached.
 * (Normally we'd grow the file in that case, but file growth is
 * not yet implemented.)
 * The file's current position is unaffected. */
off_t
file_write_at (struct file *file, const void *buffer, off_t size,
		off_t file_ofs) {
	return inode_write_at (file->inode, buffer, size, file_ofs);
}

/* Prevents write operations on FILE's underlying inode
 * until file_allow_write() is called or FILE is closed. */
void
file_deny_write (struct file *file) {
	ASSERT (file != NULL);
	if (!file->deny_write) {
		file->deny_write = true;
		inode_deny_write (file->inode);
	}
}

/* Re-enables write operations on FILE's underlying inode.
 * (Writes might still be denied by some other file that has the
 * same inode open.) */
/* FILE의 기본 inode에서 쓰기 작업을 다시 활성화합니다. 
 * 같은 inode를 열고 있는 다른 파일에 의해 쓰기 동작은 여전히 거부될 수 있습니다. */
void
file_allow_write (struct file *file) {
	ASSERT (file != NULL);
	if (file->deny_write) {
		file->deny_write = false;
		inode_allow_write (file->inode);
	}
}

/* Returns the size of FILE in bytes. */
off_t
file_length (struct file *file) {
	ASSERT (file != NULL);
	return inode_length (file->inode);
}

/* Sets the current position in FILE to NEW_POS bytes from the
 * start of the file. */
void
file_seek (struct file *file, off_t new_pos) {
	ASSERT (file != NULL);
	ASSERT (new_pos >= 0);
	file->pos = new_pos;
}

/* Returns the current position in FILE as a byte offset from the
 * start of the file. */
/* 파일 시작에서 바이트 오프셋으로 FILE의 현재 위치를 반환합니다.*/
off_t
file_tell (struct file *file) {
	ASSERT (file != NULL);
	return file->pos;
}
