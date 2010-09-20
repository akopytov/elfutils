/* Pedantic checking of DWARF files
   Copyright (C) 2010 Red Hat, Inc.
   This file is part of Red Hat elfutils.

   Red Hat elfutils is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by the
   Free Software Foundation; version 2 of the License.

   Red Hat elfutils is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with Red Hat elfutils; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301 USA.

   Red Hat elfutils is an included package of the Open Invention Network.
   An included package of the Open Invention Network is a package for which
   Open Invention Network licensees cross-license their patents.  No patent
   license is granted, either expressly or impliedly, by designation as an
   included package.  Should you wish to participate in the Open Invention
   Network licensing program, please visit www.openinventionnetwork.com
   <http://www.openinventionnetwork.com>.  */

#include "cu_coverage.hh"
#include "check_debug_info.hh"
#include "check_debug_loc_range.hh"

checkdescriptor const *
cu_coverage::descriptor ()
{
  static checkdescriptor cd
    (checkdescriptor::create ("cu_coverage")
     .prereq<typeof (*_m_info)> ()
     .prereq<typeof (*_m_ranges)> ());
  return &cd;
}

cu_coverage::cu_coverage (checkstack &stack, dwarflint &lint)
  : _m_info (lint.check (stack, _m_info))
  , _m_ranges (lint.check_if (_m_info->need_ranges (), stack, _m_ranges))
{
  memset (&cov, 0, sizeof (cov));
  coverage_add_all (&cov, &_m_info->cov ());
  if (_m_ranges)
    coverage_add_all (&cov, &_m_ranges->cov ());
}

cu_coverage::~cu_coverage ()
{
  coverage_free (&cov);
}