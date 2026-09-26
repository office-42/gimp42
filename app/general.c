/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <glib.h>
#include "datafiles.h"
#include "general.h"


/* prune filename removes all of the leading path information to a filename */

char *
prune_filename (char *filename)
{
  char *last_slash = filename;

  while (*filename)
    {
      char c = *filename++;

      if (c == '/' || c == G_DIR_SEPARATOR)
	last_slash = filename;
    }

  return last_slash;
}


/*  Looks for filename in the folders of search_path; returns the full
 *  name of the first match in a static buffer, or NULL.
 */
char*
search_in_path (char *search_path,
		char *filename)
{
  static char *path = NULL;
  GList *dirs, *list;
  char *found = NULL;

  dirs = datafiles_parse_path (search_path);

  for (list = dirs; list; list = list->next)
    {
      char *candidate = g_build_filename (list->data, filename, NULL);

      if (g_file_test (candidate, G_FILE_TEST_IS_REGULAR))
	{
	  g_free (path);
	  path = candidate;
	  found = path;
	  break;
	}

      g_free (candidate);
    }

  datafiles_free_path (dirs);
  return found;
}

/*****/

/* FIXME: This is straight from the GNU libc sources.  We should use
 * autoconf to test for a system having strsep() and similar
 * stuff... then use HAVE_STRSEP to decide whether to include or not
 * our own functions.
 */

char *
xstrsep (char **pp,
	 char  *delim)
{
  char *p, *q;

  if (!(p = *pp))
    return NULL;
  if ((q = strpbrk (p, delim))) {
    *pp = q + 1;
    *q = '\0';
  } else
    *pp = NULL;

  return p;
} /* xstrsep */


char *token_str;
char *token_sym;
double token_num;
int token_int;

/*  Appends c to the token being collected, growing the token buffer
 *  as needed (tokens, e.g. plug-in help strings in pluginrc, can be
 *  arbitrarily long).  info->tokenbuf must be heap allocated.
 */
static void
token_append (ParseInfo *info,
	      int       *tokenpos,
	      char       c)
{
  /* always leave room for the terminating '\0' */
  if (*tokenpos + 1 >= info->tokenbuf_size)
    {
      info->tokenbuf_size *= 2;
      info->tokenbuf = g_realloc (info->tokenbuf, info->tokenbuf_size);
    }

  info->tokenbuf[(*tokenpos)++] = c;
}

