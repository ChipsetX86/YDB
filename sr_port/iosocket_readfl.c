/****************************************************************
 *								*
 *	Copyright 2001, 2006 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/* iosocket_readfl.c */
#include "mdef.h"
#include <errno.h>
#include "gtm_stdio.h"
#include "gtm_time.h"
#ifdef __MVS__
#include <sys/time.h>
#endif
#include "gtm_socket.h"
#include "gtm_inet.h"
#include "gtm_string.h"
#ifdef UNIX
#include "gtm_fcntl.h"
#include "eintr_wrappers.h"
#endif
#include "gt_timer.h"
#include "io.h"
#include "iotimer.h"
#include "iotcpdef.h"
#include "iotcproutine.h"
#include "stringpool.h"
#include "iosocketdef.h"
#include "min_max.h"
#include "outofband.h"
#include "wake_alarm.h"
#include "gtm_conv.h"
#include "gtm_utf8.h"

#define	TIMEOUT_INTERVAL	200000

GBLREF	io_pair 		io_curr_device;
GBLREF	bool			out_of_time;
GBLREF	spdesc 			stringpool;
GBLREF	volatile int4		outofband;
GBLREF	mstr			chset_names[];
GBLREF	UConverter		*chset_desc[];

#ifdef UNICODE_SUPPORTED
/* Maintenance of $KEY, $DEVICE and $ZB on a badchar error */
void iosocket_readfl_badchar(mval *vmvalptr, int datalen, int delimlen, unsigned char *delimptr, unsigned char *strend)
{
	int		tmplen, len;
	unsigned char	*delimend;
 	io_desc		*iod;
	d_socket_struct	*dsocketptr;

	iod = io_curr_device.in;
	vmvalptr->str.len = datalen;
	vmvalptr->str.addr = (char *)stringpool.free;
	if (0 < datalen)
	{	/* Return how much input we got */
                if (CHSET_M != iod->ichset && CHSET_UTF8 != iod->ichset)
                {
                        SOCKET_DEBUG(PRINTF("socrflbc: Converting UTF16xx data back to UTF8 for internal use\n"); fflush(stdout));
                        vmvalptr->str.len = gtm_conv(chset_desc[iod->ichset], chset_desc[CHSET_UTF8], &vmvalptr->str, NULL, NULL);
                        vmvalptr->str.addr = (char *)stringpool.free;
                }
		stringpool.free += vmvalptr->str.len;
	}
	if (NULL != strend && NULL != delimptr)
	{	/* First find the end of the delimiter (max of 4 bytes) */
		if (0 == delimlen)
		{
			for (delimend = delimptr; GTM_MB_LEN_MAX >= delimlen && delimend < strend; ++delimend, ++delimlen)
			{
				if (UTF8_VALID(delimend, strend, tmplen))
					break;
			}
		}
		if (0 < delimlen)
		{	/* Set $KEY and $ZB with the failing badchar */
			dsocketptr = (d_socket_struct *)(iod->dev_sp);
			memcpy(iod->dollar.zb, delimptr, MIN(delimlen, ESC_LEN - 1));
			iod->dollar.zb[MIN(delimlen, ESC_LEN - 1)] = '\0';
			memcpy(dsocketptr->dollar_key, delimptr, MIN(delimlen, DD_BUFLEN - 1));
			dsocketptr->dollar_key[MIN(delimlen, DD_BUFLEN - 1)] = '\0';
		}
	}
	len = sizeof(ONE_COMMA) - 1;
	memcpy(dsocketptr->dollar_device, ONE_COMMA, len);
	memcpy(&dsocketptr->dollar_device[len], BADCHAR_DEVICE_MSG, sizeof(BADCHAR_DEVICE_MSG));
}
#endif

