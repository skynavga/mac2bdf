/*
 *
 * Copyright (C) 2021, Glenn Adams
 * Copyright (C) 1993, Norm Walsh
 * Copyright (C) 1992, 1993, Metis Technology, Inc.
 *
 * STANDARD DISCLAIMERS
 *
 * This program is distributed under the BSD 2-Clause License as
 * specified in the accompanying LICENSE file.
 *
 * PROGRAM DESCRIPTION
 *
 * Name:     MAC2BDF
 * Summary:  Utility for extracting bitmap fonts in Macintosh files,
 *           and converting these fonts to Adobe Bitmap Distribution
 *	     Format (BDF) files.  The resulting BDF files can then
 *           be converted into X11 Font Formats, e.g., by using bdftosnf.
 *           Both FONT and NFNT resources are extracted.  FOND resources
 *	     are used to select the appropriate font and font file name;
 *	     however, FONT and NFNT resources which are orphaned (i.e.,
 *	     have no governing FOND) are extracted to temporary font
 *	     names.
 * Usage:    mac2bdf [-n] [-q] [-v] file
 * Options:  -n    Don't do anything, just report what would be done.
 *	     -q    Don't report dumped fonts.
 *	     -v    Enable verbose reporting.
 * Input:    A Macintosh file in MacBinary format.
 * Output:   Zero or more Adobe BDF files, one for each font resource.  The
 *	     names chosen for output font files are generated based on the
 *	     font family, style, and size.  Existing files with the same names
 *	     are silently replaced.  If you wish to know which files will be
 *	     produced, use the [-n] option prior to doing the real conversion.
 * Comments: The Mac font format does not have all of the information one
 *	     would normally place in a BDF file; e.g., glyph names are not
 *           specified in the Mac font resources.  Consequently, the names
 *           given to glyphs in the BDF file are dynamically assigned in a
 *           unique manner.
 * History:  11/04/92 -- Created
 *	     11/27/92 -- Modified to find font resources from FONDs.
 *	     11/27/92 -- Modified to load one font resource at a time.
 *	     12/08/92 -- Fixed bug with locTab & owTab indexing; this
 *	              -- corrected the Ajmer core dump problem.  Change
 *		      -- font name handling to substitute hyphens for
 *		      -- whitespace.  Handle overflows of OWTLoc offset
 *		      -- field for large bitmaps.
 *           08/19/93 -- Modifications by Norm Walsh.
 * Notes:    1. Orphaned font resources are not yet handled.
 *	     2. Prefixes B,I,BI,Sb,SbI need to be removed.
 *	     3. Add option to specify family, size, style to dump.
 * Authors:  Glenn Adams <glenn@metis.com> (original code)
 *           Norm Walsh <walsh@cs.umass.edu> (modifications)
 *
 * WARNINGS
 *
 * 1. This program was quickly hacked together to get its job done quickly
 *    with little error checking or view towards portability.
 * 2. This program has only been compiled and run on SunOS 4.1.1. It may not
 *    compile on other platforms, let alone even run.
 * 3. The only input this program accepts is a MacBinary format file which
 *    was created by using NCSA Telnet to FTP standard Mac files (containing
 *    both data and resource forks) to a Sun system. It may also work with
 *    MacBinary files as transferred by other terminal emulator programs;
 *    however, this has not been tested.
 * 4. This program is being released in this form in the hope that it will
 *    be useful to someone without all the frills one would expect from a
 *    robust program; this program does not make any claims to robustness.
 *
 * N.B. The above program description is based upon a prior distribution
 * by Metis Technology, Inc which contained a top-level main function.
 * The code found below consists of only the primary Mac Font Resource
 * to BDF conversion functions. A top-level main function that would be
 * invoked by a mac2bdf executable as described above is not included
 * herein.
 */

#include <alloca.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define  DEVXRES	72	/* Macintosh X-Resolution */
#define  DEVYRES	72	/* Macintosh Y-Resolution */

typedef	char		INT8;
typedef	short		INT16;
typedef	long		INT32;

