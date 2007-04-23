/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2006 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

/* This file contains portable iconv functions for SDL */

#include "SDL_stdinc.h"
#include "SDL_endian.h"

#ifdef HAVE_ICONV

#include <errno.h>

size_t SDL_iconv(SDL_iconv_t cd,
                 char **inbuf, size_t *inbytesleft,
                 char **outbuf, size_t *outbytesleft)
{
	size_t retCode = iconv(cd, inbuf, inbytesleft, outbuf, outbytesleft);
	if ( retCode == (size_t)-1 ) {
		switch(errno) {
		    case E2BIG:
			return SDL_ICONV_E2BIG;
		    case EILSEQ:
			return SDL_ICONV_EILSEQ;
		    case EINVAL:
			return SDL_ICONV_EINVAL;
		    default:
			return SDL_ICONV_ERROR;
		}
	}
	return retCode;
}

#else

/* Lots of useful information on Unicode at:
	http://www.cl.cam.ac.uk/~mgk25/unicode.html
*/

#define UNICODE_BOM	0xFEFF

#define UNKNOWN_ASCII	'?'
#define UNKNOWN_UNICODE	0xFFFD

enum {
	ENCODING_UNKNOWN,
	ENCODING_ASCII,
	ENCODING_LATIN1,
	ENCODING_UTF8,
	ENCODING_UTF16,		/* Needs byte order marker */
	ENCODING_UTF16BE,
	ENCODING_UTF16LE,
	ENCODING_UTF32,		/* Needs byte order marker */
	ENCODING_UTF32BE,
	ENCODING_UTF32LE,
	ENCODING_UCS2,		/* Native byte order assumed */
	ENCODING_UCS4,		/* Native byte order assumed */
};
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define ENCODING_UTF16NATIVE	ENCODING_UTF16BE
#define ENCODING_UTF32NATIVE	ENCODING_UTF32BE
#else
#define ENCODING_UTF16NATIVE	ENCODING_UTF16LE
#define ENCODING_UTF32NATIVE	ENCODING_UTF32LE
#endif

struct _SDL_iconv_t
{
	int src_fmt;
	int dst_fmt;
};

static struct {
	const char *name;
	int format;
} encodings[] = {
	{ "ASCII",	ENCODING_ASCII },
	{ "US-ASCII",	ENCODING_ASCII },
	{ "LATIN1",	ENCODING_LATIN1 },
	{ "ISO-8859-1",	ENCODING_LATIN1 },
	{ "UTF8",	ENCODING_UTF8 },
	{ "UTF-8",	ENCODING_UTF8 },
	{ "UTF16",	ENCODING_UTF16 },
	{ "UTF-16",	ENCODING_UTF16 },
	{ "UTF16BE",	ENCODING_UTF16BE },
	{ "UTF-16BE",	ENCODING_UTF16BE },
	{ "UTF16LE",	ENCODING_UTF16LE },
	{ "UTF-16LE",	ENCODING_UTF16LE },
	{ "UTF32",	ENCODING_UTF32 },
	{ "UTF-32",	ENCODING_UTF32 },
	{ "UTF32BE",	ENCODING_UTF32BE },
	{ "UTF-32BE",	ENCODING_UTF32BE },
	{ "UTF32LE",	ENCODING_UTF32LE },
	{ "UTF-32LE",	ENCODING_UTF32LE },
	{ "UCS2",	ENCODING_UCS2 },
	{ "UCS-2",	ENCODING_UCS2 },
	{ "UCS4",	ENCODING_UCS4 },
	{ "UCS-4",	ENCODING_UCS4 },
};

SDL_iconv_t SDL_iconv_open(const char *tocode, const char *fromcode)
{
	int src_fmt = ENCODING_UNKNOWN;
	int dst_fmt = ENCODING_UNKNOWN;
	int i;

	for ( i = 0; i < SDL_arraysize(encodings); ++i ) {
		if ( SDL_strcasecmp(fromcode, encodings[i].name) == 0 ) {
			src_fmt = encodings[i].format;
			if ( dst_fmt != ENCODING_UNKNOWN ) {
				break;
			}
		}
		if ( SDL_strcasecmp(tocode, encodings[i].name) == 0 ) {
			dst_fmt = encodings[i].format;
			if ( src_fmt != ENCODING_UNKNOWN ) {
				break;
			}
		}
	}
	if ( src_fmt != ENCODING_UNKNOWN && dst_fmt != ENCODING_UNKNOWN ) {
		SDL_iconv_t cd = (SDL_iconv_t)SDL_malloc(sizeof(*cd));
		if ( cd ) {
			cd->src_fmt = src_fmt;
			cd->dst_fmt = dst_fmt;
			return cd;
		}
	}
	return (SDL_iconv_t)-1;
}

