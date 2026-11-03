/*
 * emu_fs.c -- Zephyr's file-system API over an in-memory FAT-like tree.
 *
 * Wakes mounts its eMMC at "/EMMC:" and keeps WAKES/CNFG, WAKES/LOGS and its PRST slots
 * there. Here the volume is a list of paths, compared without case as FAT does. Every
 * access still goes through the disk Wakes registered (sp1_store.c's d_read), so its
 * card lock and ownership rules (Wakes vs. the USB host) still decide what succeeds.
 *
 * The host reads and writes the same tree (emu_fs_*), which is how the browser shows the
 * WAKES drive and how it survives a power cycle.
 */
#include "emu.h"

#include <ctype.h>
#include <stdlib.h>

#define MNT        "/EMMC:"
#define MAX_NODES  1024
#define MAX_OPEN   24
#define CLUSTER    32768u
#define VOL_BLOCKS 7634944u            /* a 4 GB eMMC's user area, in 512-byte blocks */

struct node {
	char    *path;      /* "/WAKES/CNFG/midi.cnfg"; NULL = free slot */
	int      is_dir;
	uint8_t *data;
	uint32_t size, cap;
};
struct handle { int node; uint32_t pos; uint32_t flags; int used; };
struct dirh   { char path[256]; int next; int used; };

static struct node nodes[MAX_NODES];
static struct handle handles[MAX_OPEN];
static struct dirh dirs[4];
static int formatted = 1;
static int mounted;
static uint32_t generation;
static struct disk_info *disks;

/* ------------------------------------------------------------------ paths */

static int norm(const char *in, char *out, size_t n)
{
	if (strncmp(in, MNT, strlen(MNT)) == 0) {
		in += strlen(MNT);
	} else if (in[0] == '/' && in[1] != '\0' && strchr(in, ':')) {
		return -ENOENT;               /* another mount point */
	}
	size_t j = 0;
	if (in[0] != '/') {
		out[j++] = '/';
	}
	for (size_t i = 0; in[i] && j + 1 < n; i++) {
		if (in[i] == '/' && j > 0 && out[j - 1] == '/') {
			continue;
		}
		out[j++] = in[i];
	}
	while (j > 1 && out[j - 1] == '/') {
		j--;
	}
	out[j] = '\0';
	return 0;
}

static int same(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
			return 0;
		}
	}
	return *a == *b;
}

static int find(const char *p)
{
	for (int i = 0; i < MAX_NODES; i++) {
		if (nodes[i].path && same(nodes[i].path, p)) {
			return i;
		}
	}
	return -1;
}

static int parent_ok(const char *p)
{
	const char *slash = strrchr(p, '/');
	if (!slash || slash == p) {
		return 1;                       /* in the root */
	}
	char par[256];
	size_t n = (size_t)(slash - p);
	if (n >= sizeof(par)) {
		return 0;
	}
	memcpy(par, p, n);
	par[n] = '\0';
	const int i = find(par);
	return i >= 0 && nodes[i].is_dir;
}

static int add(const char *p, int is_dir)
{
	for (int i = 0; i < MAX_NODES; i++) {
		if (!nodes[i].path) {
			nodes[i].path = strdup(p);
			nodes[i].is_dir = is_dir;
			nodes[i].data = NULL;
			nodes[i].size = nodes[i].cap = 0;
			generation++;
			return i;
		}
	}
	return -ENOSPC;
}

static void drop(int i)
{
	free(nodes[i].path);
	free(nodes[i].data);
	memset(&nodes[i], 0, sizeof(nodes[i]));
	generation++;
}

static int reserve(struct node *nd, uint32_t want)
{
	if (want <= nd->cap) {
		return 0;
	}
	uint32_t cap = nd->cap ? nd->cap : 256u;
	while (cap < want) {
		cap *= 2u;
	}
	uint8_t *d = realloc(nd->data, cap);
	if (!d) {
		return -ENOSPC;
	}
	memset(d + nd->cap, 0, cap - nd->cap);
	nd->data = d;
	nd->cap = cap;
	return 0;
}

/* ------------------------------------------------------------------ the disk underneath */

