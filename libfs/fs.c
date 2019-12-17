#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF

/* super block data structure */
struct superblock{
    char signature[8];
    uint16_t virtual_disk_amount;
    uint16_t root_index;
    uint16_t data_start_index;
    uint16_t data_amount;
    uint8_t FAT_amount;
    uint8_t padding[4079];
}__attribute__((packed));

typedef struct superblock* superblock_t;

/* root directory data structure */
struct rootdir{
    char filename[16];
    uint32_t file_size;
    uint16_t first_index;
    uint8_t padding[10];
}__attribute__((packed));

typedef struct rootdir* rootdir_t;

/* open file table data structure
 * @filename: corresponding file name
 * @open_count: the number of opening times of the file
 * @root_index: the root directory index of this file
 */
struct open_file{
    char filename[16];
    uint8_t open_count;
    uint8_t root_index;
}__attribute__((packed));

typedef struct open_file* open_file_t;

/* file descriptor table data structure
 * @open_file_index: the cooresponding open file table's index
 * @offset: the offset of this specific file
 */
struct descriptor{
    int8_t open_file_index;
    uint32_t offset;
}__attribute__((packed));

typedef struct descriptor* descriptor_t;

/* different types of block
 * @First: the latter part of the block, containing the end of the block
 * @Middle: the whole block
 * @Last: the former part of the block, containing the beginning of the block
 * @Short: only middle part of the block, neither containing the beginning nor the end of the block
 */
enum block_type{
    First,
    Middle,
    Last,
    Short
};

rootdir_t root = NULL;
superblock_t super_block = NULL;
uint16_t* FAT = NULL;
uint8_t mounted = 0;
descriptor_t descriptor_table = NULL;
open_file_t file_table = NULL;

/* error checking whether the super block read from the disk is validate */
int error_check(void)
{
    if(strncmp(super_block->signature, "ECS150FS", 8))
        return -1;
    if(super_block->virtual_disk_amount != block_disk_count())
        return -1;
    if(super_block->root_index != super_block->FAT_amount + 1)
        return -1;
    if(super_block->data_start_index != super_block->root_index + 1)
        return -1;
    if(super_block->data_amount != block_disk_count() - super_block->FAT_amount - 2)
        return -1;
    if(super_block->FAT_amount != (((2 *super_block->data_amount) + BLOCK_SIZE - 1) / BLOCK_SIZE))
        return -1;
    return 0;
}

/* check whether all files are closed */
int descriptor_check(void)
{
    for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){
        if(descriptor_table[i].open_file_index != -1)
            return -1;
    }
    return 0;
}

void release_space(void)
{
    free(FAT);
    free(root);
    free(descriptor_table);
    free(file_table);
    free(super_block);
}

/* find whether the specific file is open */
int file_is_open(const char *filename)
{
    for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){
        if(!strcmp(file_table[i].filename, filename))
            return i;
    }
    return -1;
}

/* reset the entry of open file table based on giving */
void reset_file(int index, const char* filename, uint8_t open_count, uint8_t root_index)
{
    strcpy(file_table[index].filename, filename);
    file_table[index].open_count = open_count;
    file_table[index].root_index = root_index;
}

/* reset the entry of file descriptor table based on giving */
void reset_descriptor(int index, uint32_t offset, int8_t open_file_index)
{
    descriptor_table[index].offset = offset;
    descriptor_table[index].open_file_index = open_file_index;
}

void initialize_descriptor_table(void)
{
    for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){
        reset_descriptor(i, 0, -1);
        reset_file(i, "\0", 0, FS_FILE_MAX_COUNT);
    }
}

