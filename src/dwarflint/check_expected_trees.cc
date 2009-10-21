#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "checks-high.hh"
#include "expected.hh"
#include "dwarfstrings.h"
#include "../libdw/c++/dwarf-knowledge.cc"

using elfutils::dwarf;

namespace
{
  class check_expected_trees
    : public highlevel_check<check_expected_trees>
  {
  public:
    explicit check_expected_trees (dwarflint &lint);
  };

  reg<check_expected_trees> reg_check_expected_trees;

  const expected_at_map expected_at;
  //static const expected_children_map expected_children;

  struct name_extractor {
    int operator () (dwarf::attribute const &at) {
      return at.first;
    }
  } extract_name;

  std::ostream &
  operator << (std::ostream &o, dwarf::value_space vs)
  {
    switch (vs)
      {
      case dwarf::VS_flag: return o << "flag";
      case dwarf::VS_dwarf_constant: return o << "dwarf_constant";
      case dwarf::VS_discr_list: return o << "discr_list";
      case dwarf::VS_reference: return o << "reference";
      case dwarf::VS_lineptr: return o << "lineptr";
      case dwarf::VS_macptr: return o << "macptr";
      case dwarf::VS_rangelistptr: return o << "rangelistptr";
      case dwarf::VS_identifier: return o << "identifier";
      case dwarf::VS_string: return o << "string";
      case dwarf::VS_source_file: return o << "source_file";
      case dwarf::VS_source_line: return o << "source_line";
      case dwarf::VS_source_column: return o << "source_column";
      case dwarf::VS_address: return o << "address";
      case dwarf::VS_constant: return o << "constant";
      case dwarf::VS_location: return o << "location";
      };

    abort ();
  }
}

check_expected_trees::check_expected_trees (dwarflint &lint)
  : highlevel_check<check_expected_trees> (lint)
{
  lint.check <check_debug_info> ();

  try
    {
      struct
      {
	void operator () (dwarf::compile_unit const &cu,
			  dwarf::debug_info_entry const &parent)
	{
	  struct where where = WHERE (sec_info, NULL);
	  where_reset_1 (&where, cu.offset ());
	  where_reset_2 (&where, parent.offset ());

	  int parent_tag = parent.tag ();

	  // Set of attributes of this DIE.
	  std::set <int> attributes;
	  std::transform (parent.attributes ().begin (),
			  parent.attributes ().end (),
			  std::inserter (attributes, attributes.end ()),
			  extract_name);

	  // Attributes that we expect at this DIE.
	  expected_set::expectation_map const &expect
	    = expected_at.map (parent_tag);

	  // Check missing attributes.
	  for (expected_set::expectation_map::const_iterator jt
		 = expect.begin (); jt != expect.end (); ++jt)
	    {
	      std::set <int>::iterator kt = attributes.find (jt->first);
	      if (kt == attributes.end ())
		switch (jt->second)
		  {
		  case opt_required:
		    wr_message (cat (mc_impact_4, mc_info), &where,
				": %s lacks required attribute %s.\n",
				dwarf_tag_string (parent_tag),
				dwarf_attr_string (jt->first));
		    break;

		  case opt_expected:
		    wr_message (cat (mc_impact_2, mc_info), &where,
				": %s should contain attribute %s.\n",
				dwarf_tag_string (parent_tag),
				dwarf_attr_string (jt->first));
		  case opt_optional:
		    break;
		  };
	    }

	  // Check present attributes for expected-ness, and validate value space.
	  for (dwarf::debug_info_entry::attributes_type::const_iterator jt
		 = parent.attributes ().begin (), jte = parent.attributes ().end ();
	       jt != jte; ++jt)
	    {
	      unsigned name = extract_name (*jt);

	      expected_set::expectation_map::const_iterator kt = expect.find (name);
	      if (kt == expect.end ())
		wr_message (cat (mc_impact_3, mc_info), &where,
			    ": DIE \"%s\" has attribute \"%s\", which is not expected.\n",
			    dwarf_tag_string (parent_tag),
			    dwarf_attr_string (name));
	      try
		{
		  unsigned exp_vs = expected_value_space (name, parent_tag);
		  dwarf::value_space vs = (*jt).second.what_space ();
		  if ((exp_vs & (1U << vs)) == 0)
		    wr_message (cat (mc_impact_3, mc_info), &where,
				": in DIE \"%s\", attribute \"%s\" has value of unexpected type \"%u\".\n",
				dwarf_tag_string (parent_tag),
				dwarf_attr_string (name),
				vs);
		}
	      // XXX more specific class when <dwarf> has it
	      catch (...)
		{
		  wr_message (cat (mc_impact_4, mc_info, mc_error), &where,
			      ": in DIE \"%s\", couldn't obtain type of attribute \"%s\".\n",
			      dwarf_tag_string (parent_tag),
			      dwarf_attr_string (name));
		}
	    }

	  // Check children recursively.
	  dwarf::debug_info_entry::children_type const &children
	    = parent.children ();
	  for (dwarf::debug_info_entry::children_type::const_iterator
		 jt = children.begin (); jt != children.end (); ++jt)
	    (*this) (cu, *jt);
	}
      } recursively_validate;

      class dwarf::compile_units const &cus = dw.compile_units ();
      for (dwarf::compile_units::const_iterator it = cus.begin ();
	   it != cus.end (); ++it)
	recursively_validate (*it, *it);
    }
  // XXX more specific class when <dwarf> has it
  catch (std::runtime_error &exc)
    {
      throw check_base::failed
	(std::string ("Error while checking expected trees: ")
	 + exc.what () + ".\n");
    }
}