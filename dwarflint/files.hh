/* Pedantic checking of DWARF files.
   Copyright (C) 2011 Red Hat, Inc.
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

#ifndef _DWARFLINT_FILES_H_
#define _DWARFLINT_FILES_H_

#include "../libdwfl/libdwfl.h"
#include "../libdw/c++/dwarf"

// The functions in this module do their own error handling, and throw
// std::runtime_error with descriptive error message on error.
namespace files
{
  int open (char const *fname);

  Dwfl *open_dwfl ()
    __attribute__ ((nonnull, malloc));

  Dwarf *open_dwarf (Dwfl *dwfl, char const *fname, int fd)
    __attribute__ ((nonnull, malloc));

  elfutils::dwarf open_dwarf (Dwarf *dw);
}

#endif /* _DWARFLINT_FILES_H_ */