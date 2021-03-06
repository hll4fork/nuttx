/****************************************************************************
 * sched/module/mod_sections.c
 *
 *   Copyright (C) 2015 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/kmalloc.h>
#include <nuttx/module.h>

#include "module.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mod_sectname
 *
 * Description:
 *   Get the symbol name in loadinfo->iobuffer[].
 *
 * Returned Value:
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

static inline int mod_sectname(FAR struct mod_loadinfo_s *loadinfo,
                               FAR const Elf32_Shdr *shdr)
{
  FAR Elf32_Shdr *shstr;
  FAR uint8_t *buffer;
  off_t  offset;
  size_t readlen;
  size_t bytesread;
  int shstrndx;
  int ret;

  /* Get the section header table index of the entry associated with the
   * section name string table. If the file has no section name string table,
   * this member holds the value SH_UNDEF.
   */

  shstrndx = loadinfo->ehdr.e_shstrndx;
  if (shstrndx == SHN_UNDEF)
    {
      serr("ERROR: No section header string table\n");
      return -EINVAL;
    }

  /* Get the section name string table section header */

  shstr = &loadinfo->shdr[shstrndx];

  /* Get the file offset to the string that is the name of the section. This
   * is the sum of:
   *
   *   shstr->sh_offset: The file offset to the first byte of the section
   *     header string table data.
   *   shdr->sh_name: The offset to the name of the section in the section
   *     name table
   */

  offset = shstr->sh_offset + shdr->sh_name;

  /* Loop until we get the entire section name into memory */

  buffer    = loadinfo->iobuffer;
  bytesread = 0;

  for (; ; )
    {
      /* Get the number of bytes to read */

      readlen = loadinfo->buflen - bytesread;
      if (offset + readlen > loadinfo->filelen)
        {
          if (loadinfo->filelen <= offset)
            {
              serr("ERROR: At end of file\n");
              return -EINVAL;
            }

          readlen = loadinfo->filelen - offset;
        }

      /* Read that number of bytes into the array */

      buffer = &loadinfo->iobuffer[bytesread];
      ret = mod_read(loadinfo, buffer, readlen, offset);
      if (ret < 0)
        {
          serr("ERROR: Failed to read section name: %d\n", ret);
          return ret;
        }

      bytesread += readlen;

      /* Did we read the NUL terminator? */

      if (memchr(buffer, '\0', readlen) != NULL)
        {
          /* Yes, the buffer contains a NUL terminator. */

          return OK;
        }

      /* No.. then we have to read more */

      ret = mod_reallocbuffer(loadinfo, CONFIG_MODULE_BUFFERINCR);
      if (ret < 0)
        {
          serr("ERROR: mod_reallocbuffer failed: %d\n", ret);
          return ret;
        }
    }

  /* We will not get here */

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mod_loadshdrs
 *
 * Description:
 *   Loads section headers into memory.
 *
 * Returned Value:
 *   0 (OK) is returned on success and a negated errno is returned on
 *   failure.
 *
 ****************************************************************************/

int mod_loadshdrs(FAR struct mod_loadinfo_s *loadinfo)
{
  size_t shdrsize;
  int ret;

  DEBUGASSERT(loadinfo->shdr == NULL);

  /* Verify that there are sections */

  if (loadinfo->ehdr.e_shnum < 1)
    {
      serr("ERROR: No sections(?)\n");
      return -EINVAL;
    }

  /* Get the total size of the section header table */

  shdrsize = (size_t)loadinfo->ehdr.e_shentsize * (size_t)loadinfo->ehdr.e_shnum;
  if (loadinfo->ehdr.e_shoff + shdrsize > loadinfo->filelen)
    {
      serr("ERROR: Insufficent space in file for section header table\n");
      return -ESPIPE;
    }

  /* Allocate memory to hold a working copy of the sector header table */

  loadinfo->shdr = (FAR FAR Elf32_Shdr *)kmm_malloc(shdrsize);
  if (!loadinfo->shdr)
    {
      serr("ERROR: Failed to allocate the section header table. Size: %ld\n",
           (long)shdrsize);
      return -ENOMEM;
    }

  /* Read the section header table into memory */

  ret = mod_read(loadinfo, (FAR uint8_t *)loadinfo->shdr, shdrsize,
                    loadinfo->ehdr.e_shoff);
  if (ret < 0)
    {
      serr("ERROR: Failed to read section header table: %d\n", ret);
    }

  return ret;
}

/****************************************************************************
 * Name: mod_findsection
 *
 * Description:
 *   A section by its name.
 *
 * Input Parameters:
 *   loadinfo - Load state information
 *   sectname - Name of the section to find
 *
 * Returned Value:
 *   On success, the index to the section is returned; A negated errno value
 *   is returned on failure.
 *
 ****************************************************************************/

int mod_findsection(FAR struct mod_loadinfo_s *loadinfo,
                    FAR const char *sectname)
{
  FAR const Elf32_Shdr *shdr;
  int ret;
  int i;

  /* Search through the shdr[] array in loadinfo for a section named 'sectname' */

  for (i = 0; i < loadinfo->ehdr.e_shnum; i++)
    {
      /* Get the name of this section */

      shdr = &loadinfo->shdr[i];
      ret  = mod_sectname(loadinfo, shdr);
      if (ret < 0)
        {
          serr("ERROR: mod_sectname failed: %d\n", ret);
          return ret;
        }

      /* Check if the name of this section is 'sectname' */

      sinfo("%d. Comparing \"%s\" and .\"%s\"\n",
            i, loadinfo->iobuffer, sectname);

      if (strcmp((FAR const char *)loadinfo->iobuffer, sectname) == 0)
        {
          /* We found it... return the index */

          return i;
        }
    }

  /* We failed to find a section with this name. */

  return -ENOENT;
}
