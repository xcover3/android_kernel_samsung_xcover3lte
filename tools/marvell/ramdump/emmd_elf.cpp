/*
	This code has been taken from u-boot EMMD implementation file emmd_service.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdp.h"

#define MAX_MEM_PAIRS	10
#define EI_NIDENT	16

#define PF_X            (1 << 0)        /* Segment is executable */
#define PF_W            (1 << 1)        /* Segment is writable */
#define PF_R            (1 << 2)        /* Segment is readable */
#define ELFMAG          "\177ELF"
#define SELFMAG		4

#define EI_CLASS        4               /* File class byte index */
#define ELFCLASS32      1               /* 32-bit objects */

#define EI_DATA         5               /* Data encoding byte index */
#define ELFDATA2LSB     1               /* 2's complement, little endian */

#define EI_VERSION      6               /* File version byte index */
#define EV_CURRENT      1               /* Current version */

#define ET_CORE         4               /* Core file */
#define EM_ARM          40              /* ARM */

#define PT_LOAD         1               /* Loadable program segment */
#define PT_NOTE         4               /* Auxiliary information */

#define VIRT_START	0xc0000000

#if (0)
DECLARE_GLOBAL_DATA_PTR;
#endif

/* 32-bit ELF base types. */
typedef unsigned int Elf32_Addr;
typedef unsigned short Elf32_Half;
typedef unsigned int Elf32_Off;
typedef unsigned int Elf32_Sword;
typedef unsigned int Elf32_Word;

typedef struct elf32_hdr {
	unsigned char	e_ident[EI_NIDENT];
	Elf32_Half	e_type;
	Elf32_Half	e_machine;
	Elf32_Word	e_version;
	Elf32_Addr	e_entry;  /* Entry point */
	Elf32_Off	e_phoff;
	Elf32_Off	e_shoff;
	Elf32_Word	e_flags;
	Elf32_Half	e_ehsize;
	Elf32_Half	e_phentsize;
	Elf32_Half	e_phnum;
	Elf32_Half	e_shentsize;
	Elf32_Half	e_shnum;
	Elf32_Half	e_shstrndx;
} Elf32_Ehdr;

typedef struct elf32_phdr {
	Elf32_Word	p_type;
	Elf32_Off	p_offset;
	Elf32_Addr	p_vaddr;
	Elf32_Addr	p_paddr;
	Elf32_Word	p_filesz;
	Elf32_Word	p_memsz;
	Elf32_Word	p_flags;
	Elf32_Word	p_align;
} Elf32_Phdr;

#if (0)
ulong ion_base;
ulong ion_size;
#endif
void *init_elf(int *elf_size)
{
	unsigned long mem_pairs[2][2];
	int i, sz, offset;
	Elf32_Ehdr *ehdr;
	Elf32_Phdr *phdr;
	void *elf_header=0;
	int n_pheaders;

	mem_pairs[0][0] = segments[0].pa;
	mem_pairs[0][1] = segments[0].size;

	mem_pairs[1][0] = segments[1].pa;
	mem_pairs[1][1] = segments[1].size;

	n_pheaders = segments[1].size ? 3 : 2;
	sz = sizeof(Elf32_Ehdr) + sizeof(Elf32_Phdr)*n_pheaders;

	elf_header = malloc(sz);
	if (!elf_header) return elf_header;
	memset(elf_header, 0, sz);
	ehdr = (Elf32_Ehdr *)elf_header;

	memcpy(ehdr->e_ident, ELFMAG, SELFMAG);
	ehdr->e_ident[EI_CLASS] = ELFCLASS32;
	ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
	ehdr->e_ident[EI_VERSION] = EV_CURRENT;
	ehdr->e_type = ET_CORE;
	ehdr->e_machine = EM_ARM;
	ehdr->e_version = EV_CURRENT;
	ehdr->e_phoff = sizeof(Elf32_Ehdr);
	ehdr->e_ehsize = sizeof(Elf32_Ehdr);
	ehdr->e_phentsize = sizeof(Elf32_Phdr);

	phdr = (Elf32_Phdr *)(ehdr + 1);
	phdr->p_type = PT_NOTE;
	phdr->p_offset = sz;
	ehdr->e_phnum = n_pheaders;

	phdr++; /* done with PT_NOTE; now generate 1 or 2 extra pheaders for the memory blocks */
	offset = sz;
	for (i = 0; i < ehdr->e_phnum - 1; i++) {
		phdr->p_type = PT_LOAD;
		phdr->p_offset = offset;
		phdr->p_vaddr = mem_pairs[i][0] + VIRT_START;
		phdr->p_paddr = mem_pairs[i][0];
		phdr->p_filesz = mem_pairs[i][1];
		phdr->p_memsz = mem_pairs[i][1];
		phdr->p_flags = PF_X|PF_W|PF_R;
		phdr++;
		offset += mem_pairs[i][1];
	}

	*elf_size = sz;
	return elf_header;
}

/*
 * Checks if the input file is a dump in valid ELF format.
 * Returns:
 * 0: not ELF
 * 1: valid: fills segments[] based on ELF headers.
 * -1: ELF but not valid
 */
int check_elf(FILE *fin)
{
	elf32_hdr s_ehdr;
	Elf32_Phdr s_phdr;
	elf32_hdr *ehdr = &s_ehdr;
	Elf32_Phdr *phdr = &s_phdr;
	int n_pheaders;
	int i, is;
	if(fseek(fin, 0, SEEK_SET)) return 0;
	if(fread((void*)ehdr, sizeof(*ehdr), 1, fin) != 1) return 0;
	if(memcmp(ehdr->e_ident, ELFMAG, SELFMAG)) return 0;
	n_pheaders = ehdr->e_phnum;
	if((ehdr->e_ident[EI_CLASS] != ELFCLASS32) || (ehdr->e_type != ET_CORE) || (ehdr->e_machine != EM_ARM)
		|| (n_pheaders < 2)) return 0;

	for (i = 0, is = 0; i < n_pheaders; i++) {
		if(fread((void*)phdr, sizeof(*phdr), 1, fin) != 1) return -1;
		if(phdr->p_type == PT_LOAD) {
			if (is >= MAX_SEGMENTS) return -1;
			segments[is].foffs = phdr->p_offset;
			segments[is].pa = phdr->p_paddr;
			segments[is].size = phdr->p_filesz;
			is++;
		}
	}
	return is < 1 ? -1 : 1;
}