int disk_access_register(struct disk_info *d)
{
	for (struct disk_info *x = disks; x; x = x->emu_next) {
		if (x == d) {
			return 0;
		}
	}
	d->emu_next = disks;
	disks = d;
	return 0;
}

int disk_access_unregister(struct disk_info *d)
{
	for (struct disk_info **pp = &disks; *pp; pp = &(*pp)->emu_next) {
		if (*pp == d) {
			*pp = d->emu_next;
			return 0;
		}
	}
	return -EINVAL;
}

static struct disk_info *volume_disk(void)
{
	for (struct disk_info *d = disks; d; d = d->emu_next) {
		if (strcmp(d->name, CONFIG_FS_FATFS_CUSTOM_MOUNT_POINTS) == 0) {
			return d;
		}
	}
	return NULL;
}

/* FAT touches the card on every access; so do we, through Wakes' own disk callbacks. */
static int touch_card(void)
{
	static uint8_t scratch[512];
	struct disk_info *d = volume_disk();
	if (!d || !d->ops || !d->ops->read) {
		return -EIO;
	}
	return d->ops->read(d, scratch, 0, 1) == 0 ? 0 : -EIO;
}

static int ready(void)
{
	if (!mounted) {
		return -ENOENT;
	}
	return touch_card();
}

/* ------------------------------------------------------------------ mount */

int fs_mount(struct fs_mount_t *mp)
{
	if (!mp || strcmp(mp->mnt_point, MNT) != 0) {
		return -EINVAL;
	}
	if (mounted) {
		return -EBUSY;
	}
	struct disk_info *d = volume_disk();
	if (!d || !d->ops) {
		return -ENODEV;
	}
	if (d->ops->init && d->ops->init(d) != 0) {
		return -EIO;
	}
	if (d->ops->status && d->ops->status(d) != DISK_STATUS_OK) {
		return -EIO;
	}
	if (!formatted) {
		return -ENODEV;                /* FR_NO_FILESYSTEM */
	}
	if (touch_card() != 0) {
		return -EIO;
	}
	if (mp->fs_data) {
		FATFS *f = (FATFS *)mp->fs_data;
		f->database = 8192u + 2u * 1000u;
		f->csize = CLUSTER / 512u;
	}
	mounted = 1;
	return 0;
}

int fs_unmount(struct fs_mount_t *mp)
{
	ARG_UNUSED(mp);
	if (!mounted) {
		return -EINVAL;
	}
	mounted = 0;
	memset(handles, 0, sizeof(handles));
	memset(dirs, 0, sizeof(dirs));
	return 0;
}

int fs_mkfs(int fs_type, uintptr_t dev_id, void *cfg, int flags)
{
	ARG_UNUSED(fs_type); ARG_UNUSED(dev_id); ARG_UNUSED(cfg); ARG_UNUSED(flags);
	struct disk_info *d = volume_disk();
	if (!d || !d->ops || !d->ops->write) {
		return -ENODEV;
	}
	/* The boot sector a FAT32 mkfs leaves: Wakes reads it back and patches its
	 * hidden-sectors field before writing the MBR (sp1_store.c, format_card). */
	uint8_t bs[512] = { 0xEB, 0x58, 0x90 };
	memcpy(&bs[3], "MSDOS5.0", 8);
	bs[11] = 0x00; bs[12] = 0x02;              /* 512 bytes per sector  */
	bs[13] = (uint8_t)(CLUSTER / 512u);
	bs[14] = 32;                               /* reserved sectors      */
	bs[16] = 2;                                /* FATs                  */
	bs[21] = 0xF8;
	memcpy(&bs[82], "FAT32   ", 8);
	bs[510] = 0x55; bs[511] = 0xAA;
	if (d->ops->write(d, bs, 0, 1) != 0) {
		return -EIO;
	}
	emu_fs_clear();
	formatted = 1;
	return 0;
}

/* ------------------------------------------------------------------ files */