int fs_mount(const char *diskname)
{
    super_block = (superblock_t)malloc(sizeof(struct superblock));
    
    /* error checking: virtual disk file @diskname cannot be opened */
    if(block_disk_open(diskname))
        return -1;
    if(block_read(0, super_block))
        return -1;
    /* error checking: no valid file system can be located */
    if(error_check())
        return -1;
    
    FAT = (uint16_t*)malloc(BLOCK_SIZE * (super_block->FAT_amount) * sizeof(uint16_t));
    root = (rootdir_t)malloc(FS_FILE_MAX_COUNT * sizeof(struct rootdir));
    descriptor_table = (descriptor_t)malloc(FS_OPEN_MAX_COUNT * sizeof(struct descriptor));
    file_table = (open_file_t)malloc(FS_OPEN_MAX_COUNT * sizeof(struct open_file));
    
    for(int i = 1; i <= super_block->FAT_amount; i++){
        if(block_read(i, (FAT + ((i - 1) * BLOCK_SIZE))))
            return -1;
    }
    if(block_read(super_block->root_index, root))
        return -1;
    
    initialize_descriptor_table();
    mounted = 1;
    return 0;
}

int fs_umount(void)
{
    /* error checking: no underlying virtual disk was opened */
    if(!mounted)
        return -1;
    /* error checking: there are still open file descriptors */
    if(descriptor_check())
        return -1;
    
    /* write back to disk */
    if(block_write(0, super_block))
        return -1;
    for(int i = 1; i <= super_block->FAT_amount; i++){
        if(block_write(i, (FAT + ((i - 1) * BLOCK_SIZE))))
            return -1;
    }
    if(block_write(super_block->root_index, root))
        return -1;
    
    /* error checking: the virtual disk cannot be closed */
    if(block_disk_close())
        return -1;
    mounted = 0;
    release_space();
    return 0;
}

int get_empty_block_num(void){
    int empty_fat = 0;
    for(int i = 0; i < super_block->data_amount; i++){
        if(FAT[i] == 0)
            empty_fat++;
    }
    return empty_fat;
}

int get_empty_dir_num(void){
    int empty_dir = 0;
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
        if(!strcmp(root[i].filename, "\0"))
            empty_dir++;
    }
    return empty_dir;
}

int get_empty_block(void){
    for(int i = 0; i < super_block->data_amount; i++){
        if(FAT[i] == 0)
            return i;
    }
    return -1;
}

int get_empty_fd(void){
    for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){
        if(descriptor_table[i].open_file_index == -1)
            return i;
    }
    return -1;
}

int get_empty_open_file(void){
    for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){
        if(!strcmp(file_table[i].filename, "\0"))
            return i;
    }
    return -1;
}

int get_dir(const char *filename)
{
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
        if(strcmp(root[i].filename, filename) == 0)
            return i;
    }
    return -1;
}

int fs_info(void)
{
    /* error checking: no underlying virtual disk was opened */
    if(!mounted)
        return -1;
    printf("FS Info:\n");
    printf("total_blk_count=%d\n", super_block->virtual_disk_amount);
    printf("fat_blk_count=%d\n", super_block->FAT_amount);
    printf("rdir_blk=%d\n", super_block->root_index);
    printf("data_blk=%d\n", super_block->data_start_index);
    printf("data_blk_count=%d\n", super_block->data_amount);
    printf("fat_free_ratio=%d/%d\n", get_empty_block_num(), super_block->data_amount);
    printf("rdir_free_ratio=%d/%d\n", get_empty_dir_num(), FS_FILE_MAX_COUNT);
    return 0;
}

/* check the validity of file name */
int check_file(const char *filename)
{
    int f_len = 0;
    if(!mounted)
        return -1;
    if(strcmp(filename, "\0") == 0)
        return -1;
    for(f_len = 0; f_len < FS_FILENAME_LEN; f_len++){
        if(filename[f_len] == '\0')
            break;
    }
    if(f_len >= FS_FILENAME_LEN)
        return -1;
    return 0;
}

void free_FAT(uint16_t index)
{
    uint16_t next_index = FAT_EOC;
    while(FAT[index] != FAT_EOC){
        next_index = FAT[index];
        FAT[index] = 0;
        index = next_index;
    }
    FAT[index] = 0;
}

