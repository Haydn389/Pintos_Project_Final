/* Grows a file from 0 bytes to 2,134 bytes, 37 bytes at a time,
   and checks that the file's size is reported correctly at each
   step. */

#include <syscall.h>
#include "tests/filesys/seq-test.h"
#include "tests/lib.h"
#include "tests/main.h"

// #include "include/userprog/syscall.h"

static char buf[2134];

static size_t
return_block_size (void) 
{
  return 37;
}

static void
check_file_size (int fd, long ofs) 
{
  // printf("**********test_fd : %d\n",fd);
  // printf("**********test_file : %p\n",find_file(fd));
  long size = filesize (fd);
  if (size != ofs)
    fail ("filesize not updated properly: should be %ld, actually %ld",
          ofs, size);
}

void
test_main (void) 
{
  seq_test ("testfile",
            buf, sizeof buf, 0,
            return_block_size, check_file_size);
}
