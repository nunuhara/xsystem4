/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <SDL.h>

#include "system4.h"
#include "system4/buffer.h"
#include "system4/cg.h"
#include "system4/file.h"
#include "system4/little_endian.h"

#include "xsystem4.h"
#include "gfx/private.h"

#pragma pack(1)

#define NR_DIRECTORY_ENTRIES 16
#define SECTION_NAME_SIZE 8
#define DIRECTORY_ENTRY_RESOURCE 2
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x80

struct dos_header {
	uint16_t magic;
	uint16_t cblp;
	uint16_t cp;
	uint16_t crlc;
	uint16_t cparhdr;
	uint16_t minalloc;
	uint16_t maxalloc;
	uint16_t ss;
	uint16_t sp;
	uint16_t csum;
	uint16_t ip;
	uint16_t cs;
	uint16_t lfarlc;
	uint16_t ovno;
	uint16_t res[4];
	uint16_t oemid;
	uint16_t oeminfo;
	uint16_t res2[10];
	uint32_t lfanew;
};

// COFF header
struct coff_header {
	uint16_t machine;
	uint16_t number_of_sections;
	uint32_t time_date_stamp;
	uint32_t pointer_to_symbol_table;
	uint32_t number_of_symbols;
	uint16_t size_of_optional_header;
	uint16_t characteristics;
};

struct data_directory {
	uint32_t virtual_address;
	uint32_t size;
};

// PE optional header
struct optional_header {
	uint16_t magic;
	uint8_t major_linker_version;
	uint8_t minor_linker_version;
	uint32_t size_of_code;
	uint32_t size_of_initialized_data;
	uint32_t size_of_uninitialized_data;
	uint32_t address_of_entry_point;
	uint32_t base_of_code;
	uint32_t base_of_data;
	uint32_t image_base;
	uint32_t section_alignment;
	uint32_t file_alignment;
	uint16_t major_operating_system_version;
	uint16_t minor_operating_system_version;
	uint16_t major_image_version;
	uint16_t minor_image_version;
	uint16_t major_subsystem_version;
	uint16_t minor_subsystem_version;
	uint32_t win32_version_value;
	uint32_t size_of_image;
	uint32_t size_of_headers;
	uint32_t checksum;
	uint16_t subsystem;
	uint16_t dll_characteristics;
	uint32_t size_of_stack_reserve;
	uint32_t size_of_stack_commit;
	uint32_t size_of_heap_reserve;
	uint32_t size_of_heap_commit;
	uint32_t loader_flags;
	uint32_t number_of_rva_and_sizes;
	struct data_directory data_directory[NR_DIRECTORY_ENTRIES];
};

struct pe_header {
	uint32_t signature;
	struct coff_header coff;
	struct optional_header opt;
};

// header describing section in executable image
struct section_header {
	uint8_t name[SECTION_NAME_SIZE];
	union {
		uint32_t physical_address;
		uint32_t virtual_size;
	} misc;
	uint32_t virtual_address;
	uint32_t size_of_raw_data;
	uint32_t pointer_to_raw_data;
	uint32_t pointer_to_relocations;
	uint32_t pointer_to_linenumbers;
	uint16_t number_of_relocations;
	uint16_t number_of_linenumbers;
	uint32_t characteristics;
};

// resource directory table
struct res_dir_table {
	uint32_t characteristics;
	uint32_t time_date_stamp;
	uint16_t major_version;
	uint16_t minor_version;
	uint16_t number_of_named_entries;
	uint16_t number_of_id_entries;
};

// entry in resource directory table
struct res_dir_entry {
	uint32_t id;
	uint32_t off;
};

// leaf data of resource directory table
struct res_data_entry {
	uint32_t offset_to_data;
	uint32_t size;
	uint32_t code_page;
	uint32_t resource_handle;
};

// group_cursor resource data (table header)
struct cursor_dir {
	uint16_t reserved;
	uint16_t type;
	uint16_t count;
};

// entry in group_cursor table
struct cursor_dir_entry {
	uint16_t width;
	uint16_t height;
	uint16_t plane_count;
	uint16_t bit_count;
	uint32_t bytes_in_res;
	uint16_t res_id;
};

