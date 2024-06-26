#include "fs.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"

#define BLOCK_SIZE 4096
#define SIGNATURE_LENGTH 8
#define PADDING_SUPERBLOCK 4079
#define PADDING_ROOT 10
#define FAT_EOC 0xFFFF

struct superblock {
  char signature[SIGNATURE_LENGTH];  // Signature (should be ECS150FS)
  uint16_t total_blocks;             // total amount of blocks
  uint16_t root_index;               // root directory start index
  uint16_t data_index;               // data block start index
  uint16_t data_blocks;              // amount of data blocks
  unsigned int fat_blocks;                // number of blocks for FAT
  char padding[PADDING_SUPERBLOCK];
} __attribute__((packed));

struct root_directory_entry {
  char file_name[FS_FILENAME_LEN];  // Filename (including null char)
  uint32_t file_size;               // size of the file (bytes)
  uint16_t file_data_index;         // index of first data block
  char padding[PADDING_ROOT];                 // padding
} __attribute__((packed));

struct superblock *sblock;
uint16_t *fat;
struct root_directory_entry root_block[128];
int file_count;

int fs_mount(const char *diskname) {
  /* Open disk */
  if (block_disk_open(diskname) != 0) {
    fprintf(stderr, "Could not open the disk %s\n", diskname);
    return -1;
  }

	char sblock_buffer[BLOCK_SIZE];
  /* Load superblock into block 0 */
  if (block_read(0, sblock_buffer) == -1) {
    fprintf(stderr, "Could not read the superblock\n");
    return -1;
  }

	sblock = (struct superblock *)sblock_buffer;
  /* Verify signature */
  if (memcmp(sblock->signature, "ECS150FS", 8) != 0) {
    fprintf(stderr, "Invalid filesystem signature\n");
    return -1;
  }

	/* Calculate total number of entries in the FAT */
	size_t total_fat_entries = sblock->fat_blocks * (BLOCK_SIZE / sizeof(uint16_t));

	/* Allocate memory for the FAT array */
	fat = (uint16_t *)malloc(total_fat_entries * sizeof(uint16_t));

	/* Load FAT blocks */
	char fat_buffer[BLOCK_SIZE];
	for (int i = 0; i < sblock->fat_blocks; i++) {
		if (block_read(i+1, fat_buffer) == -1) {
		fprintf(stderr, "Cannot read FAT\n");
		return -1;
		}

		for (int j = 0; j < BLOCK_SIZE / sizeof(uint16_t); j++) {
			fat[i * (BLOCK_SIZE / sizeof(uint16_t)) + j] = ((uint16_t *)fat_buffer)[j];
		}
	}
	

  /* Load root directory */
  if (block_read(sblock->root_index, root_block) == -1) {
    fprintf(stderr, "Cannot read root directory\n");
    return -1;
  }

  /* Verify total amount of blocks */
  if (sblock->total_blocks != block_disk_count()) {
		printf("%d: sb, %d: bl", sblock->total_blocks, block_disk_count());
    fprintf(stderr, "Invalid block count\n");
    return -1;
  }

  return 0;
}

int fs_umount(void) {
  /* Write the superblock */
  if (block_write(0, sblock) < 0) {
    printf("Failed to write superblock to disk.\n");
    return -1;
  }

  /* Write the FAT */
  for (int i = 1; i <= sblock->fat_blocks; i++) {
    if (block_write(i, &fat[i - 1]) < 0) {
      printf("Failed to write FAT block %d to disk.\n", i);
      return -1;
    }
  }

  /* Write the root directory */
  if (block_write(sblock->root_index, root_block) < 0) {
    printf("Failed to write root directory to disk.\n");
    return -1;
  }

  block_disk_close();
  return 0;
}

int check_fat_avail(void){
	int count = 0;
	for (int i = 0; i < sblock -> data_blocks; i++){
		if (fat[i] == 0)
			count++;
	}
	return count;
}

int check_root_avail(void){
	int count = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if (root_block[i].file_name[0] == '\0'){
			count++;
		}
	}
	return count;
}

int fs_info(void) {
  printf("FS Info:\n");
  printf("total_blk_count=%d\n", sblock->total_blocks);
  printf("fat_blk_count=%d\n", sblock->fat_blocks);
  printf("rdir_blk=%d\n", sblock->root_index);
  printf("data_blk=%d\n", sblock->data_index);
  printf("data_blk_count=%d\n", sblock->data_blocks);
  printf("fat_free_ratio=%d/%d\n", check_fat_avail(), sblock->data_blocks);
  printf("rdir_free_ratio=%d/%d\n", check_root_avail(), FS_FILE_MAX_COUNT);

  return 0;
}