int fs_create(const char *filename)
{
    /* error checking:  @filename is invalid, @filename is too long */
    if(check_file(filename))
        return -1;
    /* error checking: the root directory already contains %FS_FILE_MAX_COUNT files */
    if(get_empty_dir_num() <= 0)
        return -1;
    /* error checking: a file named @filename already exists */
    if(get_dir(filename) != -1)
        return -1;
    
    /* find first empty data block and root directory spot */
    int empty_blk = get_empty_block();
    if(empty_blk == -1)
        return -1;
    int empty_dir = get_dir("\0");
    
    strcpy(root[empty_dir].filename, filename);
    root[empty_dir].file_size = 0;
    root[empty_dir].first_index = empty_blk;
    FAT[empty_blk] = FAT_EOC;
    return 0;
}

int fs_delete(const char *filename)
{
    /* error checking: @filename is invalid */
    if(check_file(filename))
        return -1;
    /* error checking: no file named @filename to delete */
    if(get_dir(filename) == -1)
        return -1;
    /* error checking: file @filename is currently open */
    if(file_is_open(filename) != -1)
        return -1;
    int file_dir = get_dir(filename);
    strcpy(root[file_dir].filename, "\0");
    free_FAT(root[file_dir].first_index);
    return 0;
}

int fs_ls(void)
{
    /* error checking: no underlying virtual disk was opened */
    if(!mounted)
        return -1;
    
    printf("FS LS:\n");
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
        if(strcmp(root[i].filename,"\0") != 0){
            printf("file: %s, size: %d, ", root[i].filename, root[i].file_size);
            printf("data_blk: %d\n", root[i].first_index);
        }
    }
    return 0;
}

/* check the validaty of fd */
int check_fd(int fd)
{
    if(!mounted)
        return -1;
    if((fd >= FS_OPEN_MAX_COUNT) || (fd < 0))
        return -1;
    int open_file_index = descriptor_table[fd].open_file_index;
    if((open_file_index >= FS_OPEN_MAX_COUNT) || (open_file_index < 0))
        return -1;
    if(!strcmp(file_table[open_file_index].filename, "\0"))
        return -1;
    return open_file_index;
}

int fs_open(const char *filename)
{
    /* error checking: @filename is invalid */
    if(check_file(filename))
        return -1;
    /* error checking: there is no file named @filename to open */
    if(get_dir(filename) == -1)
        return -1;
    int fd = get_empty_fd();
    int open_file_index = file_is_open(filename);
    /* error checking: there are already %FS_OPEN_MAX_COUNT files currently open */
    if(fd == -1)
        return -1;
    if(open_file_index != -1){
        /* if the file is already opened before, set a new file descriptor */
        file_table[open_file_index].open_count++;
        reset_descriptor(fd, 0, open_file_index);
    } else {
        /* if the file is not opened before, set new open file entry and descriptor */
        open_file_index = get_empty_open_file();
        if(open_file_index != -1){
            reset_file(open_file_index, filename, 1, get_dir(filename));
            reset_descriptor(fd, 0, open_file_index);
        } else {
            return -1;
        }
    }
    return fd;
}

int fs_close(int fd)
{
    /* error checking: file descriptor @fd is invalid */
    int open_file_index = check_fd(fd);
    if (open_file_index == -1)
        return -1;
    reset_descriptor(fd, 0, -1);
    /* if there is no opening descriptor of this file, delete the open file entry */
    if((--file_table[open_file_index].open_count) <= 0)
        reset_file(open_file_index, "\0", 0, FS_FILE_MAX_COUNT);
    return 0;
}

int fs_stat(int fd)
{
    /* error checking: file descriptor @fd is invalid */
    int open_file_index = check_fd(fd);
    if (open_file_index == -1)
        return -1;
    int root_index = file_table[open_file_index].root_index;
    return root[root_index].file_size;
}