typedef	unsigned char	CARD8;
typedef	unsigned short	CARD16;
typedef	unsigned long	CARD32;

typedef struct _MacBinHdrRec MacBinHdrRec, *MacBinHdr;
struct _MacBinHdrRec {
  CARD8		fnLen	     [   2 ];
  CARD8		fnName       [  63 ];
  CARD8		fnType       [   4 ];
  CARD8		fnCreator    [   4 ];
  CARD8		fnFlags      [   2 ];
  CARD8		pad1         [   2 ];
  CARD8		pad2         [   2 ];
  CARD8		pad3         [   4 ];
  CARD8		fnDataLen    [   4 ];
  CARD8         pad4         [  41 ];
};

#define RHDRLEN		       256	/* total header length */

typedef struct _RsrcHdrRec RsrcHdrRec, *RsrcHdr;
struct _RsrcHdrRec {
  CARD8         rhDataOffset [   4 ];
  CARD8		rhMapOffset  [   4 ];
  CARD8		rhDataLen    [   4 ];
  CARD8		rhMapLen     [   4 ];
};

typedef struct _RsrcMapRec RsrcMapRec, *RsrcMap;
struct _RsrcMapRec {
  RsrcHdrRec	rmHdrCopy;
  CARD8		rmNextMap    [   4 ];
  CARD8		rmFileRef    [   2 ];
  CARD8         rmFileAttr   [   2 ];
  CARD8         rmTypeOffset [   2 ];
  CARD8         rmNameOffset [   2 ];
};

typedef struct _RsrcTypeRec RsrcTypeRec, *RsrcType;
struct _RsrcTypeRec {
  CARD8		rtName       [   4 ];
  CARD8		rtCount      [   2 ];
  CARD8         rtRefOffset  [   2 ];
};

typedef struct _RsrcRefRec RsrcRefRec, *RsrcRef;
struct _RsrcRefRec {
  CARD8		rrIdent      [   2 ];
  CARD8		rrNameOffset [   2 ];
  CARD8         rrAttr       [   1 ];
  CARD8         rrDataOffset [   3 ];
  CARD8         rrReserved   [   4 ];
};

typedef struct _FontRsrcRec FontRsrcRec, *FontRsrc;
struct _FontRsrcRec {
  CARD8		ftFontType   [   2 ];
  CARD8		ftFirstChar  [   2 ];
  CARD8		ftLastChar   [   2 ];
  CARD8		ftWidMax     [   2 ];
  CARD8		ftKernMax    [   2 ];
  CARD8		ftNDescent   [   2 ];
  CARD8		ftFRectWidth [   2 ];
  CARD8		ftFRectHeight[   2 ];
  CARD8		ftOWTLoc     [   2 ];
  CARD8		ftAscent     [   2 ];
  CARD8		ftDescent    [   2 ];
  CARD8		ftLeading    [   2 ];
  CARD8		ftRowWords   [   2 ];
};

typedef struct _FondRsrcRec FondRsrcRec, *FondRsrc;
struct _FondRsrcRec {
  CARD8		fdFlags	     [   2 ];
  CARD8		fdFamID	     [   2 ];
  CARD8		fdFirst	     [   2 ];
  CARD8		fdLast	     [   2 ];
  CARD8		fdAscent     [   2 ];
  CARD8		fdDescent    [   2 ];
  CARD8		fdLeading    [   2 ];
  CARD8		fdWidMax     [   2 ];
  CARD8		fdWTabOff    [   4 ];
  CARD8		fdKernOff    [   4 ];
  CARD8		fdStylOff    [   4 ];
  CARD8		fdProperty   [  18 ];
  CARD8		fdIntl	     [   4 ];
  CARD8		fdVersion    [   2 ];
};

typedef struct _FontNameRec FontNameRec, *FontName;
struct _FontNameRec {
  char *	name;
  int		resource_id;
  int		size;
  int		style;
  FontName	next;
};

char *		progname;
int		nodump;
int		quiet;
int		verbose;
FontName	fontnames;

char *
strdup (s)
     char *s;
{
  char *news = (char *) malloc (strlen (s) + 1);
  strcpy (news, s);
  return news;
}