int
get_token (ParseInfo *info)
{
  char *buffer;
  int tokenpos = 0;
  int state;
  int count;
  int slashed;
  int octal_count = 0;    /*  digits of an octal escape read so far  */
  int octal_value = 0;

  state = 0;
  buffer = info->buffer;
  slashed = FALSE;

  while (1)
    {
      if (info->inc_charnum && info->charnum)
	info->charnum += 1;
      if (info->inc_linenum && info->linenum)
	{
	  info->linenum += 1;
	  info->charnum = 1;
	}

      info->inc_linenum = FALSE;
      info->inc_charnum = FALSE;

      if (info->position >= (info->buffer_size - 1))
	info->position = -1;
      if ((info->position == -1) || (buffer[info->position] == '\0'))
	{
	  count = fread (buffer, sizeof (char), info->buffer_size - 1, info->fp);
	  if (count == 0)  /* end of file, or a read error */
	    return TOKEN_EOF;
	  buffer[count] = '\0';
	  info->position = 0;
	}

      info->inc_charnum = TRUE;
      if ((buffer[info->position] == '\n') ||
	  (buffer[info->position] == '\r'))
	info->inc_linenum = TRUE;

      switch (state)
	{
	case 0:
	  if (buffer[info->position] == '#')
	    {
	      info->position += 1;
	      state = 4;
	    }
	  else if (buffer[info->position] == '(')
	    {
	      info->position += 1;
	      return TOKEN_LEFT_PAREN;
	    }
	  else if (buffer[info->position] == ')')
	    {
	      info->position += 1;
	      return TOKEN_RIGHT_PAREN;
	    }
	  else if (buffer[info->position] == '"')
	    {
	      info->position += 1;
	      state = 1;
	      slashed = FALSE;
	    }
	  else if ((buffer[info->position] == '-') || isdigit ((unsigned char) buffer[info->position]))
	    {
	      token_append (info, &tokenpos, buffer[info->position]);
	      info->position += 1;
	      state = 3;
	    }
	  else if ((buffer[info->position] != ' ') &&
		   (buffer[info->position] != '\t') &&
		   (buffer[info->position] != '\n') &&
		   (buffer[info->position] != '\r'))
	    {
	      token_append (info, &tokenpos, buffer[info->position]);
	      info->position += 1;
	      state = 2;
	    }
	  else if (buffer[info->position] == '\'')
	    {
	      info->position += 1;
	      state = 2;
	    }
	  else
	    {
	      info->position += 1;
	    }
	  break;
	case 1:
	  /*  An octal escape, \ooo: up to three digits, which may be split
	   *  across two reads of the file.
	   */
	  if (octal_count > 0)
	    {
	      char c = buffer[info->position];

	      if (c >= '0' && c <= '7' && octal_count < 3)
		{
		  octal_value = octal_value * 8 + (c - '0');
		  octal_count += 1;
		  info->position += 1;
		  if (octal_count == 3)
		    {
		      token_append (info, &tokenpos, (char) (octal_value & 0xff));
		      octal_count = 0;
		    }
		  break;
		}

	      /*  a shorter escape ends here; c is handled below  */
	      token_append (info, &tokenpos, (char) (octal_value & 0xff));
	      octal_count = 0;
	    }

	  if (slashed && buffer[info->position] >= '0' &&
	      buffer[info->position] <= '7')
	    {
	      slashed = FALSE;
	      octal_value = buffer[info->position] - '0';
	      octal_count = 1;
	      info->position += 1;
	    }
	  else if ((buffer[info->position] == '\\') && (!slashed))
	    {
	      slashed = TRUE;
	      info->position += 1;
	    }
	  else if (slashed || (buffer[info->position] != '"'))
	    {
	      slashed = FALSE;
	      token_append (info, &tokenpos, buffer[info->position]);
	      info->position += 1;
	    }
	  else
	    {
	      info->tokenbuf[tokenpos] = '\0';
	      token_str = info->tokenbuf;
	      token_sym = info->tokenbuf;
	      info->position += 1;
	      return TOKEN_STRING;
	    }
	  break;
	case 2:
	  if ((buffer[info->position] != ' ') &&
	      (buffer[info->position] != '\t') &&
	      (buffer[info->position] != '\n') &&
	      (buffer[info->position] != '\r') &&
	      (buffer[info->position] != '"') &&
	      (buffer[info->position] != '(') &&
	      (buffer[info->position] != ')'))
	    {
	      token_append (info, &tokenpos, buffer[info->position]);
	      info->position += 1;
	    }
	  else
	    {
	      info->tokenbuf[tokenpos] = '\0';
	      token_sym = info->tokenbuf;
	      return TOKEN_SYMBOL;
	    }
	  break;
	case 3:
	  if (isdigit ((unsigned char) buffer[info->position]) ||
	      (buffer[info->position] == '.'))
	    {
	      token_append (info, &tokenpos, buffer[info->position]);
	      info->position += 1;
	    }
	  else if ((buffer[info->position] != ' ') &&
		   (buffer[info->position] != '\t') &&
		   (buffer[info->position] != '\n') &&
		   (buffer[info->position] != '\r') &&
		   (buffer[info->position] != '"') &&
		   (buffer[info->position] != '(') &&
		   (buffer[info->position] != ')'))
	    {
	      token_append (info, &tokenpos, buffer[info->position]);
	      info->position += 1;
	      state = 2;
	    }
	  else
	    {
	      info->tokenbuf[tokenpos] = '\0';
	      token_sym = info->tokenbuf;
	      token_num = atof (info->tokenbuf);
	      token_int = atoi (info->tokenbuf);
	      return TOKEN_NUMBER;
	    }
	  break;
	case 4:
	  if (buffer[info->position] == '\n')
	    state = 0;
	  info->position += 1;
	  break;
	}
    }
}

/* Parse "line" and look for a string of characters without spaces
   following a '(' and copy them into "token_r".  Return the number of
   characters stored, or 0. */
int
find_token (char *line,
	    char *token_r,
	    int maxlen)
{
  char *sp;
  char *dp;
  int   i;

  /* FIXME: This should be replaced by a more intelligent parser which
     checks for '\', '"' and nested parentheses.  See get_token().  */
  for (sp = line; *sp; sp++)
    if (*sp == '(' || *sp == '#')
      break;
  if (*sp == '\000' || *sp == '#')
    {
      *token_r = '\000';
      return 0;
    }
  dp = token_r;
  sp++;
  i = 0;
  while ((*sp != '\000') && (*sp != ' ') && (*sp != '\t') && i < maxlen)
    {
      *dp = *sp;
      dp++;
      sp++;
      i++;
    }
  if (*sp == '\000' || i >= maxlen)
    {
      *token_r = '\000';
      return 0;
    }
  *dp = '\000';
  return i;
}

/* Generate a string containing the date and time and return a pointer
   to it.  If user_buf is not NULL, the string is stored in that
   buffer (21 bytes).  If strict is FALSE, the 'T' between the date
   and the time is replaced by a space, which makes the format a bit
   more readable (IMHO). */
char *
iso_8601_date_format (char *user_buf, int strict)
{
  static char static_buf[21];
  char *buf;
  time_t clock;
  struct tm *now;

  if (user_buf != NULL)
    buf = user_buf;
  else
    buf = static_buf;
  clock = time (NULL);
  now = gmtime (&clock);
  /* date format derived from ISO 8601:1988 */
  sprintf(buf, "%04d-%02d-%02d%c%02d:%02d:%02d%c",
	  now->tm_year + 1900, now->tm_mon + 1, now->tm_mday,
	  (strict ? 'T' : ' '),
	  now->tm_hour, now->tm_min, now->tm_sec,
	  (strict ? 'Z' : '\000'));
  return buf;
}