size_t SDL_iconv(SDL_iconv_t cd,
                 char **inbuf, size_t *inbytesleft,
                 char **outbuf, size_t *outbytesleft)
{
	/* For simplicity, we'll convert everything to and from UCS-4 */
	char *src, *dst;
	size_t srclen, dstlen;
	Uint32 ch = 0;
	size_t total;

	if ( !inbuf || !*inbuf ) {
		/* Reset the context */
		return 0;
	}
	if ( !outbuf || !*outbuf || !outbytesleft || !*outbytesleft ) {
		return SDL_ICONV_E2BIG;
	}
	src = *inbuf;
	srclen = (inbytesleft ? *inbytesleft : 0);
	dst = *outbuf;
	dstlen = *outbytesleft;

	switch ( cd->src_fmt ) {
	    case ENCODING_UTF16:
		/* Scan for a byte order marker */
		{
			Uint8 *p = (Uint8 *)src;
			size_t n = srclen / 2;
			while ( n ) {
				if ( p[0] == 0xFF && p[1] == 0xFE ) {
					cd->src_fmt = ENCODING_UTF16BE;
					break;
				} else if ( p[0] == 0xFE && p[1] == 0xFF ) {
					cd->src_fmt = ENCODING_UTF16LE;
					break;
				}
				p += 2;
				--n;
			}
			if ( n == 0 ) {
				/* We can't tell, default to host order */
				cd->src_fmt = ENCODING_UTF16NATIVE;
			}
		}
		break;
	    case ENCODING_UTF32:
		/* Scan for a byte order marker */
		{
			Uint8 *p = (Uint8 *)src;
			size_t n = srclen / 4;
			while ( n ) {
				if ( p[0] == 0xFF && p[1] == 0xFE &&
				     p[2] == 0x00 && p[3] == 0x00 ) {
					cd->src_fmt = ENCODING_UTF32BE;
					break;
				} else if ( p[0] == 0x00 && p[1] == 0x00 &&
				            p[2] == 0xFE && p[3] == 0xFF ) {
					cd->src_fmt = ENCODING_UTF32LE;
					break;
				}
				p += 4;
				--n;
			}
			if ( n == 0 ) {
				/* We can't tell, default to host order */
				cd->src_fmt = ENCODING_UTF32NATIVE;
			}
		}
		break;
	}

	switch ( cd->dst_fmt ) {
	    case ENCODING_UTF16:
		/* Default to host order, need to add byte order marker */
		if ( dstlen < 2 ) {
			return SDL_ICONV_E2BIG;
		}
		*(Uint16 *)dst = UNICODE_BOM;
		dst += 2;
		dstlen -= 2;
		cd->dst_fmt = ENCODING_UTF16NATIVE;
		break;
	    case ENCODING_UTF32:
		/* Default to host order, need to add byte order marker */
		if ( dstlen < 4 ) {
			return SDL_ICONV_E2BIG;
		}
		*(Uint32 *)dst = UNICODE_BOM;
		dst += 4;
		dstlen -= 4;
		cd->dst_fmt = ENCODING_UTF32NATIVE;
		break;
	}

	total = 0;
	while ( srclen > 0 ) {
		/* Decode a character */
		switch ( cd->src_fmt ) {
		    case ENCODING_ASCII:
			{
				Uint8 *p = (Uint8 *)src;
				ch = (Uint32)(p[0] & 0x7F);
				++src;
				--srclen;
			}
			break;
		    case ENCODING_LATIN1:
			{
				Uint8 *p = (Uint8 *)src;
				ch = (Uint32)p[0];
				++src;
				--srclen;
			}
			break;
		    case ENCODING_UTF8: /* RFC 3629 */
			{
				Uint8 *p = (Uint8 *)src;
				size_t left = 0;
				SDL_bool overlong = SDL_FALSE;
				if ( p[0] >= 0xFC ) {
					if ( (p[0] & 0xFE) != 0xFC ) {
						/* Skip illegal sequences
						return SDL_ICONV_EILSEQ;
						*/
						ch = UNKNOWN_UNICODE;
					} else {
						if ( p[0] == 0xFC ) {
							overlong = SDL_TRUE;
						}
						ch = (Uint32)(p[0] & 0x01);
						left = 5;
					}
				} else if ( p[0] >= 0xF8 ) {
					if ( (p[0] & 0xFC) != 0xF8 ) {
						/* Skip illegal sequences
						return SDL_ICONV_EILSEQ;
						*/
						ch = UNKNOWN_UNICODE;
					} else {
						if ( p[0] == 0xF8 ) {
							overlong = SDL_TRUE;
						}
						ch = (Uint32)(p[0] & 0x03);
						left = 4;
					}
				} else if ( p[0] >= 0xF0 ) {
					if ( (p[0] & 0xF8) != 0xF0 ) {
						/* Skip illegal sequences
						return SDL_ICONV_EILSEQ;
						*/
						ch = UNKNOWN_UNICODE;
					} else {
						if ( p[0] == 0xF0 ) {
							overlong = SDL_TRUE;
						}
						ch = (Uint32)(p[0] & 0x07);
						left = 3;
					}
				} else if ( p[0] >= 0xE0 ) {
					if ( (p[0] & 0xF0) != 0xE0 ) {
						/* Skip illegal sequences
						return SDL_ICONV_EILSEQ;
						*/
						ch = UNKNOWN_UNICODE;
					} else {
						if ( p[0] == 0xE0 ) {
							overlong = SDL_TRUE;
						}
						ch = (Uint32)(p[0] & 0x0F);
						left = 2;
					}
				} else if ( p[0] >= 0xC0 ) {
					if ( (p[0] & 0xE0) != 0xC0 ) {
						/* Skip illegal sequences
						return SDL_ICONV_EILSEQ;
						*/
						ch = UNKNOWN_UNICODE;
					} else {
						if ( (p[0] & 0xCE) == 0xC0 ) {
							overlong = SDL_TRUE;
						}
						ch = (Uint32)(p[0] & 0x1F);
						left = 1;
					}
				} else {
					if ( (p[0] & 0x80) != 0x00 ) {
						/* Skip illegal sequences
						return SDL_ICONV_EILSEQ;
						*/
						ch = UNKNOWN_UNICODE;
					} else {
						ch = (Uint32)p[0];
					}
				}
				++src;
				--srclen;
				if ( srclen < left ) {
					return SDL_ICONV_EINVAL;
				}
				while ( left-- ) {
					++p;
					if ( (p[0] & 0xC0) != 0x80 ) {
						/* Skip illegal sequences
						return SDL_ICONV_EILSEQ;
						*/
						ch = UNKNOWN_UNICODE;
						break;
					}
					ch <<= 6;
					ch |= (p[0] & 0x3F);
					++src;
					--srclen;
				}
				if ( overlong ) {
					/* Potential security risk
					return SDL_ICONV_EILSEQ;
					*/
					ch = UNKNOWN_UNICODE;
				}
				if ( (ch >= 0xD800 && ch <= 0xDFFF) ||
				     (ch == 0xFFFE || ch == 0xFFFF) ||
				     ch > 0x10FFFF ) {
					/* Skip illegal sequences
					return SDL_ICONV_EILSEQ;
					*/
					ch = UNKNOWN_UNICODE;
				}
			}
			break;
		    case ENCODING_UTF16BE: /* RFC 2781 */
			{
				Uint8 *p = (Uint8 *)src;
				Uint16 W1, W2;
				if ( srclen < 2 ) {
					return SDL_ICONV_EINVAL;
				}
				W1 = ((Uint16)p[0] << 8) |
				      (Uint16)p[1];
				src += 2;
				srclen -= 2;
				if ( W1 < 0xD800 || W1 > 0xDFFF ) {
					ch = (Uint32)W1;
					break;
				}
				if ( W1 > 0xDBFF ) {
					/* Skip illegal sequences
					return SDL_ICONV_EILSEQ;
					*/
					ch = UNKNOWN_UNICODE;
					break;
				}
				if ( srclen < 2 ) {
					return SDL_ICONV_EINVAL;
				}
				p = (Uint8 *)src;
				W2 = ((Uint16)p[0] << 8) |
				      (Uint16)p[1];
				src += 2;
				srclen -= 2;
				if ( W2 < 0xDC00 || W2 > 0xDFFF ) {
					/* Skip illegal sequences
					return SDL_ICONV_EILSEQ;
					*/
					ch = UNKNOWN_UNICODE;
					break;
				}
				ch = (((Uint32)(W1 & 0x3FF) << 10) |
				      (Uint32)(W2 & 0x3FF)) + 0x10000;
			}
			break;
		    case ENCODING_UTF16LE: /* RFC 2781 */
			{
				Uint8 *p = (Uint8 *)src;
				Uint16 W1, W2;
				if ( srclen < 2 ) {
					return SDL_ICONV_EINVAL;
				}
				W1 = ((Uint16)p[1] << 8) |
				      (Uint16)p[0];
				src += 2;
				srclen -= 2;
				if ( W1 < 0xD800 || W1 > 0xDFFF ) {
					ch = (Uint32)W1;
					break;
				}
				if ( W1 > 0xDBFF ) {
					/* Skip illegal sequences
					return SDL_ICONV_EILSEQ;
					*/
					ch = UNKNOWN_UNICODE;
					break;
				}
				if ( srclen < 2 ) {
					return SDL_ICONV_EINVAL;
				}
				p = (Uint8 *)src;
				W2 = ((Uint16)p[1] << 8) |
				      (Uint16)p[0];
				src += 2;
				srclen -= 2;
				if ( W2 < 0xDC00 || W2 > 0xDFFF ) {
					/* Skip illegal sequences
					return SDL_ICONV_EILSEQ;
					*/
					ch = UNKNOWN_UNICODE;
					break;
				}
				ch = (((Uint32)(W1 & 0x3FF) << 10) |
				      (Uint32)(W2 & 0x3FF)) + 0x10000;
			}
			break;
		    case ENCODING_UTF32BE:
			{
				Uint8 *p = (Uint8 *)src;
				if ( srclen < 4 ) {
					return SDL_ICONV_EINVAL;
				}
				ch = ((Uint32)p[0] << 24) |
				     ((Uint32)p[1] << 16) |
				     ((Uint32)p[2] << 8) |
				      (Uint32)p[3];
				src += 4;
				srclen -= 4;
			}
			break;
		    case ENCODING_UTF32LE:
			{
				Uint8 *p = (Uint8 *)src;
				if ( srclen < 4 ) {
					return SDL_ICONV_EINVAL;
				}
				ch = ((Uint32)p[3] << 24) |
				     ((Uint32)p[2] << 16) |
				     ((Uint32)p[1] << 8) |
				      (Uint32)p[0];
				src += 4;
				srclen -= 4;
			}
			break;
		    case ENCODING_UCS2:
			{
				Uint16 *p = (Uint16 *)src;
				if ( srclen < 2 ) {
					return SDL_ICONV_EINVAL;
				}
				ch = *p;
				src += 2;
				srclen -= 2;
			}
			break;
		    case ENCODING_UCS4:
			{
				Uint32 *p = (Uint32 *)src;
				if ( srclen < 4 ) {
					return SDL_ICONV_EINVAL;
				}
				ch = *p;
				src += 4;
				srclen -= 4;
			}
			break;
		}

		/* Encode a character */
		switch ( cd->dst_fmt ) {
		    case ENCODING_ASCII:
			{
				Uint8 *p = (Uint8 *)dst;
				if ( dstlen < 1 ) {
					return SDL_ICONV_E2BIG;
				}
				if ( ch > 0x7F ) {
					*p = UNKNOWN_ASCII;
				} else {
					*p = (Uint8)ch;
				}
				++dst;
				--dstlen;
			}
			break;
		    case ENCODING_LATIN1:
			{
				Uint8 *p = (Uint8 *)dst;
				if ( dstlen < 1 ) {
					return SDL_ICONV_E2BIG;
				}
				if ( ch > 0xFF ) {
					*p = UNKNOWN_ASCII;
				} else {
					*p = (Uint8)ch;
				}
				++dst;
				--dstlen;
			}
			break;
		    case ENCODING_UTF8: /* RFC 3629 */
			{
				Uint8 *p = (Uint8 *)dst;
				if ( ch > 0x10FFFF ) {
					ch = UNKNOWN_UNICODE;
				}
				if ( ch <= 0x7F ) {
					if ( dstlen < 1 ) {
						return SDL_ICONV_E2BIG;
					}
					*p = (Uint8)ch;
					++dst;
					--dstlen;
				} else if ( ch <= 0x7FF ) {
					if ( dstlen < 2 ) {
						return SDL_ICONV_E2BIG;
					}
					p[0] = 0xC0 | (Uint8)((ch >> 6) & 0x1F);
					p[1] = 0x80 | (Uint8)(ch & 0x3F);
					dst += 2;
					dstlen -= 2;
				} else if ( ch <= 0xFFFF ) {
					if ( dstlen < 3 ) {
						return SDL_ICONV_E2BIG;
					}
					p[0] = 0xE0 | (Uint8)((ch >> 12) & 0x0F);
					p[1] = 0x80 | (Uint8)((ch >> 6) & 0x3F);
					p[2] = 0x80 | (Uint8)(ch & 0x3F);
					dst += 3;
					dstlen -= 3;
				} else if ( ch <= 0x1FFFFF ) {
					if ( dstlen < 4 ) {
						return SDL_ICONV_E2BIG;
					}
					p[0] = 0xF0 | (Uint8)((ch >> 18) & 0x07);
					p[1] = 0x80 | (Uint8)((ch >> 12) & 0x3F);
					p[2] = 0x80 | (Uint8)((ch >> 6) & 0x3F);
					p[3] = 0x80 | (Uint8)(ch & 0x3F);
					dst += 4;
					dstlen -= 4;
				} else if ( ch <= 0x3FFFFFF ) {
					if ( dstlen < 5 ) {
						return SDL_ICONV_E2BIG;
					}
					p[0] = 0xF8 | (Uint8)((ch >> 24) & 0x03);
					p[1] = 0x80 | (Uint8)((ch >> 18) & 0x3F);
					p[2] = 0x80 | (Uint8)((ch >> 12) & 0x3F);
					p[3] = 0x80 | (Uint8)((ch >> 6) & 0x3F);
					p[4] = 0x80 | (Uint8)(ch & 0x3F);
					dst += 5;
					dstlen -= 5;
				} else {
					if ( dstlen < 6 ) {
						return SDL_ICONV_E2BIG;
					}
					p[0] = 0xFC | (Uint8)((ch >> 30) & 0x01);
					p[1] = 0x80 | (Uint8)((ch >> 24) & 0x3F);
					p[2] = 0x80 | (Uint8)((ch >> 18) & 0x3F);
					p[3] = 0x80 | (Uint8)((ch >> 12) & 0x3F);
					p[4] = 0x80 | (Uint8)((ch >> 6) & 0x3F);
					p[5] = 0x80 | (Uint8)(ch & 0x3F);
					dst += 6;
					dstlen -= 6;
				}
			}
			break;
		    case ENCODING_UTF16BE: /* RFC 2781 */
			{
				Uint8 *p = (Uint8 *)dst;
				if ( ch > 0x10FFFF ) {
					ch = UNKNOWN_UNICODE;
				}
				if ( ch < 0x10000 ) {
					if ( dstlen < 2 ) {
						return SDL_ICONV_E2BIG;
					}
					p[0] = (Uint8)(ch >> 8);
					p[1] = (Uint8)ch;
					dst += 2;
					dstlen -= 2;
				} else {
					Uint16 W1, W2;
					if ( dstlen < 4 ) {
						return SDL_ICONV_E2BIG;
					}
					ch = ch - 0x10000;
					W1 = 0xD800 | (Uint16)((ch >> 10) & 0x3FF);
					W2 = 0xDC00 | (Uint16)(ch & 0x3FF);
					p[0] = (Uint8)(W1 >> 8);
					p[1] = (Uint8)W1;
					p[2] = (Uint8)(W2 >> 8);
					p[3] = (Uint8)W2;
					dst += 4;
					dstlen -= 4;
				}
			}
			break;
		    case ENCODING_UTF16LE: /* RFC 2781 */
			{
				Uint8 *p = (Uint8 *)dst;
				if ( ch > 0x10FFFF ) {
					ch = UNKNOWN_UNICODE;
				}
				if ( ch < 0x10000 ) {
					if ( dstlen < 2 ) {
						return SDL_ICONV_E2BIG;
					}
					p[1] = (Uint8)(ch >> 8);
					p[0] = (Uint8)ch;
					dst += 2;
					dstlen -= 2;
				} else {
					Uint16 W1, W2;
					if ( dstlen < 4 ) {
						return SDL_ICONV_E2BIG;
					}
					ch = ch - 0x10000;
					W1 = 0xD800 | (Uint16)((ch >> 10) & 0x3FF);
					W2 = 0xDC00 | (Uint16)(ch & 0x3FF);
					p[1] = (Uint8)(W1 >> 8);
					p[0] = (Uint8)W1;
					p[3] = (Uint8)(W2 >> 8);
					p[2] = (Uint8)W2;
					dst += 4;
					dstlen -= 4;
				}
			}
			break;
		    case ENCODING_UTF32BE:
			{
				Uint8 *p = (Uint8 *)dst;
				if ( ch > 0x10FFFF ) {
					ch = UNKNOWN_UNICODE;
				}
				if ( dstlen < 4 ) {
					return SDL_ICONV_E2BIG;
				}
				p[0] = (Uint8)(ch >> 24);
				p[1] = (Uint8)(ch >> 16);
				p[2] = (Uint8)(ch >> 8);
				p[3] = (Uint8)ch;
				dst += 4;
				dstlen -= 4;
			}
			break;
		    case ENCODING_UTF32LE:
			{
				Uint8 *p = (Uint8 *)dst;
				if ( ch > 0x10FFFF ) {
					ch = UNKNOWN_UNICODE;
				}
				if ( dstlen < 4 ) {
					return SDL_ICONV_E2BIG;
				}
				p[3] = (Uint8)(ch >> 24);
				p[2] = (Uint8)(ch >> 16);
				p[1] = (Uint8)(ch >> 8);
				p[0] = (Uint8)ch;
				dst += 4;
				dstlen -= 4;
			}
			break;
		    case ENCODING_UCS2:
			{
				Uint16 *p = (Uint16 *)dst;
				if ( ch > 0xFFFF ) {
					ch = UNKNOWN_UNICODE;
				}
				if ( dstlen < 2 ) {
					return SDL_ICONV_E2BIG;
				}
				*p = (Uint16)ch;
				dst += 2;
				dstlen -= 2;
			}
			break;
		    case ENCODING_UCS4:
			{
				Uint32 *p = (Uint32 *)dst;
				if ( ch > 0x7FFFFFFF ) {
					ch = UNKNOWN_UNICODE;
				}
				if ( dstlen < 4 ) {
					return SDL_ICONV_E2BIG;
				}
				*p = ch;
				dst += 4;
				dstlen -= 4;
			}
			break;
		}

		/* Update state */
		*inbuf = src;
		*inbytesleft = srclen;
		*outbuf = dst;
		*outbytesleft = dstlen;
		++total;
	}
	return total;
}