// group_icon resource data (table header)
struct icon_dir {
	uint16_t reserved;
	uint16_t type;
	uint16_t count;
};

// entry in group_icon table
struct icon_dir_entry {
	uint8_t width;
	uint8_t height;
	uint8_t color_count;
	uint8_t reserved;
	uint16_t plane_count;
	uint16_t bit_count;
	uint32_t bytes_in_res;
	uint16_t res_id;
};

// BITMAPINFOHEADER
struct bitmap_info {
	uint32_t size;
	uint32_t width;
	uint32_t height;
	uint16_t planes;
	uint16_t bpp;
	uint32_t compression;
	uint32_t size_image;
	uint32_t ppm_x;
	uint32_t ppm_y;
	uint32_t colors_used;
	uint32_t colors_important;
};

struct color_map {
	struct { uint8_t b, g, r, u; } colors[256];
};

// cursor resource data
struct cursor_data {
	uint16_t hotspot_x;
	uint16_t hotspot_y;
	struct bitmap_info bm_info;
	struct color_map colors;
};

// icon resource data
struct icon_data {
	struct bitmap_info bm_info;
	struct color_map colors;
};

struct resource {
	uint32_t id;
	struct resource *children;
	unsigned nr_children;
	struct {
		uint32_t addr;
		uint32_t size;
	} leaf;
};

struct vma {
	uint32_t virt_addr;
	uint32_t size;
	uint32_t raw_addr;
	bool initialized;
};

// TODO: use this abstraction for memory reads on executable image
struct vmm {
	struct vma *vmas;
	unsigned nr_vmas;
	struct buffer exe;
};

uint8_t *vmm_lookup(uint32_t ptr);

#pragma pack()

static bool buffer_expect_string(struct buffer *buf, const char *magic, size_t magic_size)
{
	if (buffer_remaining(buf) < magic_size)
		return false;
	return !strncmp(buffer_strdata(buf), magic, magic_size);
}

static uint16_t buffer_read_u16_at(struct buffer *buf, size_t off)
{
	size_t tmp = buf->index;
	buf->index = off;
	uint32_t v = buffer_read_u16(buf);
	buf->index = tmp;
	return v;
}

static uint32_t buffer_read_u32_at(struct buffer *buf, size_t off)
{
	size_t tmp = buf->index;
	buf->index = off;
	uint32_t v = buffer_read_int32(buf);
	buf->index = tmp;
	return v;
}

#define read_member16(b, s, m) \
	buffer_read_u16_at(b, b->index + offsetof(s, m));
#define read_member32(b, s, m) \
	buffer_read_u32_at(b, b->index + offsetof(s, m));

static bool read_pe_header(struct buffer *buf, uint32_t *addr_out, uint32_t *size_out,
		struct vma **vma_out, uint16_t *nr_vmas_out)
{
	possibly_unused size_t pe_loc = buf->index;
	if (buffer_remaining(buf) < sizeof(struct pe_header))
		return false;

	if (buffer_read_int32(buf) != 0x4550)
		return false;

	uint16_t nr_sections = read_member16(buf, struct coff_header, number_of_sections);
	uint16_t opt_size = read_member16(buf, struct coff_header, size_of_optional_header);

	buffer_skip(buf, sizeof(struct coff_header));
	assert(buf->index == pe_loc + offsetof(struct pe_header, opt));
	size_t opt_pos = buf->index;
	buffer_skip(buf, offsetof(struct optional_header, data_directory));
	buffer_skip(buf, sizeof(struct data_directory) * DIRECTORY_ENTRY_RESOURCE);

	*addr_out = read_member32(buf, struct data_directory, virtual_address);
	*size_out = read_member32(buf, struct data_directory, size);

	buffer_seek(buf, opt_pos + opt_size);
	if (buffer_remaining(buf) < nr_sections * sizeof(struct section_header))
		return false;