int fs_create(const char *filename) { 
	/* Check if filename is valid and not too long */
    if (filename == NULL || strlen(filename) >= FS_FILENAME_LEN) {
		fprintf(stderr, "Filename not valid");
        return -1;
    }

    /* Check if file already exists */
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (strcmp(root_block[i].file_name, filename) == 0) {
			fprintf(stderr, "Filename already exists");
            return -1;
        }
    }

	/* Check for first available spot in root directory */
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (root_block[i].file_name[0] == '\0') {
            /* Create new file */
            strcpy(root_block[i].file_name, filename);
            root_block[i].file_size = 0;
            root_block[i].file_data_index = FAT_EOC;

            /* Update root directory on disk */
            block_write(sblock -> root_index, &root_block);

            return 0;
        }
    }

}

int fs_delete(const char *filename) { 
	/* Check if filename is valid */
  	if (filename == NULL || strlen(filename) >= FS_FILENAME_LEN) {
    	return -1;
    }

    /* Find the file in root directory */
    for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (strcmp(root_block[i].file_name, filename) == 0) {
            /* Delete from FAT */
            uint16_t current_block = root_block[i].file_data_index;
            while (current_block != FAT_EOC) {
                uint16_t next_block = fat[current_block];
                fat[current_block] = 0;
                current_block = next_block;
            }

            /* Delete from root directory */
            memset(&root_block[i], 0, sizeof(struct root_directory_entry));

            /* Update root directory and FAT on disk */
            block_write(sblock -> root_index, root_block);
            for (int j = 0; j < sblock -> data_blocks / (BLOCK_SIZE / sizeof(uint16_t)); j++) {
                block_write(j+1, fat + j * (BLOCK_SIZE / sizeof(uint16_t)));
            }

            return 0;
        }
    }

    /* File not found */
	fprintf(stderr, "File not found.");
    return -1;
}

int fs_ls(void) {
	printf("FS Ls:\n");
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
    	/* Check if file_name is not empty */
    	if (root_block[i].file_name[0] != '\0') {
        	printf("file: %s, size: %d, data_blk: %d\n", root_block[i].file_name, root_block[i].file_size, root_block[i].file_data_index);
      	}
  	}

  return 0;
}

struct open_file {
  char filename[FS_FILENAME_LEN];
  size_t offset;
  int is_open;
  int fd;
};
struct open_file oft[FS_OPEN_MAX_COUNT];

int fs_open(const char *filename) { 
	/* Check if filename is valid */
  	if (filename == NULL || strlen(filename) >= FS_FILENAME_LEN) {
    	return -1;
  	}

	/* Look for the file in root directory */
	int found = -1;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (strcmp(root_block[i].file_name, filename) == 0) {
			found = i;
			break;
		}
	}

	/* If file is not found, return -1 */
	if (found == -1) {
		fprintf(stderr, "Cannot open file - no file with that name was found.");
		return -1;
	}

	/* Look for an open slot in the open file table */
 	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (oft[i].is_open == 0) {
			strcpy(oft[i].filename, filename);
			oft[i].offset = 0;
			oft[i].is_open = 1;
      oft[i].fd = found;
			return i;
    	}
	}
	return -1;

}

int fs_close(int fd) { 
	int index = -1;
	/* Check if file descriptor is valid */
	if (fd < 0 || fd >= FS_FILE_MAX_COUNT) {
		fprintf(stderr, "Invalid file descriptor.\n");
		return -1;
	}
	
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (oft[i].fd == fd) {
			index = i;
			break;
		}
	}

	/* Check if the file is open */
	if (oft[index].is_open == 0) {
		fprintf(stderr, "File not found or is open already.\n");
		return -1;
	}

	/* Close the file and return success */
	oft[index].is_open = 0;
	return 0;
}

int fs_stat(int fd) { 
  if (root_block[fd].file_name[0] == '\0') {
    return -1;
  } else {
    printf("Size of file \'%s\' is %d bytes\n", root_block[fd].file_name,
           root_block[fd].file_size);
  }

  return 0;
}

int fs_lseek(int fd, size_t offset) { 
  if (root_block[fd].file_name[0] == '\0') {
    return -1;
  }  
  
  for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
    if (oft[i].fd == fd) {
      oft[i].offset = offset;
      break;
    }
  }

	return 0;
}

int find_available_fat(void) {
  for (int i = 0; i < sblock->data_blocks; i++) {
    if (fat[i] == 0) {
      return i;
    }
  }
  return -1;
}