/* check whether the offset is validate */
int check_offset(int fd, size_t offset)
{
    int file_size = fs_stat(fd);
    if(file_size < 0)
        return -1;
    if((offset < 0) || (offset > file_size))
        return -1;
    return 0;
}

int fs_lseek(int fd, size_t offset)
{
    /* error checking: file descriptor @fd is invalid */
    if (check_fd(fd) == -1)
        return -1;
    /* error checking:  @offset is out of bounds */
    if(check_offset(fd, offset))
        return -1;
    
    descriptor_table[fd].offset = offset;
    return 0;
}

int get_block_index_by_offset(int fd, size_t offset)
{
    int fd_index = descriptor_table[fd].open_file_index;
    int root_index = file_table[fd_index].root_index;
    int block_index = root[root_index].first_index;
    size_t block_num = offset / BLOCK_SIZE;
    while(block_num > 0){
        block_index = FAT[block_index];
        block_num--;
    }
    return block_index;
}

/* update file size and offset after writing */
void update_size(int fd, size_t count){
    int file_size = fs_stat(fd);
    int fd_index = descriptor_table[fd].open_file_index;
    int root_index = file_table[fd_index].root_index;
    size_t offset = descriptor_table[fd].offset;
    
    if(offset + count > file_size)
        root[root_index].file_size = offset + count;
    descriptor_table[fd].offset += count;
}

/* allocate new data block for the file if there isn't enough space for writing */
int allocate_new_block(int fd, size_t written_size)
{
    int fd_index = descriptor_table[fd].open_file_index;
    int root_index = file_table[fd_index].root_index;
    int block_index = root[root_index].first_index;
    size_t offset = descriptor_table[fd].offset;
    size_t new_block_num = (offset + written_size - 1) / BLOCK_SIZE;
    int new_block_index;
    
    while(new_block_num > 0){
        if(FAT[block_index] == FAT_EOC){
            new_block_index = get_empty_block();
            if(new_block_index == -1)
                return -1;
            FAT[block_index] = new_block_index;
            FAT[new_block_index] = FAT_EOC;
        } else {
            new_block_index = FAT[block_index];
        }
        block_index = new_block_index;
        new_block_num--;
    }
    return 0;
}

void read_by_blk(int blk_index, void *buf, size_t read_size, enum block_type type, int fd)
{
    void* my_buf = malloc(BLOCK_SIZE);
    block_read(blk_index, my_buf);
    switch(type){
        /* read the latter part of block into buffer*/
        case First:
            memcpy(buf, my_buf + BLOCK_SIZE - read_size, read_size);
            break;
        /* read the whole block into buffer*/
        case Middle:
            memcpy(buf, my_buf, BLOCK_SIZE);
            break;
        /* read the former part of block into buffer*/
        case Last:
            memcpy(buf, my_buf, read_size);
            break;
        /* read only middle part of block into buffer*/
        case Short:
            memcpy(buf, my_buf + descriptor_table[fd].offset % BLOCK_SIZE, read_size);
            break;
    }
    free(my_buf);
}

size_t read_blks(int fd, void *buf, size_t read_size)
{
    size_t data_amount = read_size;
    int first_block_amount = BLOCK_SIZE - (descriptor_table[fd].offset % BLOCK_SIZE);
    int current_block = get_block_index_by_offset(fd, descriptor_table[fd].offset);
    void* buf_index = buf;
    
    /* if the reading data is among a block and not reach the end of the block */
    if(read_size <= first_block_amount){
        read_by_blk(super_block->data_start_index + current_block, buf_index, read_size, Short, fd);
        descriptor_table[fd].offset += read_size;
        return read_size;
    /* if the reading data is across multiple blocks */
    } else {
        /* read the former part of block */
        read_by_blk(super_block->data_start_index + current_block, buf_index, first_block_amount, First, fd);
        current_block = FAT[current_block];
        buf_index += first_block_amount;
        data_amount -= first_block_amount;
        /* read the whole blocks */
        while(data_amount > BLOCK_SIZE){
            read_by_blk(super_block->data_start_index + current_block, buf_index, BLOCK_SIZE, Middle, fd);
            current_block = FAT[current_block];
            buf_index += BLOCK_SIZE;
            data_amount -= BLOCK_SIZE;
        }
        /* read the remaining part of block */
        read_by_blk(super_block->data_start_index + current_block, buf_index, data_amount, Last, fd);
        descriptor_table[fd].offset += read_size;
        return read_size;
    }
}