	struct vma *vma = xmalloc(nr_sections * sizeof(struct vma));
	for (int i = 0; i < nr_sections; i++) {
		vma[i].virt_addr = read_member32(buf, struct section_header, virtual_address);
		vma[i].size = read_member32(buf, struct section_header, size_of_raw_data);
		vma[i].raw_addr = read_member32(buf, struct section_header, pointer_to_raw_data);
		uint32_t characteristics = read_member32(buf, struct section_header, characteristics);
		vma[i].initialized = !(characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA);
		buffer_skip(buf, sizeof(struct section_header));
	}

	*vma_out = vma;
	*nr_vmas_out = nr_sections;
	return true;
}

static bool read_res_dir_table(struct buffer *b, struct res_dir_entry **entry_out,
		uint16_t *nr_named_out, uint16_t *nr_id_out)
{
	if (buffer_remaining(b) < sizeof(struct res_dir_table))
		return false;

	uint16_t nr_named = read_member16(b, struct res_dir_table, number_of_named_entries);
	uint16_t nr_id = read_member16(b, struct res_dir_table, number_of_id_entries);
	unsigned nr_entries = nr_named + nr_id;
	buffer_skip(b, sizeof(struct res_dir_table));
	if (buffer_remaining(b) < nr_entries * sizeof(struct res_dir_entry))
		return false;

	struct res_dir_entry *entry = xmalloc(nr_entries * sizeof(struct res_dir_entry));
	for (unsigned i = 0; i < nr_entries; i++) {
		entry[i].id = buffer_read_int32(b);
		entry[i].off = buffer_read_int32(b);
	}

	*entry_out = entry;
	*nr_named_out = nr_named;
	*nr_id_out = nr_id;
	return true;
}

bool read_resource_data(struct buffer *b, struct resource *node)
{
	if (buffer_remaining(b) < sizeof(struct res_data_entry))
		return false;
	node->leaf.addr = buffer_read_int32(b);
	node->leaf.size = buffer_read_int32(b);
	return true;
}

bool read_resource_table(struct buffer *b, struct resource *node, uint32_t res_base)
{
	uint16_t nr_named, nr_id;
	struct res_dir_entry *entry = NULL;
	if (!read_res_dir_table(b, &entry, &nr_named, &nr_id))
		goto error;

	node->nr_children = nr_named + nr_id;
	node->children = xcalloc(node->nr_children, sizeof(struct resource));

	for (unsigned i = 0; i < node->nr_children; i++) {
		node->children[i].id = entry[i].id;
		buffer_seek(b, res_base + (entry[i].off & 0x7fffffff));
		if (entry[i].off & 0x80000000) {
			if (!read_resource_table(b, &node->children[i], res_base))
				goto error;
		} else {
			if (!read_resource_data(b, &node->children[i]))
				goto error;
		}
	}

	free(entry);
	return true;
error:
	free(entry);
	free(node->children);
	return false;
}

struct resource *read_resources(struct buffer *b)
{
	struct resource *root = xcalloc(1, sizeof(struct resource));
	if (read_resource_table(b, root, b->index))
		return root;

	free(root);
	return NULL;
}

void _free_resources(struct resource *res)
{
	for (unsigned i = 0; i < res->nr_children; i++) {
		_free_resources(&res->children[i]);
	}
	free(res->children);
}

void free_resources(struct resource *root)
{
	_free_resources(root);
	free(root);
}

void read_bitmap_info(struct buffer *b, struct bitmap_info *bm_info)
{
	bm_info->size = buffer_read_int32(b);
	bm_info->width = buffer_read_int32(b);
	bm_info->height = buffer_read_int32(b);
	bm_info->planes = buffer_read_u16(b);
	bm_info->bpp = buffer_read_u16(b);
	bm_info->compression = buffer_read_int32(b);
	bm_info->size_image = buffer_read_int32(b);
	bm_info->ppm_x = buffer_read_int32(b);
	bm_info->ppm_y = buffer_read_int32(b);
	bm_info->colors_used = buffer_read_int32(b);
	bm_info->colors_important = buffer_read_int32(b);
}

