#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fs.h>

void test_basic()
{
    int success;
    int fd;
    /* test fs_umount()
     */
    success = fs_umount();
    assert(success == -1);
    
    
    /* test fs_mount()
     */
    /* error checking: virtual disk file @diskname cannot be opened */
    success = fs_mount("fake.fs");
    assert(success == -1);
    success = fs_mount("disk.fs");
    assert(success != -1);
    
    
    /* test fs_create()
     */
    success = fs_create("f1.txt");
    assert(success != -1);
    /* error checking:  @filename is invalid, @filename is too long */
    success = fs_create("xiajibaluanxiezenmehaibudaosanshiergezifu.txt");
    assert(success == -1);
    success = fs_create("\0");
    assert(success == -1);
    /* error checking: a file named @filename already exists */
    success = fs_create("xiajibaluanxiezenmehaibudaosanshiergezifu.txt");
    assert(success == -1);
    
    /* test fs_open()
     */
    success = fs_open("f1.txt");
    assert(success != -1);
    /* error checking: the file doesn't exist */
    success = fs_open("NotExist.txt");
    
    /* test fs_close()
     */
    success = fs_close(0);
    /* error checking: file descriptor @fd is invalid */
    success = fs_close(33);
    assert(success == -1);
    
    
    /* test fs_delete()
     */
    success = fs_delete("f1.txt");
    assert(success == 0);
    /* error checking: no file named @filename to delete */
    success = fs_delete("NoFile.txt");
    assert(success == -1);
    /* error checking: file @filename is currently open */
    fs_create("f2.txt");
    fs_open("f2.txt");
    success = fs_delete("f2.txt");
    assert(success == -1);
    
    /* test fs_lseek()
     */
    fs_create("f1.txt");
    fd = fs_open("f1.txt");
    success = fs_lseek(fd, 0);
    assert(success == 0);
    /* error checking: file descriptor @fd is invalid */
    success = fs_lseek(33, 0);
    assert(success == -1);
    /* error checking:  @offset is out of bounds */
    success = fs_lseek(0, 4000);
    assert(success == -1);
    
    /* test fs_write() and fs_read()
     */
    char msg[] = "This is the final project!!!!";
    char buf[40];
    
    fs_create("write.txt");
    fd = fs_open("write.txt");
    fs_lseek(fd, 0);
    fs_write(fd,msg,40);
    fs_lseek(fd,0);
    fs_read(fd,buf,40);
    if(strcmp(msg,buf) == 0){
        assert(1);
    }else {
        assert(0);
    }
    
}

/* test whether fs_write and fs_read work for different offset on the same file */
void test_diff_offset_read_write()
{
    char msg[] = "Good luck on final!!!!";
    char w1[] = "pink";
    char w2[] = "Stay";
    char buf1[40], buf2[40];
    
    fs_create("diff.txt");
    int fd1 = fs_open("diff.txt");
    int fd2 = fs_open("diff.txt");
    
    fs_lseek(fd1, 0);
    fs_write(fd1,msg,40);
 
    fs_lseek(fd1, 5);
    fs_write(fd1,w1,4);
    fs_lseek(fd1,0);
    fs_read(fd1,buf1,40);
    fs_lseek(fd2, 0);
    fs_write(fd2,w2,4);
    fs_lseek(fd2,0);
    fs_read(fd2,buf2,40);
    if((strcmp("Good pink on final!!!!",buf1) == 0) && (strcmp("Stay pink on final!!!!",buf2) == 0)){
        assert(1);
    }else {
        assert(0);
    }
    
    
}

/* error checking: there are already %FS_OPEN_MAX_COUNT files currently open */
void test_max_open()
{
	int success;
	fs_mount("max_open.fs");
	fs_create("f1.txt");
	
    for (int i = 1; i < 33; i++){
    	fs_open("f1.txt");
    }
    success = fs_open("f1.txt");
    assert(success == -1);
    fs_umount();
	
}

/* error checking: the root directory already contains %FS_FILE_MAX_COUNT files */
void test_max_create()
{
	int success;
	fs_mount("max_create.fs");
	
    char fn[10];
    for (int i = 1; i < 129; i++){
        sprintf(fn, "f%d.txt", i);
        fs_create(fn);
    }
    success = fs_create("f129.txt");
    assert(success == -1);
    fs_umount();
}

int main()
{
    test_basic();
    test_diff_offset_read_write();
	test_max_open();
	test_max_create();
    
}