int fs_open(struct fs_file_t *zfp, const char *name, uint32_t flags)
{
	int rc = ready();
	if (rc) {
		return rc;
	}
	char p[256];
	if (norm(name, p, sizeof(p))) {
		return -ENOENT;
	}
	int i = find(p);
	if (i < 0) {
		if (!(flags & FS_O_CREATE)) {
			return -ENOENT;
		}
		if (!parent_ok(p)) {
			return -ENOENT;
		}
		i = add(p, 0);
		if (i < 0) {
			return i;
		}
	} else if (nodes[i].is_dir) {
		return -EISDIR;
	}
	int h = -1;
	for (int k = 0; k < MAX_OPEN; k++) {
		if (!handles[k].used) { h = k; break; }
	}
	if (h < 0) {
		return -ENFILE;
	}
	handles[h] = (struct handle){ i, 0, flags, 1 };
	if (flags & FS_O_TRUNC) {
		nodes[i].size = 0;
		generation++;
	}
	zfp->filep = &handles[h];
	zfp->flags = flags;
	return 0;
}

static struct handle *H(struct fs_file_t *zfp)
{
	struct handle *h = (struct handle *)zfp->filep;
	return (h && h->used && nodes[h->node].path) ? h : NULL;
}

int fs_close(struct fs_file_t *zfp)
{
	struct handle *h = (struct handle *)zfp->filep;
	if (!h || !h->used) {
		return -EBADF;
	}
	h->used = 0;
	zfp->filep = NULL;
	return 0;
}

ssize_t fs_read(struct fs_file_t *zfp, void *ptr, size_t size)
{
	struct handle *h = H(zfp);
	if (!h) { return -EBADF; }
	if (!(h->flags & FS_O_READ)) { return -EACCES; }
	if (ready()) { return -EIO; }
	struct node *nd = &nodes[h->node];
	if (h->pos >= nd->size) {
		return 0;
	}
	size_t n = nd->size - h->pos;
	if (n > size) { n = size; }
	memcpy(ptr, nd->data + h->pos, n);
	h->pos += (uint32_t)n;
	return (ssize_t)n;
}

ssize_t fs_write(struct fs_file_t *zfp, const void *ptr, size_t size)
{
	struct handle *h = H(zfp);
	if (!h) { return -EBADF; }
	if (!(h->flags & FS_O_WRITE)) { return -EACCES; }
	if (ready()) { return -EIO; }
	struct node *nd = &nodes[h->node];
	if (h->flags & FS_O_APPEND) {
		h->pos = nd->size;
	}
	if (reserve(nd, h->pos + (uint32_t)size)) {
		return -ENOSPC;
	}
	memcpy(nd->data + h->pos, ptr, size);
	h->pos += (uint32_t)size;
	if (h->pos > nd->size) {
		nd->size = h->pos;
	}
	generation++;
	return (ssize_t)size;
}

int fs_seek(struct fs_file_t *zfp, long off, int whence)
{
	struct handle *h = H(zfp);
	if (!h) { return -EBADF; }
	long base = (whence == FS_SEEK_SET) ? 0 : (whence == FS_SEEK_CUR) ? (long)h->pos
		  : (long)nodes[h->node].size;
	long np = base + off;
	if (np < 0) {
		return -EINVAL;
	}
	if ((uint32_t)np > nodes[h->node].size && !(h->flags & FS_O_WRITE)) {
		return -EINVAL;
	}
	h->pos = (uint32_t)np;
	return 0;
}

long fs_tell(struct fs_file_t *zfp)
{
	struct handle *h = H(zfp);
	return h ? (long)h->pos : -EBADF;
}

int fs_truncate(struct fs_file_t *zfp, long length)
{
	struct handle *h = H(zfp);
	if (!h) { return -EBADF; }
	if (!(h->flags & FS_O_WRITE) || length < 0) { return -EINVAL; }
	struct node *nd = &nodes[h->node];
	if (reserve(nd, (uint32_t)length)) { return -ENOSPC; }
	if ((uint32_t)length > nd->size) {
		memset(nd->data + nd->size, 0, (uint32_t)length - nd->size);
	}
	nd->size = (uint32_t)length;
	generation++;
	return 0;
}

int fs_sync(struct fs_file_t *zfp)
{
	return H(zfp) ? touch_card() : -EBADF;
}

/* ------------------------------------------------------------------ names */

static int has_children(const char *p)
{
	const size_t n = strlen(p);
	for (int i = 0; i < MAX_NODES; i++) {
		if (nodes[i].path && strlen(nodes[i].path) > n && nodes[i].path[n] == '/' &&
		    strncasecmp(nodes[i].path, p, n) == 0) {
			return 1;
		}
	}
	return 0;
}

