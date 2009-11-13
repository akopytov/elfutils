#include "checks-low.hh"

#include <dwarf.h>
#include "../libdw/known-dwarf.h"
#include "dwarfstrings.h"
#include "pri.hh"

#include <sstream>

namespace
{
  class check_debug_line
    : public check<check_debug_line>
  {
    section<sec_line> *_m_sec;

    struct include_directory_t
    {
      std::string name;
      bool used;
    };
    typedef std::vector<include_directory_t> include_directories_t;
    include_directories_t _m_include_directories;

    struct file_t
    {
      const char *name;
      uint64_t dir_idx;
      bool used;
    };
    typedef std::vector<file_t> files_t;
    files_t _m_files;

  public:
    explicit check_debug_line (dwarflint &lint)
      : _m_sec (lint.check (_m_sec))
    {
      addr_record line_tables;
      WIPE (line_tables);
      if (!check_line_structural (&_m_sec->file,
				  &_m_sec->sect,
				  &line_tables))
	throw check_base::failed ();

      check_debug_info *info = NULL;
      info = lint.toplev_check (info);
      if (info != NULL)
	for (std::vector<cu>::iterator it = info->cus.begin ();
	     it != info->cus.end (); ++it)
	  for (size_t i = 0; i < it->line_refs.size; ++i)
	    {
	      struct ref *ref = it->line_refs.refs + i;
	      if (!addr_record_has_addr (&line_tables, ref->addr))
		wr_error (ref->who)
		  << "unresolved reference to .debug_line table "
		  << pri::hex (ref->addr) << '.' << std::endl;
	    }
      addr_record_free (&line_tables);
    }

    /* Directory index.  */
    bool read_directory_index (read_ctx *ctx,
			       const char *name, uint64_t *ptr,
			       where *where, bool &retval)
    {
      size_t nfile = _m_files.size () + 1;
      if (!checked_read_uleb128 (ctx, ptr,
				 where, "directory index"))
	return false;

      if (*name == '/' && *ptr != 0)
	wr_message (*where, cat (mc_impact_2, mc_line, mc_header))
	  << "file #" << nfile
	  << " has absolute pathname, but refers to directory != 0."
	  << std::endl;

      if (*ptr > _m_include_directories.size ())
	/* Not >=, dirs are indexed from 1.  */
	{
	  wr_message (*where, cat (mc_impact_4, mc_line, mc_header))
	    << "file #" << nfile
	    << " refers to directory #" << *ptr
	    << ", which wasn't defined." << std::endl;

    	  /* Consumer might choke on that.  */
	  retval = false;
	}
      else if (*ptr != 0)
	_m_include_directories[*ptr - 1].used = true;
      return true;
    }

    bool
    use_file (uint64_t file_idx, where *where)
    {
      if (file_idx == 0 || file_idx > _m_files.size ())
	{
	  wr_error (*where)
	     << "DW_LNS_set_file: invalid file index " << file_idx << '.'
	     << std::endl;
	  return false;
	}
      else
	_m_files[file_idx - 1].used = true;
      return true;
    }

    bool
    check_line_structural (struct elf_file *file,
			   struct sec *sec,
			   struct addr_record *line_tables);
  };

  reg<check_debug_line> reg_debug_line;
}

bool
check_debug_line::check_line_structural (struct elf_file *file,
					 struct sec *sec,
					 struct addr_record *line_tables)
{
  struct read_ctx ctx;
  read_ctx_init (&ctx, sec->data, file->other_byte_order);
  bool retval = true;

