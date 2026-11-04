// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: puts the home screen tiles the app made into /data/homebrew (app/forwarders.h).
//
// /data/homebrew is root's: without a HEN the app's sandbox can read it but not make folders in it.
// So the app writes each tile, whole, in /data/ps5cemu/tiles/pending/<title ID> (psfwd_write) and
// sends this payload to elfldr, the console's payload loader on 127.0.0.1:9021, which runs it as
// root. For each pending tile it:
//   - takes only a PS5 Forwarder Format folder (eboot.bin, sce_sys/param.json, forwarder.json) whose
//     forwarder.json starts PS5-ECAM (PPSA99360), under a title ID of PPSA and five digits that is
//     not PS5-ECAM's own;
//   - replaces /data/homebrew/<title ID> only when that folder is itself such a tile (the same game's
//     tile made again), never another app's folder;
//   - moves the folder in (a rename, both being on /data; a copy where they are not), gives it the
//     modes all homebrew there has (0777: eboot.bin must be executable), and writes <title ID>.result
//     beside the pending folder: "ok", or "error <errno> <what>".
// Then, for each request in /data/ps5cemu/tiles/remove (a file named by a title ID), it takes that tile
// away, only when /data/homebrew/<title ID> is itself such a tile, and answers in <title ID>.result
// beside the request. Last, it gives the same modes to every PS5-ECAM tile already in /data/homebrew
// (those of the first helper, which left eboot.bin 0666), and exits. It reads and writes nothing else.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PENDING "/data/ps5cemu/tiles/pending"
#define REMOVE "/data/ps5cemu/tiles/remove"
#define HOMEBREW "/data/homebrew"
#define TARGET "PPSA99360"

static int valid_id(const char* id)
{
	if (strlen(id) != 9 || strncmp(id, "PPSA", 4) != 0 || strcmp(id, TARGET) == 0)
		return 0;
	for (int i = 4; i < 9; i++)
		if (id[i] < '0' || id[i] > '9')
			return 0;
	return 1;
}

static int exists(const char* path)
{
	struct stat st;
	return lstat(path, &st) == 0;
}

// A forwarder.json of the format's that starts PS5-ECAM, and the tile program beside it
static int our_tile(const char* dir)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/eboot.bin", dir);
	if (!exists(path))
		return 0;
	snprintf(path, sizeof(path), "%s/sce_sys/param.json", dir);
	if (!exists(path))
		return 0;
	snprintf(path, sizeof(path), "%s/forwarder.json", dir);
	const int fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;
	static char body[64 * 1024];
	const ssize_t length = read(fd, body, sizeof(body) - 1);
	close(fd);
	if (length <= 0)
		return 0;
	body[length] = '\0';
	return strstr(body, "ps5-forwarder/") != NULL && strstr(body, "\"" TARGET "\"") != NULL;
}

// A folder and all in it (links themselves, never what they point to)
static int remove_tree(const char* path)
{
	struct stat st;
	if (lstat(path, &st) != 0)
		return errno == ENOENT ? 0 : -1;
	if (!S_ISDIR(st.st_mode))
		return unlink(path);
	DIR* dir = opendir(path);
	if (!dir)
		return -1;
	int result = 0;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL)
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		char child[1024];
		snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
		if (remove_tree(child) != 0)
			result = -1;
	}
	closedir(dir);
	return rmdir(path) == 0 ? result : -1;
}

static int copy_file(const char* from, const char* to, mode_t mode)
{
	const int in = open(from, O_RDONLY);
	if (in < 0)
		return -1;
	const int out = open(to, O_WRONLY | O_CREAT | O_TRUNC, mode & 0777);
	if (out < 0)
	{
		close(in);
		return -1;
	}
	static char buffer[256 * 1024];
	ssize_t got;
	int result = 0;
	while ((got = read(in, buffer, sizeof(buffer))) > 0)
		if (write(out, buffer, (size_t)got) != got)
		{
			result = -1;
			break;
		}
	if (got < 0)
		result = -1;
	close(in);
	if (close(out) != 0)
		result = -1;
	return result;
}

// For a pending folder on another file system than /data/homebrew
static int copy_tree(const char* from, const char* to)
{
	if (mkdir(to, 0777) != 0 && errno != EEXIST)
		return -1;
	DIR* dir = opendir(from);
	if (!dir)
		return -1;
	int result = 0;
	struct dirent* entry;
	while (result == 0 && (entry = readdir(dir)) != NULL)
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		char source[1024], target[1024];
		snprintf(source, sizeof(source), "%s/%s", from, entry->d_name);
		snprintf(target, sizeof(target), "%s/%s", to, entry->d_name);
		struct stat st;
		if (lstat(source, &st) != 0)
			result = -1;
		else if (S_ISDIR(st.st_mode))
			result = copy_tree(source, target);
		else if (S_ISREG(st.st_mode))
			result = copy_file(source, target, st.st_mode);
	}
	closedir(dir);
	return result;
}