/* VMS uses the UCX interface; should support others that emulate it */
int	iosocket_readfl(mval *v, int4 width, int4 timeout)
	/* width == 0 is a flag for non-fixed length read */
					/* timeout in seconds */
{
	int		ret;
	boolean_t 	timed, vari, more_data, terminator, has_delimiter, requeue_done;
	int		flags, len, real_errno, save_errno, fcntl_res, errlen, charlen;
	int		bytes_read, orig_bytes_read, ii, max_bufflen, bufflen_init, bufflen, chars_read, mb_len, match_delim;
	io_desc		*iod;
	d_socket_struct	*dsocketptr;
	socket_struct	*socketptr;
	int4		msec_timeout; /* timeout in milliseconds */
	TID		timer_id;
	ABS_TIME	cur_time, end_time, time_for_read, zero;
	char		*errptr;
	unsigned char	*buffptr, *c_ptr, *c_top, *inv_beg;
	ssize_t		status;
	gtm_chset_t	ichset;

	error_def(ERR_IOEOF);
	error_def(ERR_TEXT);
	error_def(ERR_CURRSOCKOFR);
	error_def(ERR_NOSOCKETINDEV);
	error_def(ERR_GETSOCKOPTERR);
	error_def(ERR_SETSOCKOPTERR);
	error_def(ERR_MAXSTRLEN);
	error_def(ERR_BOMMISMATCH);

	assert(stringpool.free >= stringpool.base);
	assert(stringpool.free <= stringpool.top);
	if (0 == width)
	{	/* op_readfl won't do this; must be a call from iosocket_read */
		vari = TRUE;
		width = MAX_STRLEN;
	} else
	{
		vari = FALSE;
		width = (width < MAX_STRLEN) ? width : MAX_STRLEN;
	}
	/* if width is set to MAX_STRLEN, we might be overly generous (assuming every char is just one byte) we must check if byte
	 * length crosses the MAX_STRLEN boundary */

	iod = io_curr_device.in;
	ichset = iod->ichset;
	assert(dev_open == iod->state);
	assert(gtmsocket == iod->type);
	dsocketptr = (d_socket_struct *)(iod->dev_sp);
	socketptr = dsocketptr->socket[dsocketptr->current_socket];
	if (0 >= dsocketptr->n_socket)
	{
		iod->dollar.za = 9;
		rts_error(VARLSTCNT(1) ERR_NOSOCKETINDEV);
		return 0;
	}
	if (dsocketptr->n_socket <= dsocketptr->current_socket)
        {
		iod->dollar.za = 9;
		rts_error(VARLSTCNT(4) ERR_CURRSOCKOFR, 2, dsocketptr->current_socket, dsocketptr->n_socket);
                return 0;
        }
	if (iod->dollar.x  &&  (TCP_WRITE == socketptr->lastop))
	{	/* switching from write to read */
		if (!iod->dollar.za)
			iosocket_flush(iod);
		iod->dollar.x = 0;
	}
	socketptr->lastop = TCP_READ;
	ret = TRUE;
	has_delimiter = (0 < socketptr->n_delimiter);
	time_for_read.at_sec  = 0;
	time_for_read.at_usec = ((0 == timeout) ? 0 : TIMEOUT_INTERVAL);
	/* ATTN: the 200000 above is mystery. This should be machine dependent 	*/
	/*       When setting it, following aspects needed to be considered	*/
	/*	 1. Not too long to let users at direct mode feel uncomfortable */
	/*		i.e. r x  will wait for this long to return		*/
	/*		     	even when there is something to read from the 	*/
	/*			socket 						*/
	/*	 2. Not too short so that when it is waiting for something 	*/
	/* 	    from a socket, it won't load up the CPU. This shall be able */
	/*	    to be omited when the next item is considered.		*/
	/*	 3. Not too short so that it won't cut one message into pieces	*/
	/*	    when the read is issued first.				*/
	/*		for w "abcd", 10 us will do it				*/
	/* 		for w "ab",!,"cd",!,"ef"  it will have to be larger     */
	/*			than 50000 us on beowulf.			*/
	/* This is gonna be a headache.						*/
	timer_id = (TID)iosocket_readfl;
	out_of_time = FALSE;
	if (NO_M_TIMEOUT == timeout)
	{
		timed = FALSE;
		msec_timeout = NO_M_TIMEOUT;
	} else
	{
		timed = TRUE;
		msec_timeout = timeout2msec(timeout);
		if (msec_timeout > 0)
		{	/* there is time to wait */
#ifdef UNIX
			/* set blocking I/O */
			FCNTL2(socketptr->sd, F_GETFL, flags);
			if (flags < 0)
			{
				iod->dollar.za = 9;
				save_errno = errno;
				errptr = (char *)STRERROR(errno);
				rts_error(VARLSTCNT(7) ERR_GETSOCKOPTERR, 5, LEN_AND_LIT("F_GETFL FOR NON BLOCKING I/O"),
					  save_errno, LEN_AND_STR(errptr));
			}
			FCNTL3(socketptr->sd, F_SETFL, flags & (~(O_NDELAY | O_NONBLOCK)), fcntl_res);
			if (fcntl_res < 0)
			{
				iod->dollar.za = 9;
				save_errno = errno;
				errptr = (char *)STRERROR(errno);
				rts_error(VARLSTCNT(7) ERR_SETSOCKOPTERR, 5, LEN_AND_LIT("F_SETFL FOR NON BLOCKING I/O"),
					  save_errno, LEN_AND_STR(errptr));
			}
#endif
			sys_get_curr_time(&cur_time);
			add_int_to_abs_time(&cur_time, msec_timeout, &end_time);
			start_timer(timer_id, msec_timeout, wake_alarm, 0, NULL);
		} else
			out_of_time = TRUE;
	}
	dsocketptr->dollar_key[0] = '\0';
	iod->dollar.zb[0] = '\0';
	more_data = TRUE;

	bufflen_init = MAX_STRBUFF_INIT;
	/* compute the worst case byte requirement knowing that width is in chars */
	max_bufflen = (vari) ? bufflen_init : ((MAX_STRLEN > (width * 4)) ? (width * 4) : MAX_STRLEN);
	if (stringpool.free + max_bufflen > stringpool.top)
		stp_gcol(max_bufflen);
	real_errno = 0;
	requeue_done = FALSE;
	SOCKET_DEBUG(PRINTF("socrfl: ############################### Entering read loop ################################\n");
		     fflush(stdout));
	for (bytes_read = 0, chars_read = 0, status = 0;  status >= 0;)
	{
		SOCKET_DEBUG(PRINTF("socrfl: *************** Top of read loop - bytes_read: %d  chars_read: %d  "
				    "buffered_length: %d\n", bytes_read, chars_read, socketptr->buffered_length); fflush(stdout));
		SOCKET_DEBUG(PRINTF("socrfl: *************** read-width: %d  vari: %d  status: %d\n", width, vari, status);
			     fflush(stdout));
		if (bytes_read >= max_bufflen)
		{	/* more buffer needed. Extend the stringpool buffer by doubling the size as much as we
			   extended previously */
			SOCKET_DEBUG(PRINTF("socrfl: Buffer expand1 bytes_read(%d) max_bufflen(%d) -- bufflen_init(%d)\n",
					    bytes_read, max_bufflen, bufflen_init); fflush(stdout));
			assert(vari);
			bufflen_init += bufflen_init;
			assert(bufflen_init <= MAX_STRLEN);
			if (stringpool.free + bytes_read + bufflen_init > stringpool.top)
			{
				v->str.len = bytes_read; /* to keep the data read so far from being garbage collected */
				v->str.addr = (char *)stringpool.free;
				stringpool.free += v->str.len;
				assert(stringpool.free <= stringpool.top);
				stp_gcol(bufflen_init);
				memcpy(stringpool.free, v->str.addr, v->str.len);
			}
			max_bufflen = bufflen_init;
		}
		if (has_delimiter || requeue_done || (socketptr->first_read && CHSET_M != ichset))
		{	/* Delimiter scanning needs one char at a time. Question is how big is a char?
			   For the UTF character sets, we have a similar issue (with the same solution) in that
			   we need to make sure the entire BOM we may have is in the buffer. If the last read
			   caused chars to be requeued, we have to make sure that we read in one full character
			   (buffer likely contains only a partial char) in order to make forward progess. If we
			   didn't do this, we would just pull the partial char out of the buffer, notice its incompleteness
			   and requeue it again and again. Forcing a full char read lets us continue past this point.
			*/
			requeue_done = FALSE;	/* housekeeping -- We don't want to come back here for this reason
						   until it happens again */
			SOCKET_DEBUG(if (socketptr->first_read && CHSET_M != ichset)
				             PRINTF("socrfl: Prebuffering because ichset = UTF16\n");
				     else PRINTF("socrfl: Prebuffering because we have delimiter or requeue\n"); fflush(stdout));
			if (CHSET_M == iod->ichset)
				bufflen = 1;		/* M mode, 1 char == 1 byte */
			else
			{	/* We need to make sure the read buffer contains a complete UTF character and to make sure
				   we know how long that character is so we can read it completely. The issue is that if we
				   read a partial utf character, the utf checking code below will notice this and rebuffer it
				   so that it gets re-read on the next iteration. However, this will then re-read the same
				   partial character and we have a loop. We make a call here which will take a peek at the
				   first byte of the next character (and read it in if necessary), determine how long the
				   character is and verify that many characters are available in the buffer and return the
				   character length to us to use for bufflen.
				*/
				charlen = iosocket_snr_utf_prebuffer(iod, socketptr, 0, &time_for_read,
								     (!vari || has_delimiter || 0 == chars_read));
				SOCKET_DEBUG(PRINTF("socrfl: charlen from iosocket_snr_utf_prebuffer = %d\n", charlen);
					     fflush(stdout));
				if (0 < charlen)
				{	/* We know how long the next char is. If it will fit in our buffer, then it is
					   the correct bufflen. If it won't, our buffer is full and we need to expand.
					*/
					if ((max_bufflen - bytes_read) < charlen)
					{
						SOCKET_DEBUG(PRINTF("socrfl: Buffer expand2 - max_bufflen(%d) "
								    "bytes_read(%d) bufflen_init(%d)\n",
								    max_bufflen, bytes_read, bufflen_init); fflush(stdout));
						assert(vari);
						bufflen_init += bufflen_init;
						assert(bufflen_init <= MAX_STRLEN);
						if (stringpool.free + bytes_read + bufflen_init > stringpool.top)
						{
							v->str.len = bytes_read; /* to keep the data read so far from
										    being garbage collected */
							v->str.addr = (char *)stringpool.free;
							stringpool.free += v->str.len;
							assert(stringpool.free <= stringpool.top);
							stp_gcol(bufflen_init);
							memcpy(stringpool.free, v->str.addr, v->str.len);
						}
					}
					bufflen = charlen;
				} else if (0 == charlen)
				{	/* No data was available or there was a timeout. We set status to -3 here
					   so that we end up bypassing the call to iosocket_snr below which was to
					   do the actual IO. No need to repeat our lack of IO issue.
					*/
					SOCKET_DEBUG(PRINTF("socrfl: Timeout or end of data stream\n"); fflush(stdout));
					status = -3;		/* To differentiate from status=0 if prebuffer bypassed */
					DEBUG_ONLY(bufflen = 0);  /* Triggers assert in iosocket_snr if we end up there anyway */
				} else
				{	/* Something bad happened. Feed the error to the lower levels for proper handling */
					SOCKET_DEBUG(PRINTF("socrfl: Read error: status: %d  errno: %d\n", charlen, errno);
						     fflush(stdout));
					status = charlen;
                                        DEBUG_ONLY(bufflen = 0);  /* Triggers assert in iosocket_snr if we end up there anyway */
				}
			}
		} else
		{
			bufflen = max_bufflen - bytes_read;
			SOCKET_DEBUG(PRINTF("socrfl: Regular read .. bufflen = %d\n", bufflen); fflush(stdout));
			status = 0;
		}
		buffptr = (stringpool.free + bytes_read);
		terminator = FALSE;
		if (0 <= status)
			/* An IO above in prebuffering may have failed in which case we do not want to reset status */
			status = iosocket_snr(socketptr, buffptr, bufflen, 0, &time_for_read);
		else
		{
			SOCKET_DEBUG(PRINTF("socrfl: Data read bypassed - status: %d\n", status); fflush(stdout));
		}
		if (0 == status || -3 == status)	/* -3 status can happen on EOB from prebuffering */
		{
			SOCKET_DEBUG(PRINTF("socrfl: No more data available\n"); fflush(stdout));
			more_data = FALSE;
			status = 0;			/* Consistent treatment of no more data */
		} else if (0 < status)
		{
			SOCKET_DEBUG(PRINTF("socrfl: Bytes read: %d\n", status); fflush(stdout));
			bytes_read += status;
			if (socketptr->first_read && CHSET_M != ichset) /* May have a BOM to defuse */
			{
				if (CHSET_UTF8 != ichset)
				{	/* When the type is UTF16xx, we need to check for a BOM at the beginning of the file. If
					   found it will tell us which of UTF-16BE (default if no BOM) or UTF-16LE mode the data
					   is being written with. The call to iosocket_snr_utf_prebuffer() above should have made
					   sure that there were at least two chars available in the buffer if the char is a BOM.
					*/
					if (UTF16BE_BOM_LEN <= bytes_read)	/* All UTF16xx BOM lengths are the same */
					{
						if (0 == memcmp(buffptr, UTF16BE_BOM, UTF16BE_BOM_LEN))
						{
							if (CHSET_UTF16LE == ichset)
							{
								iod->dollar.za = 9;
								rts_error(VARLSTCNT(6) ERR_BOMMISMATCH, 4,
									  chset_names[CHSET_UTF16BE].len,
									  chset_names[CHSET_UTF16BE].addr,
									  chset_names[CHSET_UTF16LE].len,
									  chset_names[CHSET_UTF16LE].addr);
							} else
							{
								iod->ichset = ichset = CHSET_UTF16BE;
								bytes_read -= UTF16BE_BOM_LEN;	/* Throw way BOM */
								SOCKET_DEBUG(PRINTF("socrfl: UTF16BE BOM detected\n");
									     fflush(stdout));
							}
						} else if (0 == memcmp(buffptr, UTF16LE_BOM, UTF16LE_BOM_LEN))
                                                {
                                                        if (CHSET_UTF16BE == ichset)
							{
								iod->dollar.za = 9;
                                                                rts_error(VARLSTCNT(6) ERR_BOMMISMATCH, 4,
                                                                          chset_names[CHSET_UTF16LE].len,
                                                                          chset_names[CHSET_UTF16LE].addr,
                                                                          chset_names[CHSET_UTF16BE].len,
                                                                          chset_names[CHSET_UTF16BE].addr);
							} else
							{
                                                                iod->ichset = ichset = CHSET_UTF16LE;
								bytes_read -= UTF16BE_BOM_LEN;	/* Throw away BOM */
                                                                SOCKET_DEBUG(PRINTF("socrfl: UTF16LE BOM detected\n");
                                                                             fflush(stdout));
                                                        }
						} else
						{	/* No BOM specified. If UTF16, default BOM to UTF16BE per Unicode
							   standard
							*/
							if (CHSET_UTF16 == ichset)
							{
								SOCKET_DEBUG(PRINTF("socrfl: UTF16BE BOM assumed\n");
									     fflush(stdout));
								iod->ichset = ichset = CHSET_UTF16BE;
							}
						}
					} else
					{	/* Insufficient characters to form a BOM so no BOM present. Like above, if in
						   UTF16 mode, default to UTF16BE per the Unicode standard.
						*/
						if (CHSET_UTF16 == ichset)
						{
							SOCKET_DEBUG(PRINTF("socrfl: UTF16BE BOM assumed\n");
								     fflush(stdout));
							iod->ichset = ichset = CHSET_UTF16BE;
						}
					}
				} else
				{	/* Check for UTF8 BOM. If found, just eliminate it. */
                                        if (UTF8_BOM_LEN <= bytes_read && (0 == memcmp(buffptr, UTF8_BOM, UTF8_BOM_LEN)))
					{
						bytes_read -= UTF8_BOM_LEN;        /* Throw way BOM */
						SOCKET_DEBUG(PRINTF("socrfl: UTF8 BOM detected/ignored\n");
							     fflush(stdout));
					}
				}
			}
			if (socketptr->first_read)
			{
				if  (CHSET_UTF16BE == ichset || CHSET_UTF16LE == ichset)
				{
					get_chset_desc(&chset_names[ichset]);
					if (has_delimiter)
						iosocket_delim_conv(socketptr, ichset);
				}
				socketptr->first_read = FALSE;
			}
			if (bytes_read && has_delimiter)
			{ /* ------- check to see if it is a delimiter -------- */
				SOCKET_DEBUG(PRINTF("socrfl: Searching for delimiter\n"); fflush(stdout));
				for (ii = 0; ii < socketptr->n_delimiter; ii++)
				{
					if (bytes_read < socketptr->idelimiter[ii].len)
						continue;
					if (0 == memcmp(socketptr->idelimiter[ii].addr,
							stringpool.free + bytes_read - socketptr->idelimiter[ii].len,
							socketptr->idelimiter[ii].len))
					{
						terminator = TRUE;
						match_delim = ii;
						memcpy(iod->dollar.zb, socketptr->idelimiter[ii].addr,
						       MIN(socketptr->idelimiter[ii].len, ESC_LEN - 1));
						iod->dollar.zb[MIN(socketptr->idelimiter[ii].len, ESC_LEN - 1)] = '\0';
						memcpy(dsocketptr->dollar_key, socketptr->idelimiter[ii].addr,
						       MIN(socketptr->idelimiter[ii].len, DD_BUFLEN - 1));
						dsocketptr->dollar_key[MIN(socketptr->idelimiter[ii].len, DD_BUFLEN - 1)] = '\0';
						break;
					}
				}
				SOCKET_DEBUG(
				if (terminator)
					PRINTF("socrfl: Delimiter found - match_delim: %d\n", match_delim);
				else
				        PRINTF("socrfl: Delimiter not found\n");
				fflush(stdout);
				);
			}
			if (!terminator)
				more_data = TRUE;
		} else if (EINTR == errno && !out_of_time)	/* unrelated timer popped */
		{
			status = 0;
			continue;
		} else
		{
			real_errno = errno;
			break;
		}
		if (bytes_read > MAX_STRLEN)
		{
			iod->dollar.za = 9;
			rts_error(VARLSTCNT(1) ERR_MAXSTRLEN);
		}
		orig_bytes_read = bytes_read;
		if (0 != bytes_read)
		{	/* find n chars read from [buffptr, buffptr + bytes_read) */
			SOCKET_DEBUG(PRINTF("socrfl: Start char scan - c_ptr: 0x%08lx  c_top: 0x%08lx\n",
					    buffptr, (buffptr + status)); fflush(stdout));
			for (c_ptr = buffptr, c_top = buffptr + status;
			     c_ptr < c_top && chars_read < width;
			     c_ptr += mb_len, chars_read++)
			{
				mb_len = 1;	/* In case of CHSET_M */
				if (!((CHSET_M == ichset) ? 1 :
				      (CHSET_UTF8 == ichset) ? UTF8_VALID(c_ptr, c_top, mb_len) :
				      (CHSET_UTF16BE == ichset) ? UTF16BE_VALID(c_ptr, c_top, mb_len) :
				      UTF16LE_VALID(c_ptr, c_top, mb_len)))
				{	/* This char is not valid unicode but this is only an error if entire char is
					   in the buffer. Else we will ignore it and it will be rebuffered further down.
					   First, we need to find its (real) length as xx_VALID set it to one when it
					   was determined to be invalid.
					*/
					mb_len = (CHSET_M == ichset) ? 0 :
						(CHSET_UTF8 == ichset) ? UTF8_MBFOLLOW(c_ptr) :
						(CHSET_UTF16BE == ichset) ? UTF16BE_MBFOLLOW(c_ptr, c_top) :
						UTF16LE_MBFOLLOW(c_ptr, c_top);
					mb_len++;	/* Account for first byte of char */
					if (0 == mb_len || c_ptr + mb_len <= c_top)
					{	/* The entire char is in the buffer.. badchar */
#ifdef UNICODE_SUPPORTED
						if (CHSET_UTF8 == ichset)
						{
							iosocket_readfl_badchar(v, ((unsigned char *)c_ptr - stringpool.free),
										0, c_ptr, c_top);
							UTF8_BADCHAR(0, c_ptr, c_top, 0, NULL);
						} else /* UTF16LE or UTF16BE */
						{
							inv_beg = c_ptr;
							if ((c_ptr += 2) >= c_top)
								c_ptr = c_top;
							iosocket_readfl_badchar(v, ((unsigned char *)inv_beg - stringpool.free),
										(c_ptr - inv_beg), inv_beg, c_top);
							UTF8_BADCHAR(c_ptr - inv_beg, inv_beg, c_top,
								     chset_names[ichset].len, chset_names[ichset].addr);
						}
#endif
					}
				}
				if (c_ptr + mb_len > c_top)	/* Verify entire char is in buffer */
					break;
			}
                        SOCKET_DEBUG(PRINTF("socrfl: End char scan - c_ptr: 0x%08lx  c_top: 0x%08lx\n",
                                            c_ptr, c_top); fflush(stdout));
			if (c_ptr < c_top) /* width size READ completed OR partial last char, push back bytes into input buffer */
			{
				iosocket_unsnr(socketptr, c_ptr, c_top - c_ptr);
				bytes_read -= c_top - c_ptr;	/* We will be re-reading these bytes */
				requeue_done = TRUE;		/* Force single (full) char read next time through */
				SOCKET_DEBUG(PRINTF("socrfl: Requeue of %d bytes done - adjusted bytes_read: %d\n",
						    (c_top - c_ptr), bytes_read); fflush(stdout));
			}
		}
		if (terminator)
		{
			assert(0 != bytes_read);
			bytes_read -= socketptr->idelimiter[match_delim].len;
			c_ptr -= socketptr->idelimiter[match_delim].len;
			UNICODE_ONLY(chars_read -= socketptr->idelimiter[match_delim].char_len);
			NON_UNICODE_ONLY(chars_read = bytes_read);
			SOCKET_DEBUG(PRINTF("socrfl: Terminator found - bytes_read reduced by %d bytes to %d\n",
					    socketptr->idelimiter[match_delim].len, bytes_read); fflush(stdout));
			SOCKET_DEBUG(PRINTF("socrfl: .. c_ptr also reduced to 0x%08lx\n", c_ptr); fflush(stdout));
		}
		/* If we read as much as we needed or if the buffer was totally full (last char or 3 might be part of an
		   incomplete character than can never be completed in this buffer) or if variable length, no delim with
		   chars available and no more data or outofband or have data including a terminator, we are then done.
		*/
		if ((chars_read >= width) ||
		    (MAX_STRLEN <= orig_bytes_read) ||
		    (vari && !has_delimiter && 0 != chars_read && !more_data) ||
		    (OUTOFBANDNOW(outofband)) ||
		    (status > 0 && terminator))
			break;
		if (timed)
		{
			if (msec_timeout > 0)
			{
				sys_get_curr_time(&cur_time);
				cur_time = sub_abs_time(&end_time, &cur_time);
				if (0 > cur_time.at_sec)
				{
					out_of_time = TRUE;
					cancel_timer(timer_id);
					break;
				}
			} else if (!more_data)
				break;
		}
	}
	if (EINTR == real_errno)
		status = 0;	/* don't treat a <CTRL-C> or timeout as an error */
	if (timed)
	{
		if (0 < msec_timeout)
		{
#ifdef UNIX
			FCNTL3(socketptr->sd, F_SETFL, flags, fcntl_res);
			if (fcntl_res < 0)
			{
				iod->dollar.za = 9;
				save_errno = errno;
				errptr = (char *)STRERROR(errno);
				rts_error(VARLSTCNT(7) ERR_SETSOCKOPTERR, 5, LEN_AND_LIT("F_SETFL FOR RESTORING SOCKET OPTIONS"),
					  save_errno, LEN_AND_STR(errptr));
			}
#endif
			if (out_of_time)
				ret = FALSE;
			else
				cancel_timer(timer_id);
		} else if ((chars_read < width) && !(has_delimiter && terminator))
			ret = FALSE;
	}
	if (chars_read > 0)
	{	/* there's something to return */
		v->str.len = c_ptr - stringpool.free;
		v->str.addr = (char *)stringpool.free;
		UNICODE_ONLY(v->str.char_len = chars_read);
		SOCKET_DEBUG(PRINTF("socrfl: String to return bytelen: %d  charlen: %d  iod-width: %d  wrap: %d",
				    v->str.len, chars_read, iod->width, iod->wrap); fflush(stdout));
		SOCKET_DEBUG(PRINTF("socrfl:   x: %d  y: %d\n", iod->dollar.x, iod->dollar.y); fflush(stdout));
		if (((iod->dollar.x += chars_read) >= iod->width) && iod->wrap)
		{
			iod->dollar.y += (iod->dollar.x / iod->width);
			if (0 != iod->length)
				iod->dollar.y %= iod->length;
			iod->dollar.x %= iod->width;
		}
		if (CHSET_M != ichset && CHSET_UTF8 != ichset)
		{
			SOCKET_DEBUG(PRINTF("socrfl: Converting UTF16xx data back to UTF8 for internal use\n"); fflush(stdout));
			v->str.len = gtm_conv(chset_desc[ichset], chset_desc[CHSET_UTF8], &v->str, NULL, NULL);
			v->str.addr = (char *)stringpool.free;
			stringpool.free += v->str.len;
		}

	} else
	{
		v->str.len = 0;
		v->str.addr = dsocketptr->dollar_key;
	}
	if (status >= 0)
	{	/* no real problems */
		iod->dollar.zeof = FALSE;
		iod->dollar.za = 0;
		memcpy(dsocketptr->dollar_device, "0", sizeof("0"));
	} else
	{	/* there's a significant problem */
		SOCKET_DEBUG(PRINTF("socrfl: Error handling triggered - status: %d\n", status); fflush(stdout));
		if (0 == chars_read)
			iod->dollar.x = 0;
		iod->dollar.za = 9;
		len = sizeof(ONE_COMMA) - 1;
		memcpy(dsocketptr->dollar_device, ONE_COMMA, len);
		errptr = (char *)STRERROR(real_errno);
		errlen = strlen(errptr) + 1;
		memcpy(&dsocketptr->dollar_device[len], errptr, errlen);
		if (iod->dollar.zeof || -1 == status || 0 < iod->error_handler.len)
		{
			iod->dollar.zeof = TRUE;
			if (socketptr->ioerror)
				rts_error(VARLSTCNT(6) ERR_IOEOF, 0, ERR_TEXT, 2, errlen, errptr);
		} else
			iod->dollar.zeof = TRUE;
	}
	SOCKET_DEBUG(if (!ret && out_of_time) PRINTF("socrfl: Returning from read due to timeout\n");
		     else PRINTF("socrfl: Returning from read with success indicator set to %d\n", ret));
	SOCKET_DEBUG(fflush(stdout));
	return (ret);
}