static struct cg *cg_alloc_direct(unsigned w, unsigned h)
{
	struct cg *cg = xcalloc(1, sizeof(struct cg));
	cg->metrics.w = w;
	cg->metrics.h = h;
	cg->metrics.bpp = 32;
	cg->metrics.has_pixel = true;
	cg->metrics.has_alpha = true;
	cg->metrics.pixel_pitch = w * 4;
	cg->metrics.alpha_pitch = 1;
	cg->pixels = xcalloc(w * 4, h);
	return cg;
}

static struct cg *load_bmp_8bpp(struct bitmap_info *bm_info, struct color_map *colors,
		uint8_t *pixels, uint8_t *bitmask)
{
	unsigned w = bm_info->width;
	unsigned h = bm_info->height / 2;

	// expand mask
	uint8_t *mask = xmalloc(w * h);
	for (int row = 0; row < h; row++) {
		uint8_t *dst = mask + row * w;
		uint8_t *src = bitmask + row * (w/8);
		for (int bit = 0x80, col = 0; col < w; bit >>= 1, col++, dst++) {
			if (bit == 0) {
				bit = 0x80;
				src++;
			}
			*dst = (*src & bit) ? 0 : 255;
		}
	}

	// convert pixels/mask to RGBA CG
	struct cg *cg = cg_alloc_direct(w, h);
	for (unsigned row = 0; row < h; row++) {
		uint8_t *dst = cg->pixels + row * w * 4;
		uint8_t *p_src = pixels + (h - (row + 1)) * w;
		uint8_t *m_src = mask + (h - (row + 1)) * w;
		for (unsigned col = 0; col < w; col++, p_src++, m_src++, dst += 4) {
			uint8_t c = *p_src;
			dst[0] = colors->colors[c].r;
			dst[1] = colors->colors[c].g;
			dst[2] = colors->colors[c].b;
			dst[3] = *m_src;
		}
	}

	free(mask);
	return cg;
}

static struct cg *load_bmp_4bpp(struct bitmap_info *bm_info, struct color_map *colors,
		uint8_t *pixels, uint8_t *bitmask)
{
	unsigned w = bm_info->width;
	unsigned h = bm_info->height / 2;

	// expand mask
	uint8_t *mask = xmalloc(w * h);
	for (int row = 0; row < h; row++) {
		uint8_t *dst = mask + row * w;
		uint8_t *src = bitmask + row * (((w/8) + 3) & ~3);
		for (int bit = 0x80, col = 0; col < w; bit >>= 1, col++, dst++) {
			if (bit == 0) {
				bit = 0x80;
				src++;
			}
			*dst = (*src & bit) ? 0 : 255;
		}
	}

	// convert pixels/mask to RGBA CG
	struct cg *cg = cg_alloc_direct(w, h);
	for (unsigned row = 0; row < h; row++) {
		uint8_t *dst = cg->pixels + row * w * 4;
		uint8_t *p_src = pixels + (h - (row + 1)) * w / 2;
		uint8_t *m_src = mask + (h - (row + 1)) * w;
		for (unsigned col = 0; col < w; col += 2, p_src++, m_src += 2, dst += 8) {
			uint8_t c = *p_src >> 4;
			dst[0] = colors->colors[c].r;
			dst[1] = colors->colors[c].g;
			dst[2] = colors->colors[c].b;
			dst[3] = m_src[0];
			c = *p_src & 0xf;
			dst[4] = colors->colors[c].r;
			dst[5] = colors->colors[c].g;
			dst[6] = colors->colors[c].b;
			dst[7] = m_src[1];
		}
	}

	free(mask);
	return cg;
}