CARD16
  toushort ( p )
CARD8 *p;
{
  return (CARD16) ( ( p[0] << 8 ) | p [ 1 ] );
}

INT16
  toshort ( p )
CARD8 *p;
{
  return (INT16) toushort ( p );
}

CARD32
  toulong ( p )
CARD8 *p;
{
  return (CARD32) ( ( p[0] << 24 ) | ( p[1] << 16 ) | ( p[2] << 8 ) | p[3] );
}

INT32
  tolong ( p )
CARD8 *p;
{
  return (INT32) toulong ( p );
}

/*
 * Find font bounding box and total number of glyphs.
 */
void
  FontInfo ( fp, ret_top, ret_left, ret_bottom, ret_right, ret_ng )
register FontRsrc fp;
INT16	*ret_top;
INT16	*ret_left;
INT16	*ret_bottom;
INT16	*ret_right;
INT16	*ret_ng;
{
  register int i, j, bit;
  register CARD16 *bp;
  register CARD8  *gp;
  CARD16   g, fg, lg, coff0, coff1, ow, wo;
  INT16    mk, wd, ht, rw, xoff, top, bot, left, right, ng;
  CARD8 *  bitImage;
  CARD8 *  locTable;
  CARD8 *  owTable;

  top = bot = left = right = 0;

  fg = toushort ( fp->ftFirstChar   );
  lg = toushort ( fp->ftLastChar    );
  mk = toshort  ( fp->ftKernMax     );
  wd = toshort  ( fp->ftFRectWidth  );
  ht = toshort  ( fp->ftFRectHeight );
  wo = toushort ( fp->ftOWTLoc      );
  rw = toshort  ( fp->ftRowWords    );

  bitImage = (CARD8 * ) & fp [ 1 ];
  locTable = & bitImage     [ ( rw * ht ) << 1     ];
#ifdef notdef
  owTable  = & fp->ftOWTLoc [ wo          << 1     ];
#else
  owTable  = & locTable     [ ( lg - fg + 3 ) << 1 ];
#endif

  if ( lg == fg )
    return;

  if ( (CARD32) bitImage & 0x1 ) {
    bp = (CARD16 *) alloca ( ht * rw * 2 );
    (void) memcpy ( (char *) bp, (char *) bitImage, ht * rw * 2 );
  } else
    bp = (CARD16 *) bitImage;

  gp = (CARD8 *) alloca ( wd * ht );
  (void) memset ( (char *) gp, 0, wd * ht );

  for ( g = fg, ng = 0; g <= lg; g++ ) {

    /*
     * Get starting and ending offset columns in bit image.
     */
    coff0 = toushort ( & locTable [ ( ( g - fg ) + 0 ) << 1 ] );
    coff1 = toushort ( & locTable [ ( ( g - fg ) + 1 ) << 1 ] );
    if ( coff0 == coff1 )
      continue;

    /*
     * Get basepoint offset and escapement.
     */
    ow    = toushort ( & owTable  [ ( g - fg ) << 1 ] );
    xoff  = ( ( ow >> 8 ) & 0xff ) + mk;

    /*
     * Overlay glyph image.
     */
    for ( i = 0; i < ht; i++ ) {
      for ( j = coff0; j < coff1; j++ ) {
	if ( (int) ( ( j - coff0 ) + xoff ) < 0 )
	  continue;
	gp [ i * wd + ( j - coff0 ) + xoff ] |=
	  ( bp [ i * rw + ( j / 16 ) ] >> ( 15 - ( j % 16 ) ) ) & 1;
      }
    }

    ng++;
  }

  /*
   * Find top and bottom of bounding box.
   */
  top = ht;
  bot = 0;
  for ( i = 0; i < ht; i++ ) {
    for ( j = 0; j < wd; j++ ) {
      bit = gp [ i * wd + j ];
      if ( bit && ( i < top ) )
	top = i;
      if ( bit && ( i > bot ) )
	bot = i;
    }
  }

  /*
   * Find left and right of bounding box.
   */
  left  = wd;
  right = 0;
  for ( i = 0; i < ht; i++ ) {
    for ( j = 0; j < wd; j++ ) {
      bit = gp [ i * wd + j ];
      if ( bit && ( j < left ) )
	left  = j;
      if ( bit && ( j > right ) )
	right = j;
    }
  }

#ifdef notdef
  if ( ( ( right - left ) + 1 ) != wd )
    (void) fprintf ( stderr,
		    "%s: warning: bbox width mismatch, got %d, expected %d\n",
		    progname, ( right - left ) + 1, wd );
  if ( ( ( bot - top ) + 1 ) != ht )
    (void) fprintf ( stderr,
		    "%s: warning: bbox height mismatch, got %d, expected %d\n",
		    progname, ( bot - top ) + 1, ht );
#endif

  *ret_top    = top;
  *ret_left   = left + mk;
  *ret_bottom = bot;
  *ret_right  = right + mk;
  *ret_ng     = ng;
}

