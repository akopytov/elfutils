/* Convert function descriptor to the function PC value for Linux/PPC64 ABI.
   Copyright (C) 2005-2010 Red Hat, Inc.
   This file is part of elfutils.

   This file is free software; you can redistribute it and/or modify
   it under the terms of either

     * the GNU Lesser General Public License as published by the Free
       Software Foundation; either version 3 of the License, or (at
       your option) any later version

   or

     * the GNU General Public License as published by the Free
       Software Foundation; either version 2 of the License, or (at
       your option) any later version

   or both in parallel, as here.

   elfutils is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received copies of the GNU General Public License and
   the GNU Lesser General Public License along with this program.  If
   not, see <http://www.gnu.org/licenses/>.  */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>
#include "../libdwfl/libdwflP.h"

#define BACKEND ppc64_
#include "libebl_CPU.h"

/* Exact copy from src/readelf.c.  */

static const void *
convert (Elf *core, Elf_Type type, uint_fast16_t count,
	 void *value, const void *data, size_t size)
{
  Elf_Data valuedata =
    {
      .d_type = type,
      .d_buf = value,
      .d_size = size ?: gelf_fsize (core, type, count, EV_CURRENT),
      .d_version = EV_CURRENT,
    };
  Elf_Data indata =
    {
      .d_type = type,
      .d_buf = (void *) data,
      .d_size = valuedata.d_size,
      .d_version = EV_CURRENT,
    };

  Elf_Data *d = (gelf_getclass (core) == ELFCLASS32
		 ? elf32_xlatetom : elf64_xlatetom)
    (&valuedata, &indata, elf_getident (core, NULL)[EI_DATA]);
  if (d == NULL)
    return NULL;

  return data + indata.d_size;
}

struct pc_entry
{
  /* SYM_FROM must be the very first element for the use in bsearch below.  */
  GElf_Sym sym_from;
  Elf64_Addr st_value_to;
  const char *name_to;
};

/* FIXME: We could use fixedsizehash.h, we would hash the symbol name instead
   of GElf_Sym.  Although maybe the name is ambiguous compared to GElf_Sym.  */

struct pc_table
{
  size_t nelem;
  struct pc_entry a[];
  /* Here follows strings memory allocated for pc_entry->name_to.  */
};

static int
compar (const void *a_voidp, const void *b_voidp)
{
  const struct pc_entry *a = a_voidp;
  const struct pc_entry *b = b_voidp;

  return memcmp (&a->sym_from, &b->sym_from, sizeof (a->sym_from));
}

static void
init (Ebl *ebl, Dwfl_Module *mod)
{
  int syments = dwfl_module_getsymtab (mod);
  assert (syments >= 0);
  GElf_Sym sym;
  size_t funcs = 0;
  size_t names_size = 0;
  Elf *elf = ebl->elf;
  if (elf == NULL)
    return;
  GElf_Ehdr ehdr_mem, *ehdr = gelf_getehdr (elf, &ehdr_mem);
  if (ehdr == NULL)
    return;
  GElf_Word shndx, opd_shndx = 0;
  Elf_Data *opd_data;
  GElf_Shdr opd_shdr_mem, *opd_shdr;
  for (int symi = 1; symi < syments; symi++)
    {
      if (dwfl_module_getsym (mod, symi, &sym, &shndx) == NULL
	  || GELF_ST_TYPE (sym.st_info) != STT_FUNC)
	continue;
      if (sym.st_shndx != SHN_XINDEX)
	shndx = sym.st_shndx;
      if (opd_shndx == 0)
	{
	  Elf_Scn *scn = elf_getscn (elf, shndx);
	  if (scn == NULL)
	    continue;
	  opd_shdr = gelf_getshdr (scn, &opd_shdr_mem);
	  if (opd_shdr == NULL)
	    continue;
	  if (strcmp (elf_strptr (elf, ehdr->e_shstrndx, opd_shdr->sh_name), ".opd") != 0)
	    continue;
	  opd_data = elf_getdata (scn, NULL);
	  if (opd_data == NULL)
	    return;
	  assert (opd_data->d_size == opd_shdr->sh_size);
	  opd_shndx = shndx;
	}
      if (shndx != opd_shndx)
	continue;
      uint64_t val;
      if (sym.st_value < opd_shdr->sh_addr || sym.st_value > opd_shdr->sh_addr + opd_shdr->sh_size - sizeof (val))
	continue;
      const void *ptr = opd_data->d_buf + sym.st_value - opd_shdr->sh_addr;
      ptr = convert (elf, ELF_T_ADDR, 1, &val, ptr, 0);
      if (ptr == NULL)
	continue;
      if (unlikely (sym.st_name >= mod->symstrdata->d_size))
	continue;
      funcs++;
      const char *name = mod->symstrdata->d_buf + sym.st_name;
      names_size += 1 + strlen (name) + 1;
    }
  struct pc_table *pc_table = malloc (sizeof (*pc_table) + funcs * sizeof (*pc_table->a) + names_size);
  if (pc_table == NULL)
    return;
  ebl->backend = pc_table;
  pc_table->nelem = 0;
  if (funcs == 0)
    return;
  struct pc_entry *dest = pc_table->a;
  char *names = (void *) (pc_table->a + funcs), *names_dest = names;
  for (int symi = 1; symi < syments; symi++)
    {
      if (dwfl_module_getsym (mod, symi, &sym, &shndx) == NULL
	  || GELF_ST_TYPE (sym.st_info) != STT_FUNC)
	continue;
      assert (dest < pc_table->a + funcs);
      dest->sym_from = sym;
      if (sym.st_shndx != SHN_XINDEX)
	shndx = sym.st_shndx;
      if (shndx != opd_shndx)
	continue;
      uint64_t val;
      if (sym.st_value < opd_shdr->sh_addr || sym.st_value > opd_shdr->sh_addr + opd_shdr->sh_size - sizeof (val))
	continue;
      const void *ptr = opd_data->d_buf + sym.st_value - opd_shdr->sh_addr;
      ptr = convert (elf, ELF_T_ADDR, 1, &val, ptr, 0);
      assert (ptr != NULL);
      dest->st_value_to = val;
      assert (sym.st_name < mod->symstrdata->d_size);
      const char *name = mod->symstrdata->d_buf + sym.st_name;
      dest->name_to = names_dest;
      *names_dest++ = '.';
      names_dest = stpcpy (names_dest, name) + 1;
printf("0x%lx -> 0x%lx = %s\n",dest->sym_from.st_value, dest->st_value_to, dest->name_to);
      dest++;
      pc_table->nelem++;
    }
  assert (pc_table->nelem == funcs);
  assert (dest == pc_table->a + pc_table->nelem);
  assert (names_dest == names + names_size);
  qsort (pc_table->a, pc_table->nelem, sizeof (*pc_table->a), compar);
}

const char *
ppc64_get_func_pc (Ebl *ebl, Dwfl_Module *mod, GElf_Sym *sym)
{
  if (ebl->backend == NULL)
    init (ebl, mod);
  if (ebl->backend == NULL)
    return NULL;
  const struct pc_table *pc_table = ebl->backend;
  const struct pc_entry *found = bsearch (sym, pc_table->a, pc_table->nelem, sizeof (*pc_table->a), compar);
  if (found == NULL)
    return NULL;
  sym->st_value = found->st_value_to;
  return found->name_to;
}

void
ppc64_destr (Ebl *ebl)
{
  if (ebl->backend == NULL)
    return;
  struct pc_table *pc_table = ebl->backend;
  free (pc_table);
  ebl->backend = NULL;
}