void write_by_blk(int blk_index, void *buf, size_t write_size, enum block_type type, int fd)
{
    void* my_buf = malloc(BLOCK_SIZE);
    block_read(blk_index, my_buf);
    switch(type){
        /* read the latter part of block into buffer*/
        case First:
            memcpy(my_buf + BLOCK_SIZE - write_size, buf, write_size);
            break;
        /* read the whole block into buffer*/
        case Middle:
            memcpy(my_buf, buf, BLOCK_SIZE);
            break;
        /* read the former part of block into buffer*/
        case Last:
            memcpy(my_buf, buf, write_size);
            break;
        /* read only middle part of block into buffer*/
        case Short:
            memcpy(my_buf + descriptor_table[fd].offset % BLOCK_SIZE, buf, write_size);
            break;
    }
    block_write(blk_index, my_buf);
    free(my_buf);
}

size_t write_blks(int fd, void *buf, size_t write_size)
{
    size_t data_amount = write_size;
    int first_block_amount = BLOCK_SIZE - (descriptor_table[fd].offset % BLOCK_SIZE);
    int current_block = get_block_index_by_offset(fd, descriptor_table[fd].offset);
    void* buf_index = buf;
    
    /* if the written part is among a block and not reach the end of the block */
    if(write_size <= first_block_amount){
        write_by_blk(super_block->data_start_index + current_block, buf_index, write_size, Short, fd);
        update_size(fd, write_size);
        return write_size;
    /* if the writing data is across multiple blocks */
    } else {
        /* write the former part of block */
        write_by_blk(super_block->data_start_index + current_block, buf_index, first_block_amount, First, fd);
        current_block = FAT[current_block];
        buf_index += first_block_amount;
        data_amount -= first_block_amount;
        /* write the whole blocks */
        while(data_amount > BLOCK_SIZE){
            /* if the underlying disk runs out of space, write as many bytes as possible */
            if(current_block == FAT_EOC){
                update_size(fd, write_size - data_amount);
                return write_size - data_amount;
            }
            write_by_blk(super_block->data_start_index + current_block, buf_index, BLOCK_SIZE, Middle, fd);
            current_block = FAT[current_block];
            buf_index += BLOCK_SIZE;
            data_amount -= BLOCK_SIZE;
        }
        /* write the remaining part of block */
        write_by_blk(super_block->data_start_index + current_block, buf_index, data_amount, Last, fd);
        update_size(fd, write_size);
        return write_size;
    }
}


int fs_write(int fd, void *buf, size_t count)
{
    /* error checking: file descriptor @fd is invalid */
    int open_file_index = check_fd(fd);
    if (open_file_index == -1)
        return -1;
    
    allocate_new_block(fd, count);
    return write_blks(fd, buf, count);
}

int fs_read(int fd, void *buf, size_t count)
{
    int file_size = fs_stat(fd);
    /* error checking: file descriptor @fd is invalid */
    int open_file_index = check_fd(fd);
    if (open_file_index == -1)
        return -1;
    
    int read_size = 0;
    /* if the offset is larger than the file size, nothing can be read */
    if(descriptor_table[fd].offset >= file_size){
        return read_size;
    }
    /* if the part of reading data is within file, reading size is just the count */
    else if(descriptor_table[fd].offset + count <= file_size){
        read_size = count;
    }
    /* if the part of reading data is beyond file, only can read the remaining of file from offset */
    else{
        read_size = file_size - descriptor_table[fd].offset;
    }
    return read_blks(fd, buf, read_size);
}