  while (!read_ctx_eof (&ctx))
    {
      struct where where = WHERE (sec->id, NULL);
      uint64_t set_offset = read_ctx_get_offset (&ctx);
      where_reset_1 (&where, set_offset);
      addr_record_add (line_tables, set_offset);
      const unsigned char *set_begin = ctx.ptr;

      /* Size.  */
      uint32_t size32;
      uint64_t size;
      int offset_size;
      if (!read_ctx_read_4ubyte (&ctx, &size32))
	{
	  wr_error (where) << "can't read table length." << std::endl;
	  return false;
	}
      if (!read_size_extra (&ctx, size32, &size, &offset_size, &where))
	return false;

      struct read_ctx sub_ctx;
      const unsigned char *set_end = ctx.ptr + size;
      if (!read_ctx_init_sub (&sub_ctx, &ctx, set_begin, set_end))
	{
	not_enough:
	  wr_error (where)
	    << pri::not_enough ("next unit") << '.' << std::endl;
	  return false;
	}
      sub_ctx.ptr = ctx.ptr;
      sub_ctx.begin = ctx.begin;

      {
      /* Version.  */
      uint16_t version;
      if (!read_ctx_read_2ubyte (&sub_ctx, &version))
	{
	  wr_error (where) << "can't read set version." << std::endl;
	skip:
	  retval = false;
	  goto next;
	}
      if (!supported_version (version, 2, &where, 2, 3))
	goto skip;

      /* Header length.  */
      uint64_t header_length;
      if (!read_ctx_read_offset (&sub_ctx, offset_size == 8, &header_length))
	{
	  wr_error (where) << "can't read attribute value." << std::endl;
	  goto skip;
	}
      const unsigned char *program_start = sub_ctx.ptr + header_length;

      /* Minimum instruction length.  */
      uint8_t minimum_i_length;
      if (!read_ctx_read_ubyte (&sub_ctx, &minimum_i_length))
	{
	  wr_error (where)
	    << "can't read minimum instruction length." << std::endl;
	  goto skip;
	}

      /* Default value of is_stmt.  */
      uint8_t default_is_stmt;
      if (!read_ctx_read_ubyte (&sub_ctx, &default_is_stmt))
	{
	  wr_error (where) << "can't read default_is_stmt." << std::endl;
	  goto skip;
	}
      /* 7.21: The boolean values "true" and "false" used by the line
	 number information program are encoded as a single byte
	 containing the value 0 for "false," and a non-zero value for
	 "true."  [But give a notice if it's not 0 or 1.]  */
      if (default_is_stmt != 0
	  && default_is_stmt != 1)
	wr_message (where, cat (mc_line, mc_impact_2, mc_header))
	  << "default_is_stmt should be 0 or 1, not "
	  << default_is_stmt << '.' << std::endl;

      /* Line base.  */
      int8_t line_base;
      if (!read_ctx_read_ubyte (&sub_ctx, (uint8_t *)&line_base))
	{
	  wr_error (where) << "can't read line_base." << std::endl;
	  goto skip;
	}

      /* Line range.  */
      uint8_t line_range;
      if (!read_ctx_read_ubyte (&sub_ctx, &line_range))
	{
	  wr_error (where) << "can't read line_range." << std::endl;
	  goto skip;
	}

      /* Opcode base.  */
      uint8_t opcode_base;
      if (!read_ctx_read_ubyte (&sub_ctx, &opcode_base))
	{
	  wr_error (where) << "can't read opcode_base." << std::endl;
	  goto skip;
	}

      /* Standard opcode lengths.  */
      if (opcode_base == 0)
	{
	  wr_error (where) << "opcode base set to 0." << std::endl;
	  opcode_base = 1; // so that in following, our -1s don't underrun
	}
      uint8_t std_opc_lengths[opcode_base - 1]; /* -1, opcodes go from 1.  */
      for (unsigned i = 0; i < (unsigned)(opcode_base - 1); ++i)
	if (!read_ctx_read_ubyte (&sub_ctx, std_opc_lengths + i))
	  {
	    wr_error (where)
	      << "can't read length of standard opcode #" << i << '.'
	      << std::endl;
	    goto skip;
	  }

      while (!read_ctx_eof (&sub_ctx))
	{
	  const char *name = read_ctx_read_str (&sub_ctx);
	  if (name == NULL)
	    {
	      wr_error (where)
		<< "can't read name of include directory #"
		<< _m_include_directories.size () + 1 // Numbered from 1.
		<< '.' << std::endl;
	      goto skip;
	    }
	  if (*name == 0)
	    break;

	  _m_include_directories.push_back ((include_directory_t){name, false});
	}

      /* File names.  */
      while (1)
	{
	  const char *name = read_ctx_read_str (&sub_ctx);
	  if (name == NULL)
	    {
	      wr_error (where)
		<< "can't read name of file #"
		<< _m_files.size () + 1 // Numbered from 1.
		<< '.' << std::endl;
	      goto skip;
	    }
	  if (*name == 0)
	    break;

	  uint64_t dir_idx;
	  if (!read_directory_index (&sub_ctx, name, &dir_idx, &where, retval))
	    goto skip;

	  /* Time of last modification.  */
	  uint64_t timestamp;
	  if (!checked_read_uleb128 (&sub_ctx, &timestamp,
				     &where, "timestamp of file entry"))
	    goto skip;

	  /* Size of the file.  */
	  uint64_t file_size;
	  if (!checked_read_uleb128 (&sub_ctx, &file_size,
				     &where, "file size of file entry"))
	    goto skip;

	  _m_files.push_back ((struct file_t){name, dir_idx, false});
	}

      /* Skip the rest of the header.  */
      if (sub_ctx.ptr > program_start)
	{
	  wr_error (where)
	    << "header claims that it has a size of " << header_length
	    << ", but in fact it has a size of "
	    << (sub_ctx.ptr - program_start + header_length)
	    << '.' << std::endl;

	  /* Assume that the header lies, and what follows is in
	     fact line number program.  */
	  retval = false;
	}
      else if (sub_ctx.ptr < program_start)
	{
	  struct where wh = WHERE (sec_line, NULL);
	  if (!check_zero_padding (&sub_ctx, cat (mc_line, mc_header), &where))
	    wr_message_padding_n0 (cat (mc_line, mc_header), &wh,
				   read_ctx_get_offset (&sub_ctx),
				   program_start - sub_ctx.begin);
	  sub_ctx.ptr = program_start;
	}

      bool terminated = false;
      bool first_file = true;
      bool seen_opcode = false;
      while (!read_ctx_eof (&sub_ctx))
	{
	  where_reset_2 (&where, read_ctx_get_offset (&sub_ctx));
	  uint8_t opcode;
	  if (!read_ctx_read_ubyte (&sub_ctx, &opcode))
	    {
	      wr_error (where) << "can't read opcode." << std::endl;
	      goto skip;
	    }

	  unsigned operands = 0;
	  uint8_t extended = 0;
	  switch (opcode)
	    {
	      /* Extended opcodes.  */
	    case 0:
	      {
		uint64_t skip_len;
		if (!checked_read_uleb128 (&sub_ctx, &skip_len, &where,
					   "length of extended opcode"))
		  goto skip;
		const unsigned char *next = sub_ctx.ptr + skip_len;
		if (!read_ctx_read_ubyte (&sub_ctx, &extended))
		  {
		    wr_error (where)
		      << "can't read extended opcode." << std::endl;
		    goto skip;
		  }

		bool handled = true;
		switch (extended)
		  {
		  case DW_LNE_end_sequence:
		    terminated = true;
		    break;

		  case DW_LNE_set_address:
		    {
		      uint64_t ctx_offset = read_ctx_get_offset (&sub_ctx);
		      uint64_t addr;
 		      if (!read_ctx_read_offset (&sub_ctx,
						 file->addr_64, &addr))
			{
			  wr_error (where)
			    << "can't read operand of DW_LNE_set_address."
			    << std::endl;
			  goto skip;
			}

		      struct relocation *rel;
		      if ((rel = relocation_next (&sec->rel, ctx_offset,
						  &where, skip_mismatched)))
			relocate_one (file, &sec->rel, rel,
				      file->addr_64 ? 8 : 4,
				      &addr, &where, rel_address, NULL);
		      else if (file->ehdr.e_type == ET_REL)
			wr_message (where, cat (mc_impact_2, mc_line, mc_reloc))
			  << pri::lacks_relocation ("DW_LNE_set_address")
			  << '.' << std::endl;
		      break;
		    }

		  case DW_LNE_define_file:
		    {
		      const char *name;
		      if ((name = read_ctx_read_str (&sub_ctx)) == NULL)
			{
			  wr_error (where)
			    << "can't read filename operand of DW_LNE_define_file."
			    << std::endl;
			  goto skip;
			}
		      uint64_t dir_idx;
		      if (!read_directory_index (&sub_ctx, name, &dir_idx,
						 &where, retval))
			goto skip;
		      _m_files.push_back
			((struct file_t){name, dir_idx, false});
		      operands = 2; /* Skip mtime & size of the file.  */
		    }

		    /* See if we know about any other standard opcodes.  */
		  default:
		    handled = false;
		    switch (extended)
		      {
#define ONE_KNOWN_DW_LNE(NAME, CODE) case CODE: break;
			ALL_KNOWN_DW_LNE
#undef ONE_KNOWN_DW_LNE
		      default:
			/* No we don't, emit a warning.  */
			wr_message (where, cat (mc_impact_2, mc_line))
			  << "unknown extended opcode #" << extended
			  << '.' << std::endl;
		      };
		  };

		if (sub_ctx.ptr > next)
		  {
		    wr_error (where)
		      << "opcode claims that it has a size of " << skip_len
		      << ", but in fact it has a size of "
		      << (skip_len + (next - sub_ctx.ptr)) << '.' << std::endl;
		    retval = false;
		  }
		else if (sub_ctx.ptr < next)
		  {
		    if (handled
			&& !check_zero_padding (&sub_ctx, mc_line, &where))
		      {
			struct where wh = WHERE (sec_line, NULL);
			wr_message_padding_n0 (mc_line, &wh,
					       read_ctx_get_offset (&sub_ctx),
					       next - sub_ctx.begin);
		      }
		    sub_ctx.ptr = next;
		  }
		break;
	      }

	      /* Standard opcodes that need validation or have
		 non-ULEB operands.  */
	    case DW_LNS_fixed_advance_pc:
	      {
		uint16_t a;
		if (!read_ctx_read_2ubyte (&sub_ctx, &a))
		  {
		    wr_error (where)
		      << "can't read operand of DW_LNS_fixed_advance_pc."
		      << std::endl;
		    goto skip;
		  }
		break;
	      }

	    case DW_LNS_set_file:
	      {
		uint64_t file_idx;
		if (!checked_read_uleb128 (&sub_ctx, &file_idx, &where,
					   "DW_LNS_set_file operand"))
		  goto skip;
		if (!use_file (file_idx, &where))
		  retval = false;
		first_file = false;
	      }
	      break;

	    case DW_LNS_set_isa:
	      // XXX is it possible to validate this?
	      operands = 1;
	      break;

	      /* All the other opcodes.  */
	    default:
	      if (opcode < opcode_base)
		operands = std_opc_lengths[opcode - 1];

    	      switch (opcode)
		{
#define ONE_KNOWN_DW_LNS(NAME, CODE) case CODE: break;
		  ALL_KNOWN_DW_LNS
#undef ONE_KNOWN_DW_LNS

		default:
		  if (opcode < opcode_base)
		    wr_message (where, cat (mc_impact_2, mc_line))
		      << "unknown standard opcode #" << opcode
		      << '.' << std::endl;
		};
	    };

	  for (unsigned i = 0; i < operands; ++i)
	    {
	      uint64_t operand;
	      char buf[128];
	      if (opcode != 0)
		sprintf (buf, "operand #%d of DW_LNS_%s",
			 i, dwarf_line_standard_opcode_string (opcode));
	      else
		sprintf (buf, "operand #%d of extended opcode %d",
			 i, extended);
	      if (!checked_read_uleb128 (&sub_ctx, &operand, &where, buf))
		goto skip;
	    }

	  if (first_file)
	    {
	      if (!use_file (1, &where))
		retval = false;
	      first_file = false;
	    }

	  if (opcode != 0 || extended != DW_LNE_end_sequence)
	    seen_opcode = true;
	}

      for (size_t i = 0; i < _m_include_directories.size (); ++i)
	if (!_m_include_directories[i].used)
	  wr_message (where,
		      cat (mc_impact_3, mc_acc_bloat, mc_line, mc_header))
	    << "the include #" << i + 1
	    << " `" << _m_include_directories[i].name
	    << "' is not used." << std::endl;

      for (size_t i = 0; i < _m_files.size (); ++i)
	if (!_m_files[i].used)
	  wr_message (where,
		      cat (mc_impact_3, mc_acc_bloat, mc_line, mc_header))
	    << "the file #" << i + 1
	    << " `" << _m_files[i].name << "' is not used." << std::endl;

      if (!seen_opcode)
	wr_message (where, cat (mc_line, mc_acc_bloat, mc_impact_3))
	  << "empty line number program." << std::endl;

      struct where wh = WHERE (sec_line, NULL);
      if (!terminated)
	wr_error (where)
	  << "sequence of opcodes not terminated with DW_LNE_end_sequence."
	  << std::endl;
      else if (sub_ctx.ptr != sub_ctx.end
	       && !check_zero_padding (&sub_ctx, mc_line, &wh))
	wr_message_padding_n0 (mc_line, &wh,
			       /*begin*/read_ctx_get_offset (&sub_ctx),
			       /*end*/sub_ctx.end - sub_ctx.begin);
      }

      /* XXX overlaps in defined addresses are probably OK, one
	 instruction can be derived from several statements.  But
	 certain flags in table should be consistent in that case,
	 namely is_stmt, basic_block, end_sequence, prologue_end,
	 epilogue_begin, isa.  */

    next:
      if (!read_ctx_skip (&ctx, size))
	goto not_enough;
    }

  if (retval)
    relocation_skip_rest (&sec->rel, sec->id);

  return retval;
}