static bool read_icon_data(struct buffer *b, struct icon_data *icon, uint8_t **image, uint8_t **bitmask)
{
	if (buffer_remaining(b) < sizeof(struct bitmap_info))
		return false;
	read_bitmap_info(b, &icon->bm_info);
	if (icon->bm_info.planes != 1)
		return false;
	if (icon->bm_info.bpp != 8 && icon->bm_info.bpp != 4)
		return false;
	if (icon->bm_info.compression != 0)
		return false;

	int nr_colors = icon->bm_info.colors_used;
	if (!nr_colors) {
		if (icon->bm_info.bpp == 4)
			nr_colors = 16;
		else
			nr_colors = 256;
	}
	if (buffer_remaining(b) < 4 * nr_colors)
		return false;

	int image_size = icon->bm_info.width * (icon->bm_info.height / 2);
	if (icon->bm_info.bpp == 4)
		image_size /= 2;

	for (unsigned i = 0; i < nr_colors; i++) {
		icon->colors.colors[i].b = buffer_read_u8(b);
		icon->colors.colors[i].g = buffer_read_u8(b);
		icon->colors.colors[i].r = buffer_read_u8(b);
		icon->colors.colors[i].u = buffer_read_u8(b);
	}
	*image = (uint8_t*)buffer_strdata(b);
	buffer_skip(b, image_size);
	*bitmask = (uint8_t*)buffer_strdata(b);
	return true;
}

static SDL_Surface *read_icon(struct buffer *b, struct resource *res, struct cg **cg_out)
{
	struct icon_data icon;
	uint8_t *image;
	uint8_t *bitmask;
	buffer_seek(b, res->leaf.addr);
	if (!read_icon_data(b, &icon, &image, &bitmask)) {
		//WARNING("failed to read icon data");
		return NULL;
	}

	struct cg *cg;
	if (icon.bm_info.bpp == 8) {
		cg = load_bmp_8bpp(&icon.bm_info, &icon.colors, image, bitmask);
	} else if (icon.bm_info.bpp == 4) {
		cg = load_bmp_4bpp(&icon.bm_info, &icon.colors, image, bitmask);
	} else {
		WARNING("invalid bpp: %d", icon.bm_info.bpp);
		return NULL;
	}
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(cg->pixels, cg->metrics.w,
		cg->metrics.h, 32, cg->metrics.w * 4, SDL_PIXELFORMAT_RGBA32);
	if (!s)
		ERROR("SDL_CreateRGBSurfaceWithFormatFrom: %s", SDL_GetError());
	*cg_out = cg;
	return s;
}

static int read_group_icon(struct buffer *b, struct resource *res)
{
	buffer_seek(b, res->leaf.addr);
	if (buffer_remaining(b) < sizeof(struct icon_dir))
		return -1;

	uint16_t count = read_member16(b, struct icon_dir, count);
	if (count < 1) {
		WARNING("group_icon directory contains no entries");
		return -1;
	}
	//if (count > 1)
	//	WARNING("Ignoring additional entries in group_icon directory");

	buffer_skip(b, sizeof(struct icon_dir));
	buffer_skip(b, offsetof(struct icon_dir_entry, res_id));
	return buffer_read_u16(b);
}

static int nr_icons = 0;
static SDL_Surface **icons = NULL;
static struct cg **icon_cgs = NULL;

void read_icons(struct buffer *b, struct resource *root)
{
	struct resource *icon = NULL;
	struct resource *group_icon = NULL;
	for (unsigned i = 0; i < root->nr_children; i++) {
		if (root->children[i].id == 3) {
			icon = &root->children[i];
		} else if (root->children[i].id == 14) {
			group_icon = &root->children[i];
		}
	}

	nr_icons = group_icon->nr_children;
	icons = xcalloc(nr_icons, sizeof(struct SDL_Surface*));
	icon_cgs = xcalloc(nr_icons, sizeof(struct cg*));
	for (unsigned i = 0; i < group_icon->nr_children; i++) {
		struct resource *child = &group_icon->children[i];
		if (child->nr_children != 1 || child->children[0].nr_children != 0) {
			WARNING("Unexpected resource layout (icon %d)", i);
			continue;
		}
		int res_id = read_group_icon(b, &child->children[0]);
		if (res_id < 0)
			continue;

		// get the corresponding icon resource
		struct resource *icon_res = NULL;
		for (unsigned j = 0; j < icon->nr_children; j++) {
			struct resource *child = &icon->children[j];
			if (child->id == res_id) {
				if (child->nr_children != 1 || child->children[0].nr_children != 0) {
					WARNING("Unexpected resource layout (icon %d)", i);
					continue;
				}
				icon_res = &child->children[0];
			}
		}
		if (!icon_res) {
			WARNING("Couldn't find icon for group_icon %d", i);
			continue;
		}
		icons[i] = read_icon(b, icon_res, &icon_cgs[i]);
	}
}