char *
  FontStyleName ( style )
int style;
{
  static char sname [ 128 ];
  char *retsname;

  sname [ 0 ] = '\0';
  if ( style & 0001 )
    (void) strcat ( sname, "Bold" );
  if ( style & 0002 )
    (void) strcat ( sname, "Italic" );
  if ( style & 0004 )
    (void) strcat ( sname, "Underlined" );
  if ( style & 0010 )
    (void) strcat ( sname, "Outlined" );
  if ( style & 0020 )
    (void) strcat ( sname, "Shadowed" );
  if ( style & 0040 )
    (void) strcat ( sname, "Condensed" );
  if ( style & 0100 )
    (void) strcat ( sname, "Extended" );

  retsname = (char *) malloc (strlen(sname+1));
  strcpy(retsname, sname);
  return retsname;
}

int
  FontDump ( fp, name, style, size )
FontRsrc fp;
char *	 name;
int	 style;
int	 size;
{
  register int i, j, bit, bits;
  register CARD16 *bp;
  register CARD8  *gp;
  CARD16   g, fg, lg, coff0, coff1, ow, wo;
  INT16    mk, wd, ht, rw, top, bot, left, right, ng;
  CARD8 *  bitImage;
  CARD8 *  locTable;
  CARD8 *  owTable;
  FILE *   fout;
  char 	   fname [ 128 ];

  if ( ! fp || ! name || ! size )
    return 1;

  fg = toushort ( fp->ftFirstChar   );
  lg = toushort ( fp->ftLastChar    );
  mk = toshort  ( fp->ftKernMax     );
  wd = toshort  ( fp->ftFRectWidth  );
  ht = toshort  ( fp->ftFRectHeight );
  wo = toushort ( fp->ftOWTLoc      );
  rw = toshort  ( fp->ftRowWords    );

  bitImage = (CARD8 *) & fp [ 1 ];
  locTable = & bitImage     [ ( rw * ht ) << 1 ];
#ifdef notdef
  owTable  = & fp->ftOWTLoc [ wo          << 1     ];
#else
  owTable  = & locTable     [ ( lg - fg + 3 ) << 1 ];
#endif

  /*
   * If no glyphs are present, don't dump anything.
   */
  if ( lg == fg )
    return 1;

  /*
   * At least one glyph is present; create BDF file.
   */
  (void) sprintf ( fname, "%s%s-%d.bdf", name, FontStyleName ( style ), size );
  if ( ! ( fout = fopen ( fname, "w+" ) ) ) {
    (void) fprintf ( stderr, "%s: can't create output file \"%s\"\n",
		    progname, fname );
    return 0;
  }

  /*
   * Obtain per-font information and dump BDF font header.
   */
  FontInfo ( fp, & top, & left, & bot, & right, & ng );

  if ( ! quiet )
    (void) printf  ( "Dumping %d glyphs to \"%s%s-%d.bdf\"\n",
		     ng, name, FontStyleName ( style ), size );

  (void) fprintf ( fout, "STARTFONT 2.1\n" );
  (void) fprintf ( fout, "FONT %s%s-%d\n",
		   name, FontStyleName ( style ), size );
  (void) fprintf ( fout, "SIZE %d %d %d\n", size, DEVXRES, DEVYRES );
  (void) fprintf ( fout, "FONTBOUNDINGBOX %d %d %d %d\n",
		   ( right - left ) + 1,
		   ( bot - top ) + 1,
		   mk,
		   ( ht - toshort ( fp->ftDescent ) ) - ( bot + 1 ) );
  (void) fprintf ( fout, "STARTPROPERTIES 2\n" );
  (void) fprintf ( fout, "FONT_ASCENT %d\n", toshort  ( fp->ftAscent ) );
  (void) fprintf ( fout, "FONT_DESCENT %d\n", toshort  ( fp->ftDescent ) );
  (void) fprintf ( fout, "ENDPROPERTIES\n" );
  (void) fprintf ( fout, "CHARS %d\n", ng );

  if ( (CARD32) bitImage & 0x1 ) {
    bp = (CARD16 *) alloca ( ht * rw * 2 );
    (void) memcpy ( (char *) bp, (char *) bitImage, ht * rw * 2 );
  } else
    bp = (CARD16 *) bitImage;

  gp = (CARD8 *) alloca ( wd * ht );
  (void) memset ( (char *) gp, 0, wd * ht );

  for ( g = fg; g <= lg; g++ ) {

    /*
     * Get starting and ending offset columns in bit image.
     */
    coff0 = toushort ( & locTable [ ( ( g - fg ) + 0 ) << 1 ] );
    coff1 = toushort ( & locTable [ ( ( g - fg ) + 1 ) << 1 ] );
    if ( coff0 == coff1 )
      continue;

    /*
     * Get basepoint offset and escapement.
     */
    ow    = toushort ( & owTable  [ ( g - fg ) << 1 ] );

    /*
     * Extract glyph image.
     */
    (void) memset ( (char *) gp, 0, wd * ht );
    for ( i = 0; i < ht; i++ ) {
      for ( j = coff0; j < coff1; j++ ) {
        bit = ( bp [ i * rw + ( j / 16 ) ] >> ( 15 - ( j % 16 ) ) ) & 1;
	gp [ i * wd + ( j - coff0 ) ] = bit;
      }
    }
    
    /*
     * Find top and bottom of bounding box.
     */
    top = ht;
    bot = 0;
    for ( i = 0; i < ht; i++ ) {
      for ( j = 0; j < wd; j++ ) {
	bit = gp [ i * wd + j ];
	if ( bit && ( i < top ) )
	  top = i;
	if ( bit && ( i > bot ) )
	  bot = i;
      }
    }
    
    (void) fprintf ( fout, "STARTCHAR GCID%02X\n", g );
    (void) fprintf ( fout, "ENCODING %d\n", g );
    (void) fprintf ( fout, "SWIDTH %d %d\n", ( ow & 0xff ) * 720, 0 );
    (void) fprintf ( fout, "DWIDTH %d %d\n", ( ow & 0xff ), 0 );
    (void) fprintf ( fout, "BBX %d %d %d %d\n",
	     ( coff1 - coff0 ),
	     ( bot - top ) + 1,
	     ( ( ow >> 8 ) & 0xff ) + mk,
	     ( ht - toshort ( fp->ftDescent ) ) - ( bot + 1 ) );
    (void) fprintf ( fout, "BITMAP\n" );
    for ( i = top; i <= bot; i++ ) {
      for ( j = 0, bits = 0; j < ( coff1 - coff0 ); j++, bits <<= 1 ) {
	bits |= gp [ i * wd + j ];
	if ( ( j & 7 ) == 7 ) {
	  (void) fprintf ( fout, "%02x", bits );
	  bits = 0;
	}
      }
      bits <<= 7 - ( j % 8 );
      if ( j & 7 )
	(void) fprintf ( fout, "%02x\n", bits );
      else
	(void) fprintf ( fout, "\n" );
    }
    (void) fprintf ( fout, "ENDCHAR\n" );
  }
  (void) fprintf ( fout, "ENDFONT\n" );
  (void) fclose ( fout );
  return 1;
}