int fs_unlink(const char *path)
{
	int rc = ready();
	if (rc) { return rc; }
	char p[256];
	if (norm(path, p, sizeof(p))) { return -ENOENT; }
	const int i = find(p);
	if (i < 0) { return -ENOENT; }
	if (nodes[i].is_dir && has_children(p)) { return -ENOTEMPTY; }
	for (int k = 0; k < MAX_OPEN; k++) {
		if (handles[k].used && handles[k].node == i) { return -EBUSY; }
	}
	drop(i);
	return 0;
}

int fs_rename(const char *from, const char *to)
{
	int rc = ready();
	if (rc) { return rc; }
	char a[256], b[256];
	if (norm(from, a, sizeof(a)) || norm(to, b, sizeof(b))) { return -ENOENT; }
	const int i = find(a);
	if (i < 0) { return -ENOENT; }
	if (!parent_ok(b)) { return -ENOENT; }
	const int j = find(b);
	if (j >= 0 && j != i) {
		if (nodes[j].is_dir && has_children(b)) { return -ENOTEMPTY; }
		drop(j);
	}
	const size_t an = strlen(a);
	for (int k = 0; k < MAX_NODES; k++) {      /* a directory takes its tree with it */
		if (k != i && nodes[k].path && strlen(nodes[k].path) > an &&
		    nodes[k].path[an] == '/' && strncasecmp(nodes[k].path, a, an) == 0) {
			char np[512];
			snprintf(np, sizeof(np), "%s%s", b, nodes[k].path + an);
			free(nodes[k].path);
			nodes[k].path = strdup(np);
		}
	}
	free(nodes[i].path);
	nodes[i].path = strdup(b);
	generation++;
	return 0;
}

int fs_mkdir(const char *path)
{
	int rc = ready();
	if (rc) { return rc; }
	char p[256];
	if (norm(path, p, sizeof(p))) { return -ENOENT; }
	if (strcmp(p, "/") == 0 || find(p) >= 0) { return -EEXIST; }
	if (!parent_ok(p)) { return -ENOENT; }
	const int i = add(p, 1);
	return i < 0 ? i : 0;
}

static void fill(struct fs_dirent *e, int i)
{
	const char *base = strrchr(nodes[i].path, '/');
	snprintf(e->name, sizeof(e->name), "%s", base ? base + 1 : nodes[i].path);
	e->type = nodes[i].is_dir ? FS_DIR_ENTRY_DIR : FS_DIR_ENTRY_FILE;
	e->size = nodes[i].is_dir ? 0 : nodes[i].size;
}

int fs_stat(const char *path, struct fs_dirent *e)
{
	int rc = ready();
	if (rc) { return rc; }
	char p[256];
	if (norm(path, p, sizeof(p))) { return -ENOENT; }
	if (strcmp(p, "/") == 0) {
		e->type = FS_DIR_ENTRY_DIR;
		e->name[0] = '\0';
		e->size = 0;
		return 0;
	}
	const int i = find(p);
	if (i < 0) { return -ENOENT; }
	fill(e, i);
	return 0;
}

int fs_opendir(struct fs_dir_t *zdp, const char *path)
{
	int rc = ready();
	if (rc) { return rc; }
	char p[256];
	if (norm(path, p, sizeof(p))) { return -ENOENT; }
	if (strcmp(p, "/") != 0) {
		const int i = find(p);
		if (i < 0 || !nodes[i].is_dir) { return -ENOENT; }
	}
	for (int k = 0; k < (int)ARRAY_SIZE(dirs); k++) {
		if (!dirs[k].used) {
			snprintf(dirs[k].path, sizeof(dirs[k].path), "%s", strcmp(p, "/") ? p : "");
			dirs[k].next = 0;
			dirs[k].used = 1;
			zdp->dirp = &dirs[k];
			return 0;
		}
	}
	return -ENFILE;
}