// Everything 0777, as all the homebrew in /data/homebrew has it and as psfwd gives the tile program:
// the system starts eboot.bin only with its execute bits (4.0.0's first helper made the files 0666, and
// no tile started), and the sandboxed app that made the files must not keep anyone out of them
static void open_tree(const char* path)
{
	struct stat st;
	if (lstat(path, &st) != 0)
		return;
	if (!S_ISDIR(st.st_mode))
	{
		if (S_ISREG(st.st_mode))
			chmod(path, 0777);
		return;
	}
	chmod(path, 0777);
	DIR* dir = opendir(path);
	if (!dir)
		return;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL)
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		char child[1024];
		snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
		open_tree(child);
	}
	closedir(dir);
}

static void report(const char* folder, const char* id, int error, const char* what)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s.result", folder, id);
	const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		return;
	char line[512];
	const int length = error ? snprintf(line, sizeof(line), "error %d %s\n", error, what) : snprintf(line, sizeof(line), "ok\n");
	if (length > 0)
		(void)!write(fd, line, (size_t)length);
	fchmod(fd, 0666); // the app (not root) removes it once read
	close(fd);
}

static void install(const char* id)
{
	char from[512], to[512];
	snprintf(from, sizeof(from), "%s/%s", PENDING, id);
	snprintf(to, sizeof(to), "%s/%s", HOMEBREW, id);
	if (!our_tile(from))
	{
		report(PENDING, id, EINVAL, "not a Playtendoh tile");
		return;
	}
	if (exists(to))
	{
		if (!our_tile(to))
		{
			report(PENDING, id, EEXIST, "another app has that folder");
			return;
		}
		if (remove_tree(to) != 0)
		{
			report(PENDING, id, errno, "the tile from before could not be removed");
			return;
		}
	}
	if (mkdir(HOMEBREW, 0777) != 0 && errno != EEXIST)
	{
		report(PENDING, id, errno, "cannot make " HOMEBREW);
		return;
	}
	if (rename(from, to) == 0)
	{
		open_tree(to);
		report(PENDING, id, 0, NULL);
		return;
	}
	if (errno != EXDEV)
	{
		report(PENDING, id, errno, "cannot move the tile into " HOMEBREW);
		return;
	}
	if (copy_tree(from, to) != 0)
	{
		const int error = errno;
		remove_tree(to);
		report(PENDING, id, error ? error : EIO, "cannot copy the tile into " HOMEBREW);
		return;
	}
	remove_tree(from);
	open_tree(to);
	report(PENDING, id, 0, NULL);
}

// A tile asked to be taken away: only one of PS5-ECAM's, and one already gone is done too
static void take_away(const char* id)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/%s", HOMEBREW, id);
	if (!exists(path))
	{
		report(REMOVE, id, 0, NULL);
		return;
	}
	if (!our_tile(path))
	{
		report(REMOVE, id, EPERM, "not a Playtendoh tile");
		return;
	}
	if (remove_tree(path) != 0)
	{
		report(REMOVE, id, errno ? errno : EIO, "the tile could not be removed");
		return;
	}
	report(REMOVE, id, 0, NULL);
}

int main(void)
{
	DIR* dir = opendir(PENDING);
	if (!dir)
		return 1;
	// the names first: the folders move as they are done
	char ids[64][16];
	int count = 0;
	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL && count < 64)
		if (valid_id(entry->d_name))
		{
			char path[512];
			struct stat st;
			snprintf(path, sizeof(path), "%s/%s", PENDING, entry->d_name);
			if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode))
				snprintf(ids[count++], sizeof(ids[0]), "%s", entry->d_name);
		}
	closedir(dir);
	for (int i = 0; i < count; i++)
		install(ids[i]);
	dir = opendir(REMOVE);
	if (dir)
	{
		char removals[64][16];
		int asked = 0;
		while ((entry = readdir(dir)) != NULL && asked < 64)
			if (valid_id(entry->d_name))
				snprintf(removals[asked++], sizeof(removals[0]), "%s", entry->d_name);
		closedir(dir);
		for (int i = 0; i < asked; i++)
			take_away(removals[i]);
	}
	// and the tiles made before (4.0.0's first helper left their eboot.bin 0666, so they did not start):
	// each PS5-ECAM tile in /data/homebrew opened again, never another app's folder
	dir = opendir(HOMEBREW);
	if (dir)
	{
		while ((entry = readdir(dir)) != NULL)
			if (valid_id(entry->d_name))
			{
				char path[512];
				snprintf(path, sizeof(path), "%s/%s", HOMEBREW, entry->d_name);
				if (our_tile(path))
					open_tree(path);
			}
		closedir(dir);
	}
	return 0;
}