int fs_write(int fd, void *buf, size_t count) { 
 /* Checking if file descriptor is valid */
  if (fd < 0 || fd >= FS_OPEN_MAX_COUNT) {
      fprintf(stderr, "Invalid file descriptor.\n");
      return -1;
  }
  
  /* Checking if the file is open */
  if (oft[fd].is_open == 0) {
      fprintf(stderr, "File is not open.\n");
      return -1;
  }

  /* Checking if buf is NULL */
  if (buf == NULL) {
      fprintf(stderr, "Buffer is NULL.\n");
      return -1;
  }

  size_t bytes_write = 0;
  int index = oft[fd].fd;
  size_t offset_within_block = oft[fd].offset % BLOCK_SIZE;
  char buffer[BLOCK_SIZE];

  /* Have cur_block start at the first data block of the file */
  /* This is gathered from the root directory array */
  uint16_t current_block = root_block[index].file_data_index;

  while (bytes_write < count) {
    /* If current_block is EOC, allocate a new block */
    if (current_block == FAT_EOC) {
      int free_block = find_available_fat();
      if (free_block == -1) { /* No free blocks available */
        break;
      }
      /* if directory index is FAT_EOC, it is an empty file */
      if (root_block[index].file_data_index == FAT_EOC) {
        root_block[index].file_data_index = free_block;
      }
      /* else we continue to next block using the fat */ 
      else {
        uint16_t next_block = root_block[index].file_data_index;
        while (fat[next_block] != FAT_EOC) {
          next_block = fat[next_block];
        }
        fat[next_block] = free_block;
      }
      current_block = free_block;
      fat[current_block] = FAT_EOC;
    }

    /* read the current block into bounce buffer */
    if (block_read(sblock->data_index + current_block, buffer) < 0) {
      break;
    }

    /* calculate total amount of bytes to write */
    size_t to_write = count - bytes_write;
    /* if # bytes remaining is more than space in current block, then only */
    /* write amount that is remaining in block */
    if (to_write > BLOCK_SIZE - offset_within_block) {
      to_write = BLOCK_SIZE - offset_within_block;
    }

    /* copy from bounce buffer to user provided buffer */
    memcpy(buffer + offset_within_block, buf + bytes_write, to_write);
    /* write buffer back to block */
    if (block_write(sblock->data_index + current_block, buffer) < 0) {
      break;
    }

    /* update total amount of bytes written, new file offset, and new file size */
    bytes_write += to_write;
    oft[fd].offset += to_write;
    root_block[index].file_size += to_write;

    /* if block has been filled, move to next block and set offset to 0*/
    if (offset_within_block + to_write == BLOCK_SIZE) {
      current_block = fat[current_block];
      offset_within_block = 0;
    } else { // add what was written to offset
      offset_within_block += to_write;
    }
  }


  /* Write back root directory and FAT */
  block_write(sblock->root_index, root_block);
  for (int i = 0; i < sblock->fat_blocks; i++) {
    block_write(i+1, fat + i * (BLOCK_SIZE / sizeof(uint16_t)));
  }

  return bytes_write;
}


int fs_read(int fd, void *buf, size_t count) { 
  size_t bytes_read = 0;

  /* Checking if file descriptor is valid */
  if (fd < 0 || fd >= FS_OPEN_MAX_COUNT) {
      fprintf(stderr, "Invalid file descriptor.\n");
      return -1;
  }
  
  /* Checking if the file is open */
  if (oft[fd].is_open == 0) {
      fprintf(stderr, "File is not open.\n");
      return -1;
  }

  /* Checking if buf is NULL */
  if (buf == NULL) {
      fprintf(stderr, "Buffer is NULL.\n");
      return -1;
  }

  /* allocate memory for buffer */
  char* buffer = malloc(BLOCK_SIZE);
  /* Get root block index */
  int index = oft[fd].fd;


  /* Calculate the starting block and the offset within the block */
  /* ONLY IF OFFSET IS MORE THAN ENTIRE BLOCK */
  int start_block = oft[fd].offset / BLOCK_SIZE;
  int offset_within_block = oft[fd].offset % BLOCK_SIZE;

  /* Have cur_block start at the first data block of the file */
  /* This is gathered from the root directory array */
	int cur_block = root_block[index].file_data_index;

  /* Step through blocks until we reach the first block we want to access */
  /* If starting from first block, this loop does nothing */
  for (int i = 0; i < start_block; ++i) {
      cur_block = fat[cur_block];
  }

  while (bytes_read < count){
      /* read entire first block into bounce buffer */
      /* because data blocks start at 0, we need to skip the blocks */
      /* up until the root directory */
      block_read(sblock->data_index + cur_block, buffer);

      /* calculate bytes left to read */
      size_t to_read = BLOCK_SIZE - offset_within_block;
      /* if bytes read and to read are more than what the user asked for */
      /* reduce the amount left to read */
      if (bytes_read + to_read > count) {
          to_read = count - bytes_read;
      }

      /* now copy all the read bytes into the user provided buffer */  
      memcpy(buf + bytes_read, buffer + offset_within_block, to_read);

      /* add amount of bytes read this loop */
      bytes_read += to_read;

      /* use the FAT to move to the next block and reset the */
      /* offset within block */
      cur_block = fat[cur_block];
      offset_within_block = 0;
    }

    free(buffer);
    
    /* Update the offset for future reads/writes */
    oft[fd].offset += bytes_read;

    return bytes_read;
}