int SDL_iconv_close(SDL_iconv_t cd)
{
	if ( cd && cd != (SDL_iconv_t)-1 ) {
		SDL_free(cd);
	}
	return 0;
}

#endif /* !HAVE_ICONV */

char *SDL_iconv_string(const char *tocode, const char *fromcode, char *inbuf, size_t inbytesleft)
{
	SDL_iconv_t cd;
	char *string;
	size_t stringsize;
	char *outbuf;
	size_t outbytesleft;
	size_t retCode = 0;

	cd = SDL_iconv_open(tocode, fromcode);
	if ( cd == (SDL_iconv_t)-1 ) {
		return NULL;
	}

	stringsize = inbytesleft > 4 ? inbytesleft : 4;
	string = SDL_malloc(stringsize);
	if ( !string ) {
		SDL_iconv_close(cd);
		return NULL;
	}
	outbuf = string;
	outbytesleft = stringsize;
	SDL_memset(outbuf, 0, 4);

	while ( inbytesleft > 0 ) {
		retCode = SDL_iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
		switch (retCode) {
		    case SDL_ICONV_E2BIG:
			{
				char *oldstring = string;
				stringsize *= 2;
				string = SDL_realloc(string, stringsize);
				if ( !string ) {
					SDL_iconv_close(cd);
					return NULL;
				}
				outbuf = string + (outbuf - oldstring);
				outbytesleft = stringsize - (outbuf - string);
				SDL_memset(outbuf, 0, 4);
			}
			break;
		    case SDL_ICONV_EILSEQ:
			/* Try skipping some input data - not perfect, but... */
			++inbuf;
			--inbytesleft;
			break;
		    case SDL_ICONV_EINVAL:
		    case SDL_ICONV_ERROR:
			/* We can't continue... */
			inbytesleft = 0;
			break;
		}
	}
	SDL_iconv_close(cd);

	return string;
}