int fs_readdir(struct fs_dir_t *zdp, struct fs_dirent *e)
{
	struct dirh *d = (struct dirh *)zdp->dirp;
	if (!d || !d->used) { return -EBADF; }
	if (ready()) { return -EIO; }
	const size_t n = strlen(d->path);
	for (; d->next < MAX_NODES; d->next++) {
		const char *np = nodes[d->next].path;
		if (np && strlen(np) > n && np[n] == '/' && strncasecmp(np, d->path, n) == 0 &&
		    strchr(np + n + 1, '/') == NULL) {
			fill(e, d->next++);
			return 0;
		}
	}
	e->name[0] = '\0';
	return 0;
}

int fs_closedir(struct fs_dir_t *zdp)
{
	struct dirh *d = (struct dirh *)zdp->dirp;
	if (!d || !d->used) { return -EBADF; }
	d->used = 0;
	zdp->dirp = NULL;
	return 0;
}

int fs_statvfs(const char *path, struct fs_statvfs *st)
{
	ARG_UNUSED(path);
	int rc = ready();
	if (rc) { return rc; }
	unsigned long used = 0;
	for (int i = 0; i < MAX_NODES; i++) {
		if (nodes[i].path) {
			used += nodes[i].is_dir ? 1u : (nodes[i].size + CLUSTER - 1u) / CLUSTER;
		}
	}
	st->f_bsize = 512;
	st->f_frsize = CLUSTER;
	st->f_blocks = (VOL_BLOCKS - 8192u) / (CLUSTER / 512u);
	st->f_bfree = st->f_blocks > used ? st->f_blocks - used : 0;
	return 0;
}

/* ------------------------------------------------------------------ the host's side */

void emu_fs_reset_mounts(void)
{
	mounted = 0;
	disks = NULL;
	memset(handles, 0, sizeof(handles));
	memset(dirs, 0, sizeof(dirs));
}

int emu_fs_formatted(void) { return formatted; }
void emu_fs_set_formatted(int on) { formatted = on ? 1 : 0; if (!on) { emu_fs_clear(); } }
uint32_t emu_fs_generation(void) { return generation; }

int emu_fs_count(void) { return MAX_NODES; }
const char *emu_fs_path(int i) { return (i >= 0 && i < MAX_NODES) ? nodes[i].path : NULL; }
int emu_fs_is_dir(int i) { return (i >= 0 && i < MAX_NODES && nodes[i].path) ? nodes[i].is_dir : 0; }
uint32_t emu_fs_size(int i) { return (i >= 0 && i < MAX_NODES && nodes[i].path) ? nodes[i].size : 0; }
const uint8_t *emu_fs_data(int i) { return (i >= 0 && i < MAX_NODES) ? nodes[i].data : NULL; }

int emu_fs_put(const char *path, const uint8_t *data, uint32_t n, int is_dir)
{
	char p[256];
	if (norm(path, p, sizeof(p)) || strcmp(p, "/") == 0) {
		return -EINVAL;
	}
	/* Make the parents, as a computer writing to the drive would. */
	for (char *s = p + 1; (s = strchr(s, '/')) != NULL; s++) {
		*s = '\0';
		if (find(p) < 0 && add(p, 1) < 0) {
			return -ENOSPC;
		}
		*s = '/';
	}
	int i = find(p);
	if (i >= 0 && nodes[i].is_dir != is_dir) {
		return -EEXIST;
	}
	if (i < 0) {
		i = add(p, is_dir);
		if (i < 0) {
			return i;
		}
	}
	if (!is_dir) {
		if (reserve(&nodes[i], n)) {
			return -ENOSPC;
		}
		if (n) {
			memcpy(nodes[i].data, data, n);
		}
		nodes[i].size = n;
		generation++;
	}
	formatted = 1;
	return 0;
}

int emu_fs_remove(const char *path)
{
	char p[256];
	if (norm(path, p, sizeof(p))) {
		return -EINVAL;
	}
	const size_t n = strlen(p);
	for (int i = 0; i < MAX_NODES; i++) {
		if (nodes[i].path && (same(nodes[i].path, p) ||
		    (strlen(nodes[i].path) > n && nodes[i].path[n] == '/' &&
		     strncasecmp(nodes[i].path, p, n) == 0))) {
			drop(i);
		}
	}
	return 0;
}

void emu_fs_clear(void)
{
	for (int i = 0; i < MAX_NODES; i++) {
		if (nodes[i].path) {
			drop(i);
		}
	}
}
