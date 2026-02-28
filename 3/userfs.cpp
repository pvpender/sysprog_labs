#include "userfs.h"

#include "rlist.h"

#include <stddef.h>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>

enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char memory[BLOCK_SIZE];
	/** A link in the block list of the owner-file. */
	rlist in_block_list = RLIST_LINK_INITIALIZER;

	/* PUT HERE OTHER MEMBERS */
};

struct file {
	/**
	 * Doubly-linked intrusive list of file blocks. Intrusiveness of the
	 * list gives you the full control over the lifetime of the items in the
	 * list without having to use double pointers with performance penalty.
	 */
	rlist blocks = RLIST_HEAD_INITIALIZER(blocks);
	/** How many file descriptors are opened on the file. */
	int refs = 0;
	/** File name. */
	std::string name;
	/** A link in the global file list. */
	rlist in_file_list = RLIST_LINK_INITIALIZER;

	/* PUT HERE OTHER MEMBERS */
	bool for_delete = false;

	size_t eof_offset = 0;
};

/**
 * Intrusive list of all files. In this case the intrusiveness of the list also
 * grants the ability to remove items from any position in O(1) complexity
 * without having to know their iterator.
 */
static rlist file_list = RLIST_HEAD_INITIALIZER(file_list);

struct filedesc {
	file *atfile;
	/* PUT HERE OTHER MEMBERS */
	open_flags flag;
	size_t block_num = 0;
	size_t offset = 0;
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static std::vector<filedesc*> file_descriptors;

enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

void
clear_file(file *file) {
	if (rlist_empty(&file->blocks)) {
		return;
	}
	
	block *block_var = NULL;
	block *next_block = NULL;

	rlist_foreach_entry_safe_reverse(block_var, &file->blocks, in_block_list, next_block) {
		delete block_var;
	}

	return;
}

int
ufs_open(const char *filename, int flags)
{
	file *openFile = NULL;
	bool noFile = true;

	rlist_foreach_entry(openFile, &file_list, in_file_list) {
		if ((openFile->name == filename) && !openFile->for_delete) {
			noFile = false;
			break;
		}
	}

	if ((rlist_empty(&file_list) || noFile) && (flags == 0)) {
		ufs_error_code = UFS_ERR_NO_FILE;

		return -1;
	}

	if ((rlist_empty(&file_list) || noFile) && (flags == UFS_CREATE)) {
		openFile = new file{.name = filename};
		rlist_add_entry(&file_list, openFile, in_file_list);
	}

	++openFile->refs;

	for (size_t i = 0; i < file_descriptors.size(); ++i) {
		if (file_descriptors.at(i) == NULL) {
			file_descriptors.at(i) = new filedesc{.atfile=openFile, .flag=(open_flags)flags};

			return i;
		}
	}

	file_descriptors.push_back(new filedesc{.atfile=openFile, .flag=(open_flags)flags});

	return file_descriptors.size() - 1;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
	if ((fd < 0) || (fd > (int)file_descriptors.size() - 1) || file_descriptors.at(fd) == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;

		return -1;
	}

	filedesc *descriptor = file_descriptors.at(fd);

	if (descriptor->flag == UFS_READ_ONLY) {
		ufs_error_code = UFS_ERR_NO_PERMISSION;

		return -1;
	}

	if ((descriptor->block_num * BLOCK_SIZE + descriptor->offset + size) > MAX_FILE_SIZE) {
		ufs_error_code = UFS_ERR_NO_MEM;

		return -1;
	}

	block *bl;
	size_t index = 0;
	size_t writed_bytes = 0;

	if (rlist_empty(&descriptor->atfile->blocks)) {
		rlist_add_tail_entry(&descriptor->atfile->blocks, new block(), in_block_list);
	}

	rlist_foreach_entry(bl, &descriptor->atfile->blocks, in_block_list) {
		if (index == descriptor->block_num) {
			if (BLOCK_SIZE < (descriptor->offset + size)) {
				memcpy(bl->memory + descriptor->offset, buf + writed_bytes, BLOCK_SIZE - descriptor->offset);
				writed_bytes += BLOCK_SIZE - descriptor->offset;
				size -= BLOCK_SIZE - descriptor->offset;
				rlist_add_tail_entry(&descriptor->atfile->blocks, new block(), in_block_list);
				descriptor->offset = 0;
				descriptor->block_num += 1;
			} else {
				memcpy(bl->memory + descriptor->offset, buf + writed_bytes, size);
				writed_bytes += size;
				descriptor->offset += size;
				break;
			}
		}

		++index;
	}

	descriptor->atfile->eof_offset = std::max(descriptor->block_num * BLOCK_SIZE + descriptor->offset, descriptor->atfile->eof_offset);

	return writed_bytes;
}

ssize_t
ufs_read(int fd, char *buf, size_t size)
{
	if ((fd < 0) || (fd > (int)file_descriptors.size() - 1) || file_descriptors.at(fd) == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;

		return -1;
	}

	filedesc *descriptor = file_descriptors.at(fd);

	if (descriptor->flag == UFS_WRITE_ONLY) {
		ufs_error_code = UFS_ERR_NO_PERMISSION;

		return -1;
	}

	if ((descriptor->block_num * BLOCK_SIZE + descriptor->offset) >= descriptor->atfile->eof_offset) {
		return 0;
	}

	block *bl;
	size_t index = 0;
	size_t readed_bytes = 0;

	rlist_foreach_entry(bl, &descriptor->atfile->blocks, in_block_list) {
		if (index == descriptor->block_num) {
			if ((descriptor->block_num * BLOCK_SIZE + descriptor->offset + size) > descriptor->atfile->eof_offset) {
				size = descriptor->atfile->eof_offset - descriptor->offset - descriptor->block_num * BLOCK_SIZE;
			}

			if (BLOCK_SIZE < (descriptor->offset + size)) {
				memcpy(buf + readed_bytes, bl->memory + descriptor->offset, BLOCK_SIZE - descriptor->offset);
				readed_bytes += BLOCK_SIZE - descriptor->offset;
				size -= BLOCK_SIZE - descriptor->offset;
				descriptor->offset = 0;
				descriptor->block_num += 1;
			} else {
				memcpy(buf + readed_bytes, bl->memory + descriptor->offset, size);
				readed_bytes += size;
				descriptor->offset += size;
				break;
			}
		}

		++index;
	}

	return readed_bytes;
}

int
ufs_close(int fd)
{
	if ((fd < 0) || (fd > (int)file_descriptors.size() - 1) || file_descriptors.at(fd) == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;

		return -1;
	}

	file *file = file_descriptors.at(fd)->atfile;

	--file->refs;
	if ((file->for_delete == true) && (file->refs == 0)) {
		rlist_del_entry(file, in_file_list);
		clear_file(file);
		delete file;
	}

	delete file_descriptors.at(fd);
	file_descriptors.at(fd) = NULL;

	return 0;
}

int
ufs_delete(const char *filename)
{
	file *fileForDelete = NULL;
	bool noFile = true;

	rlist_foreach_entry(fileForDelete, &file_list, in_file_list) {
		if ((fileForDelete->name == filename) && !fileForDelete->for_delete) {
			noFile = false;
			break;
		}
	}

	if ((rlist_empty(&file_list) || noFile)) {
		ufs_error_code = UFS_ERR_NO_FILE;

		return -1;
	}

	fileForDelete->for_delete = true;

	if (fileForDelete->refs == 0) {
		rlist_del_entry(fileForDelete, in_file_list);
		clear_file(fileForDelete);
		delete fileForDelete;
	}
	
	return 0;
}

#if NEED_RESIZE

int
ufs_resize(int fd, size_t new_size)
{
	if ((fd < 0) || (fd > (int)file_descriptors.size() - 1) || file_descriptors.at(fd) == NULL) {
		ufs_error_code = UFS_ERR_NO_FILE;

		return -1;
	}

	filedesc *descriptor = file_descriptors.at(fd);

	if (descriptor->flag == UFS_READ_ONLY) {
		ufs_error_code = UFS_ERR_NO_PERMISSION;

		return -1;
	}

	if (new_size > MAX_FILE_SIZE) {
		ufs_error_code = UFS_ERR_NO_MEM;

		return -1;
	}

	if (descriptor->atfile->eof_offset <= new_size) {
		size_t bytesToAllocation = new_size - BLOCK_SIZE * ((descriptor->atfile->eof_offset + BLOCK_SIZE - 1) / BLOCK_SIZE);
		if (bytesToAllocation <= 0) {
			return 0;
		}

		size_t blockToAdd = (bytesToAllocation + BLOCK_SIZE - 1) / BLOCK_SIZE;

		for (size_t i = 0; i < blockToAdd; ++i) {
			rlist_add_tail_entry(&descriptor->atfile->blocks, new block(), in_block_list);
		}

		return 0;
	}

	size_t currentBlocksCount = (descriptor->atfile->eof_offset + BLOCK_SIZE - 1) / BLOCK_SIZE;
	size_t newBlocksCount = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

	block *blck = NULL;
	for (size_t i = 0; i < (currentBlocksCount - newBlocksCount); ++i) {
		blck = rlist_last_entry(&descriptor->atfile->blocks, block, in_block_list);
		rlist_shift_tail(&descriptor->atfile->blocks);
		delete blck;
	}

	descriptor->atfile->eof_offset = new_size;

	for (auto fileDesk: file_descriptors) {
		if ((fileDesk != NULL) && (fileDesk->atfile->name == descriptor->atfile->name)) {
			if ((fileDesk->block_num * BLOCK_SIZE + fileDesk->offset) > new_size) {
				fileDesk->block_num = newBlocksCount - 1;
				fileDesk->offset = new_size - (newBlocksCount - 1) * BLOCK_SIZE;
			}
		}
	}

	return 0;
}

#endif

void
ufs_destroy(void)
{
	/*
	 * The file_descriptors array is likely to leak even if
	 * you resize it to zero or call clear(). This is because
	 * the vector keeps memory reserved in case more elements
	 * would be added.
	 *
	 * The recommended way of freeing the memory is to swap()
	 * the vector with a temporary empty vector.
	 */

	file *fl;

	rlist_foreach_entry_reverse(fl, &file_list, in_file_list) {
		clear_file(fl);
	}

	std::vector<filedesc*> emptyFileDesk;
	
	std::swap(file_descriptors, emptyFileDesk);
}