SDL_Surface *icon_get(unsigned no)
{
	return no < nr_icons ? icons[no] : NULL;
}

bool _icon_init(const char *exe_path)
{
	struct buffer buf;
	struct vma *vma = NULL;
	struct res_dir_entry *entry = NULL;

	buf.buf = file_read(exe_path, &buf.size);
	buf.index = 0;
	if (!buf.buf) {
		WARNING("Failed to read executable: %s", exe_path);
		return false;
	}

	// expect DOS header
	if (!buffer_expect_string(&buf, "MZ", 2))
		goto error;
	if (buffer_remaining(&buf) < sizeof(struct dos_header))
		goto error;

	// seek to PE header
	buffer_skip(&buf, offsetof(struct dos_header, lfanew));
	buffer_seek(&buf, buffer_read_int32(&buf));
	if (!buffer_expect_string(&buf, "PE\0\0", 4))
		goto error;

	// read relevant data from PE header
	uint32_t res_virt_addr;
	uint32_t res_virt_size;
	uint16_t nr_vmas;
	if (!read_pe_header(&buf, &res_virt_addr, &res_virt_size, &vma, &nr_vmas))
		goto error;

	// calculate size of expanded process image
	size_t vma_size = 0;
	for (int i = 0; i < nr_vmas; i++) {
		vma_size = max(vma_size, vma[i].virt_addr + vma[i].size);
	}

	// load sections into memory at correct offsets
	// TODO: use an abstraction for virtual memory lookups that doesn't involve
	//       copying memory
	buf.buf = realloc(buf.buf, vma_size);
	buf.size = vma_size;
	for (int i = nr_vmas - 1; i >= 0; i--) {
		if (!vma[i].initialized)
			continue;
		if (vma[i].virt_addr + vma[i].size > buf.size)
			goto error;
		if (vma[i].raw_addr + vma[i].size > buf.size)
			goto error;
		if (vma[i].virt_addr != vma[i].raw_addr) {
			memmove(buf.buf + vma[i].virt_addr, buf.buf + vma[i].raw_addr, vma[i].size);
		}
	}

	if (res_virt_addr + res_virt_size > buf.size)
		goto error;

	buffer_seek(&buf, res_virt_addr);
	struct resource *root = read_resources(&buf);
	if (!root)
		goto error;

	read_icons(&buf, root);

	free_resources(root);
	free(buf.buf);
	free(vma);
	free(entry);

	SDL_Surface *icon = icon_get(0);
	if (icon)
		SDL_SetWindowIcon(sdl.window, icon);

	return true;
error:
	WARNING("Invalid/unexpected executable format: %s", exe_path);
	free(buf.buf);
	free(vma);
	free(entry);
	return false;
}

static char *get_exe_path(const char *name)
{
	char filename[256];
	snprintf(filename, 256, "%s.exe", name);

	char *path = gamedir_path(filename);
	if (file_exists(path))
		return path;
	char *i_path = path_get_icase(path);
	free(path);
	if (i_path)
		return i_path;
	return NULL;
}

static const char *exe_names[] = {
	"System40",
	"Rance01", // XXX: MG deleted BootName from AliceStart.ini...
	"daibanchou",
	"galzoo",
	"Hoken",
	"ojou",
	"paschacpp",
	"toushin3",
	"vanish",
	"yokubori",
};

void icon_init(void)
{
	char *path = NULL;
	if (config.boot_name)
		path = get_exe_path(config.boot_name);
	for (int i = 0; !path && i < sizeof(exe_names)/sizeof(*exe_names); i++) {
		path = get_exe_path(exe_names[i]);
	}
	if (path) {
		_icon_init(path);
		free(path);
	}
}
