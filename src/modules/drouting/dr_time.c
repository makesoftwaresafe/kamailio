/*
 * Copyright (C) 2005-2009 Voice Sistem SRL
 *
 * This file is part of Kamailio, a free SIP server.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Kamailio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Kamailio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "../../core/mem/shm_mem.h"
#include "dr_time.h"


/************************ imported from "utils.h"  ***************************/

static inline int strz2int(char *_bp)
{
	int _v;
	char *_p;
	if(!_bp)
		return 0;
	_v = 0;
	_p = _bp;
	while(*_p && *_p >= '0' && *_p <= '9') {
		_v += *_p - '0';
		_p++;
	}
	return _v;
}


/************************ imported from "ac_tm.c"  ***************************/

/* #define USE_YWEEK_U		// Sunday system
 * #define USE_YWEEK_V		// ISO 8601
 */
#ifndef USE_YWEEK_U
#ifndef USE_YWEEK_V
#ifndef USE_YWEEK_W
#define USE_YWEEK_W /* Monday system */
#endif
#endif
#endif

#ifdef USE_YWEEK_U
#define SUN_WEEK(t) (int)(((t)->tm_yday + 7 - ((t)->tm_wday)) / 7)
#else
#define MON_WEEK(t) \
	(int)(((t)->tm_yday + 7 - ((t)->tm_wday ? (t)->tm_wday - 1 : 6)) / 7)
#endif

#define dr_ac_get_wday_yr(t) (int)((t)->tm_yday / 7)
#define dr_ac_get_wday_mr(t) (int)(((t)->tm_mday - 1) / 7)

dr_ac_tm_p dr_ac_tm_new(void)
{
	dr_ac_tm_p _atp = NULL;
	_atp = (dr_ac_tm_p)shm_malloc(sizeof(dr_ac_tm_t));
	if(!_atp) {
		SHM_MEM_ERROR;
		return NULL;
	}
	memset(_atp, 0, sizeof(dr_ac_tm_t));

	return _atp;
}

int dr_ac_tm_fill(dr_ac_tm_p _atp, struct tm *_tm)
{
	if(!_atp || !_tm)
		return -1;
	_atp->t.tm_sec = _tm->tm_sec;	  /* seconds */
	_atp->t.tm_min = _tm->tm_min;	  /* minutes */
	_atp->t.tm_hour = _tm->tm_hour;	  /* hours */
	_atp->t.tm_mday = _tm->tm_mday;	  /* day of the month */
	_atp->t.tm_mon = _tm->tm_mon;	  /* month */
	_atp->t.tm_year = _tm->tm_year;	  /* year */
	_atp->t.tm_wday = _tm->tm_wday;	  /* day of the week */
	_atp->t.tm_yday = _tm->tm_yday;	  /* day in the year */
	_atp->t.tm_isdst = _tm->tm_isdst; /* daylight saving time */

	_atp->mweek = dr_ac_get_mweek(_tm);
	_atp->yweek = dr_ac_get_yweek(_tm);
	_atp->ywday = dr_ac_get_wday_yr(_tm);
	_atp->mwday = dr_ac_get_wday_mr(_tm);
	return 0;
}

int dr_ac_tm_set_time(dr_ac_tm_p _atp, time_t _t)
{
	struct tm _tm;
	if(!_atp)
		return -1;
	_atp->time = _t;
	localtime_r(&_t, &_tm);
	return dr_ac_tm_fill(_atp, &_tm);
}

int dr_ac_get_mweek(struct tm *_tm)
{
	if(!_tm)
		return -1;
#ifdef USE_YWEEK_U
	return ((_tm->tm_mday - 1) / 7
			+ (7 - _tm->tm_wday + (_tm->tm_mday - 1) % 7) / 7);
#else
	return ((_tm->tm_mday - 1) / 7
			+ (7 - (6 + _tm->tm_wday) % 7 + (_tm->tm_mday - 1) % 7) / 7);
#endif
}

int dr_ac_get_yweek(struct tm *_tm)
{
	int week = -1;
#ifdef USE_YWEEK_V
	int days;
#endif

	if(!_tm)
		return -1;

#ifdef USE_YWEEK_U
	week = SUN_WEEK(_tm);
#else
	week = MON_WEEK(_tm);
#endif

#ifdef USE_YWEEK_V
	days = ((_tm->tm_yday + 7 - (_tm->tm_wday ? _tm->tm_wday - 1 : 6)) % 7);

	if(days >= 4)
		week++;
	else if(week == 0)
		week = 53;
#endif
	return week;
}

int dr_ac_get_wkst(void)
{
#ifdef USE_YWEEK_U
	return 0;
#else
	return 1;
#endif
}

int dr_ac_tm_reset(dr_ac_tm_p _atp)
{
	if(!_atp)
		return -1;
	memset(_atp, 0, sizeof(dr_ac_tm_t));
	return 0;
}

int dr_ac_tm_free(dr_ac_tm_p _atp)
{
	if(!_atp)
		return -1;
	if(_atp->mv)
		shm_free(_atp->mv);
	shm_free(_atp);
	return 0;
}

dr_ac_maxval_p dr_ac_get_maxval(dr_ac_tm_p _atp, int mode)
{
	struct tm _tm;
	int _v;
	dr_ac_maxval_p _amp = NULL;
	static dr_ac_maxval_t _amv;

	if(!_atp)
		return NULL;
	if(mode == 1) {
		_amp = (dr_ac_maxval_p)shm_malloc(sizeof(dr_ac_maxval_t));
		if(!_amp) {
			SHM_MEM_ERROR;
			return NULL;
		}
	} else {
		_amp = &_amv;
	}
	memset(_amp, 0, sizeof(dr_ac_maxval_t));

	/* the number of the days in the year */
	_amp->yday = 365 + is_leap_year(_atp->t.tm_year + 1900);

	/* the number of the days in the month */
	switch(_atp->t.tm_mon) {
		case 1:
			if(_amp->yday == 366)
				_amp->mday = 29;
			else
				_amp->mday = 28;
			break;
		case 3:
		case 5:
		case 8:
		case 10:
			_amp->mday = 30;
			break;
		default:
			_amp->mday = 31;
	}

	/* maximum occurrences of a week day in the year */
	memset(&_tm, 0, sizeof(struct tm));
	_tm.tm_year = _atp->t.tm_year;
	_tm.tm_mon = 11;
	_tm.tm_mday = 31;
	mktime(&_tm);
	_v = 0;
	if(_atp->t.tm_wday > _tm.tm_wday)
		_v = _atp->t.tm_wday - _tm.tm_wday + 1;
	else
		_v = _tm.tm_wday - _atp->t.tm_wday;
	_amp->ywday = (int)((_tm.tm_yday - _v) / 7) + 1;

	/* maximum number of weeks in the year */
	_amp->yweek = dr_ac_get_yweek(&_tm) + 1;

	/* maximum number of the week day in the month */
	_amp->mwday =
			(int)((_amp->mday - 1 - (_amp->mday - _atp->t.tm_mday) % 7) / 7)
			+ 1;

	/* maximum number of weeks in the month */
	_v = (_atp->t.tm_wday + (_amp->mday - _atp->t.tm_mday) % 7) % 7;
#ifdef USE_YWEEK_U
	_amp->mweek =
			(int)((_amp->mday - 1) / 7 + (7 - _v + (_amp->mday - 1) % 7) / 7)
			+ 1;
#else
	_amp->mweek = (int)((_amp->mday - 1) / 7
						  + (7 - (6 + _v) % 7 + (_amp->mday - 1) % 7) / 7)
				  + 1;
#endif

	if(mode == 1) {
		if(_atp->mv != NULL) {
			shm_free(_atp->mv);
		}

		_atp->mv = _amp;
	}
	return _amp;
}


/************************ imported from "tmrec.c"  ***************************/

#define _D(c) ((c) - '0')

dr_tr_byxxx_p dr_tr_byxxx_new(void)
{
	dr_tr_byxxx_p _bxp = NULL;
	_bxp = (dr_tr_byxxx_p)shm_malloc(sizeof(dr_tr_byxxx_t));
	if(!_bxp) {
		SHM_MEM_ERROR;
		return NULL;
	}
	memset(_bxp, 0, sizeof(dr_tr_byxxx_t));
	return _bxp;
}

int dr_tr_byxxx_init(dr_tr_byxxx_p _bxp, int _nr)
{
	if(!_bxp)
		return -1;
	_bxp->nr = _nr;
	_bxp->xxx = (int *)shm_malloc(_nr * sizeof(int));
	if(!_bxp->xxx) {
		SHM_MEM_ERROR;
		return -1;
	}
	_bxp->req = (int *)shm_malloc(_nr * sizeof(int));
	if(!_bxp->req) {
		SHM_MEM_ERROR;
		shm_free(_bxp->xxx);
		return -1;
	}

	memset(_bxp->xxx, 0, _nr * sizeof(int));
	memset(_bxp->req, 0, _nr * sizeof(int));

	return 0;
}


int dr_tr_byxxx_free(dr_tr_byxxx_p _bxp)
{
	if(!_bxp)
		return -1;
	if(_bxp->xxx)
		shm_free(_bxp->xxx);
	if(_bxp->req)
		shm_free(_bxp->req);
	shm_free(_bxp);
	return 0;
}

dr_tmrec_p dr_tmrec_new(void)
{
	dr_tmrec_p _trp = NULL;
	_trp = (dr_tmrec_p)shm_malloc(sizeof(dr_tmrec_t));
	if(!_trp) {
		SHM_MEM_ERROR;
		return NULL;
	}
	memset(_trp, 0, sizeof(dr_tmrec_t));
	localtime_r(&_trp->dtstart, &(_trp->ts));
	return _trp;
}

int dr_tmrec_free(dr_tmrec_p _trp)
{
	if(!_trp)
		return -1;

	dr_tr_byxxx_free(_trp->byday);
	dr_tr_byxxx_free(_trp->bymday);
	dr_tr_byxxx_free(_trp->byyday);
	dr_tr_byxxx_free(_trp->bymonth);
	dr_tr_byxxx_free(_trp->byweekno);

	shm_free(_trp);
	return 0;
}

int dr_tr_parse_dtstart(dr_tmrec_p _trp, char *_in)
{
	if(!_trp || !_in)
		return -1;
	_trp->dtstart = dr_ic_parse_datetime(_in, &(_trp->ts));
	return (_trp->dtstart == 0) ? -1 : 0;
}

int dr_tr_parse_dtend(dr_tmrec_p _trp, char *_in)
{
	struct tm _tm;
	if(!_trp || !_in)
		return -1;
	_trp->dtend = dr_ic_parse_datetime(_in, &_tm);
	return (_trp->dtend == 0) ? -1 : 0;
}

int dr_tr_parse_duration(dr_tmrec_p _trp, char *_in)
{
	if(!_trp || !_in)
		return -1;
	_trp->duration = dr_ic_parse_duration(_in);
	return 0;
}

int dr_tr_parse_until(dr_tmrec_p _trp, char *_in)
{
	struct tm _tm;
	if(!_trp || !_in)
		return -1;
	_trp->until = dr_ic_parse_datetime(_in, &_tm);
	return 0;
}

int dr_tr_parse_freq(dr_tmrec_p _trp, char *_in)
{
	if(!_trp || !_in)
		return -1;
	if(strlen(_in) < 5) {
		_trp->freq = FREQ_NOFREQ;
		return 0;
	}
	if(!strcasecmp(_in, "daily")) {
		_trp->freq = FREQ_DAILY;
		return 0;
	}
	if(!strcasecmp(_in, "weekly")) {
		_trp->freq = FREQ_WEEKLY;
		return 0;
	}
	if(!strcasecmp(_in, "monthly")) {
		_trp->freq = FREQ_MONTHLY;
		return 0;
	}
	if(!strcasecmp(_in, "yearly")) {
		_trp->freq = FREQ_YEARLY;
		return 0;
	}

	_trp->freq = FREQ_NOFREQ;
	return 0;
}

int dr_tr_parse_interval(dr_tmrec_p _trp, char *_in)
{
	if(!_trp || !_in)
		return -1;
	_trp->interval = strz2int(_in);
	return 0;
}

int dr_tr_parse_byday(dr_tmrec_p _trp, char *_in)
{
	if(!_trp || !_in)
		return -1;
	_trp->byday = dr_ic_parse_byday(_in);
	return 0;
}

int dr_tr_parse_bymday(dr_tmrec_p _trp, char *_in)
{
	if(!_trp || !_in)
		return -1;
	_trp->bymday = dr_ic_parse_byxxx(_in);
	return 0;
}

int dr_tr_parse_byyday(dr_tmrec_p _trp, char *_in)
{
	if(!_trp || !_in)
		return -1;
	_trp->byyday = dr_ic_parse_byxxx(_in);
	return 0;
}

int dr_tr_parse_bymonth(dr_tmrec_p _trp, char *_in)
{
	if(!_trp || !_in)
		return -1;
	_trp->bymonth = dr_ic_parse_byxxx(_in);
	return 0;
}

int dr_tr_parse_byweekno(dr_tmrec_p _trp, char *_in)
{
	if(!_trp || !_in)
		return -1;
	_trp->byweekno = dr_ic_parse_byxxx(_in);
	return 0;
}

int dr_tr_parse_wkst(dr_tmrec_p _trp, char *_in)
{
	if(!_trp || !_in)
		return -1;
	_trp->wkst = dr_ic_parse_wkst(_in);
	return 0;
}


time_t dr_ic_parse_datetime(char *_in, struct tm *_tm)
{
	if(!_in || !_tm || strlen(_in) != 15)
		return 0;

	memset(_tm, 0, sizeof(struct tm));
	_tm->tm_year = _D(_in[0]) * 1000 + _D(_in[1]) * 100 + _D(_in[2]) * 10
				   + _D(_in[3]) - 1900;
	_tm->tm_mon = _D(_in[4]) * 10 + _D(_in[5]) - 1;
	_tm->tm_mday = _D(_in[6]) * 10 + _D(_in[7]);
	_tm->tm_hour = _D(_in[9]) * 10 + _D(_in[10]);
	_tm->tm_min = _D(_in[11]) * 10 + _D(_in[12]);
	_tm->tm_sec = _D(_in[13]) * 10 + _D(_in[14]);
	_tm->tm_isdst = -1 /*daylight*/;
	return mktime(_tm);
}

time_t dr_ic_parse_duration(char *_in)
{
	time_t _t, _ft;
	char *_p;
	int _fl;

	if(!_in || strlen(_in) < 2)
		return 0;

	if(*_in == 'P' || *_in == 'p') {
		_p = _in + 1;
		_fl = 1;
	} else {
		_p = _in;
		_fl = 0;
	}

	_t = _ft = 0;

	while(*_p) {
		switch(*_p) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				_t = _t * 10 + *_p - '0';
				break;

			case 'w':
			case 'W':
				if(!_fl) {
					LM_ERR("week duration not allowed"
						   " here (%d) [%s]\n",
							(int)(_p - _in), _in);
					return 0;
				}
				_ft += _t * 7 * 24 * 3600;
				_t = 0;
				break;
			case 'd':
			case 'D':
				if(!_fl) {
					LM_ERR("day duration not allowed"
						   " here (%d) [%s]\n",
							(int)(_p - _in), _in);
					return 0;
				}
				_ft += _t * 24 * 3600;
				_t = 0;
				break;
			case 'h':
			case 'H':
				if(_fl) {
					LM_ERR("hour duration not allowed"
						   " here (%d) [%s]\n",
							(int)(_p - _in), _in);
					return 0;
				}
				_ft += _t * 3600;
				_t = 0;
				break;
			case 'm':
			case 'M':
				if(_fl) {
					LM_ERR("minute duration not allowed"
						   " here (%d) [%s]\n",
							(int)(_p - _in), _in);
					return 0;
				}
				_ft += _t * 60;
				_t = 0;
				break;
			case 's':
			case 'S':
				if(_fl) {
					LM_ERR("second duration not allowed"
						   " here (%d) [%s]\n",
							(int)(_p - _in), _in);
					return 0;
				}
				_ft += _t;
				_t = 0;
				break;
			case 't':
			case 'T':
				if(!_fl) {
					LM_ERR("'T' not allowed"
						   " here (%d) [%s]\n",
							(int)(_p - _in), _in);
					return 0;
				}
				_fl = 0;
				break;
			default:
				LM_ERR("bad character here (%d) [%s]\n", (int)(_p - _in), _in);
				return 0;
		}
		_p++;
	}

	return _ft;
}

dr_tr_byxxx_p dr_ic_parse_byday(char *_in)
{
	dr_tr_byxxx_p _bxp = NULL;
	int _nr, _s, _v;
	char *_p;

	if(!_in)
		return NULL;
	_bxp = dr_tr_byxxx_new();
	if(!_bxp)
		return NULL;
	_p = _in;
	_nr = 1;
	while(*_p) {
		if(*_p == ',')
			_nr++;
		_p++;
	}
	if(dr_tr_byxxx_init(_bxp, _nr) < 0) {
		dr_tr_byxxx_free(_bxp);
		return NULL;
	}
	_p = _in;
	_nr = _v = 0;
	_s = 1;
	while(*_p && _nr < _bxp->nr) {
		switch(*_p) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				_v = _v * 10 + *_p - '0';
				break;

			case 's':
			case 'S':
				_p++;
				switch(*_p) {
					case 'a':
					case 'A':
						_bxp->xxx[_nr] = WDAY_SA;
						_bxp->req[_nr] = _s * _v;
						break;
					case 'u':
					case 'U':
						_bxp->xxx[_nr] = WDAY_SU;
						_bxp->req[_nr] = _s * _v;
						break;
					default:
						goto error;
				}
				_s = 1;
				_v = 0;
				break;
			case 'm':
			case 'M':
				_p++;
				if(*_p != 'o' && *_p != 'O')
					goto error;
				_bxp->xxx[_nr] = WDAY_MO;
				_bxp->req[_nr] = _s * _v;
				_s = 1;
				_v = 0;
				break;
			case 't':
			case 'T':
				_p++;
				switch(*_p) {
					case 'h':
					case 'H':
						_bxp->xxx[_nr] = WDAY_TH;
						_bxp->req[_nr] = _s * _v;
						break;
					case 'u':
					case 'U':
						_bxp->xxx[_nr] = WDAY_TU;
						_bxp->req[_nr] = _s * _v;
						break;
					default:
						goto error;
				}
				_s = 1;
				_v = 0;
				break;
			case 'w':
			case 'W':
				_p++;
				if(*_p != 'e' && *_p != 'E')
					goto error;
				_bxp->xxx[_nr] = WDAY_WE;
				_bxp->req[_nr] = _s * _v;
				_s = 1;
				_v = 0;
				break;
			case 'f':
			case 'F':
				_p++;
				if(*_p != 'r' && *_p != 'R')
					goto error;
				_bxp->xxx[_nr] = WDAY_FR;
				_bxp->req[_nr] = _s * _v;
				_s = 1;
				_v = 0;
				break;
			case '-':
				_s = -1;
				break;
			case '+':
			case ' ':
			case '\t':
				break;
			case ',':
				_nr++;
				break;
			default:
				goto error;
		}
		_p++;
	}

	return _bxp;

error:
	dr_tr_byxxx_free(_bxp);
	return NULL;
}

dr_tr_byxxx_p dr_ic_parse_byxxx(char *_in)
{
	dr_tr_byxxx_p _bxp = NULL;
	int _nr, _s, _v;
	char *_p;

	if(!_in)
		return NULL;
	_bxp = dr_tr_byxxx_new();
	if(!_bxp)
		return NULL;
	_p = _in;
	_nr = 1;
	while(*_p) {
		if(*_p == ',')
			_nr++;
		_p++;
	}
	if(dr_tr_byxxx_init(_bxp, _nr) < 0) {
		dr_tr_byxxx_free(_bxp);
		return NULL;
	}
	_p = _in;
	_nr = _v = 0;
	_s = 1;
	while(*_p && _nr < _bxp->nr) {
		switch(*_p) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				_v = _v * 10 + *_p - '0';
				break;

			case '-':
				_s = -1;
				break;
			case '+':
			case ' ':
			case '\t':
				break;
			case ',':
				_bxp->xxx[_nr] = _v;
				_bxp->req[_nr] = _s;
				_s = 1;
				_v = 0;
				_nr++;
				break;
			default:
				goto error;
		}
		_p++;
	}
	if(_nr < _bxp->nr) {
		_bxp->xxx[_nr] = _v;
		_bxp->req[_nr] = _s;
	}
	return _bxp;

error:
	dr_tr_byxxx_free(_bxp);
	return NULL;
}

int dr_ic_parse_wkst(char *_in)
{
	if(!_in || strlen(_in) != 2)
		goto error;

	switch(_in[0]) {
		case 's':
		case 'S':
			switch(_in[1]) {
				case 'a':
				case 'A':
					return WDAY_SA;
				case 'u':
				case 'U':
					return WDAY_SU;
				default:
					goto error;
			}
		case 'm':
		case 'M':
			if(_in[1] != 'o' && _in[1] != 'O')
				goto error;
			return WDAY_MO;
		case 't':
		case 'T':
			switch(_in[1]) {
				case 'h':
				case 'H':
					return WDAY_TH;
				case 'u':
				case 'U':
					return WDAY_TU;
				default:
					goto error;
			}
		case 'w':
		case 'W':
			if(_in[1] != 'e' && _in[1] != 'E')
				goto error;
			return WDAY_WE;
		case 'f':
		case 'F':
			if(_in[1] != 'r' && _in[1] != 'R')
				goto error;
			return WDAY_FR;
			break;
		default:
			goto error;
	}

error:
#ifdef USE_YWEEK_U
	return WDAY_SU;
#else
	return WDAY_MO;
#endif
}


/*********************** imported from "checktr.c"  **************************/

#define REC_ERR -1
#define REC_MATCH 0
#define REC_NOMATCH 1

#define _IS_SET(x) (((x) > 0) ? 1 : 0)

/*** local headers ***/
int dr_get_min_interval(dr_tmrec_p);
int dr_check_min_unit(dr_tmrec_p, dr_ac_tm_p, dr_tr_res_p);
int dr_check_freq_interval(dr_tmrec_p _trp, dr_ac_tm_p _atp);
int dr_check_byxxx(dr_tmrec_p, dr_ac_tm_p);

/**
 *
 * return 0/REC_MATCH - the time falls in
 *       -1/REC_ERR - error
 *        1/REC_NOMATCH - the time falls out
 */
int dr_check_tmrec(dr_tmrec_p _trp, dr_ac_tm_p _atp, dr_tr_res_p _tsw)
{
	if(!_trp || !_atp)
		return REC_ERR;

	/* it is before start date */
	if(_atp->time < _trp->dtstart)
		return REC_NOMATCH;

	/* no duration or end -> for ever */
	if(!_IS_SET(_trp->duration) && !_IS_SET(_trp->dtend))
		return REC_MATCH;

	/* compute the duration of the recurrence interval */
	if(!_IS_SET(_trp->duration))
		_trp->duration = _trp->dtend - _trp->dtstart;

	if(_atp->time <= _trp->dtstart + _trp->duration) {
		if(_tsw) {
			if(_tsw->flag & TSW_RSET) {
				if(_tsw->rest > _trp->dtstart + _trp->duration - _atp->time)
					_tsw->rest = _trp->dtstart + _trp->duration - _atp->time;
			} else {
				_tsw->flag |= TSW_RSET;
				_tsw->rest = _trp->dtstart + _trp->duration - _atp->time;
			}
		}
		return REC_MATCH;
	}

	/* after the bound of recurrence */
	if(_IS_SET(_trp->until) && _atp->time >= _trp->until + _trp->duration)
		return REC_NOMATCH;

	/* check if the instance of recurrence matches the 'interval' */
	if(dr_check_freq_interval(_trp, _atp) != REC_MATCH)
		return REC_NOMATCH;

	if(dr_check_min_unit(_trp, _atp, _tsw) != REC_MATCH)
		return REC_NOMATCH;

	if(dr_check_byxxx(_trp, _atp) != REC_MATCH)
		return REC_NOMATCH;

	return REC_MATCH;
}


int dr_check_freq_interval(dr_tmrec_p _trp, dr_ac_tm_p _atp)
{
	uint64_t _t0, _t1;
	struct tm _tm;
	if(!_trp || !_atp)
		return REC_ERR;

	if(!_IS_SET(_trp->freq))
		return REC_NOMATCH;

	if(!_IS_SET(_trp->interval) || _trp->interval == 1)
		return REC_MATCH;

	switch(_trp->freq) {
		case FREQ_DAILY:
		case FREQ_WEEKLY:
			memset(&_tm, 0, sizeof(struct tm));
			_tm.tm_year = _trp->ts.tm_year;
			_tm.tm_mon = _trp->ts.tm_mon;
			_tm.tm_mday = _trp->ts.tm_mday;
			_t0 = (uint64_t)mktime(&_tm);
			memset(&_tm, 0, sizeof(struct tm));
			_tm.tm_year = _atp->t.tm_year;
			_tm.tm_mon = _atp->t.tm_mon;
			_tm.tm_mday = _atp->t.tm_mday;
			_t1 = (uint64_t)mktime(&_tm);
			if(_trp->freq == FREQ_DAILY)
				return (((_t1 - _t0) / (24 * 3600)) % _trp->interval == 0)
							   ? REC_MATCH
							   : REC_NOMATCH;
#ifdef USE_YWEEK_U
			_t0 -= _trp->ts.tm_wday * 24 * 3600;
			_t1 -= _atp->t.tm_wday * 24 * 3600;
#else
			_t0 -= ((_trp->ts.tm_wday + 6) % 7) * 24 * 3600;
			_t1 -= ((_atp->t.tm_wday + 6) % 7) * 24 * 3600;
#endif
			return (((_t1 - _t0) / (7 * 24 * 3600)) % _trp->interval == 0)
						   ? REC_MATCH
						   : REC_NOMATCH;
		case FREQ_MONTHLY:
			_t0 = 12ULL * (_atp->t.tm_year - _trp->ts.tm_year) + _atp->t.tm_mon
				  - _trp->ts.tm_mon;
			return (_t0 % _trp->interval == 0) ? REC_MATCH : REC_NOMATCH;
		case FREQ_YEARLY:
			return ((_atp->t.tm_year - _trp->ts.tm_year) % _trp->interval == 0)
						   ? REC_MATCH
						   : REC_NOMATCH;
	}

	return REC_NOMATCH;
}

int dr_get_min_interval(dr_tmrec_p _trp)
{
	if(!_trp)
		return FREQ_NOFREQ;

	if(_trp->freq == FREQ_DAILY || _trp->byday || _trp->bymday || _trp->byyday)
		return FREQ_DAILY;
	if(_trp->freq == FREQ_WEEKLY || _trp->byweekno)
		return FREQ_WEEKLY;
	if(_trp->freq == FREQ_MONTHLY || _trp->bymonth)
		return FREQ_MONTHLY;
	if(_trp->freq == FREQ_YEARLY)
		return FREQ_YEARLY;

	return FREQ_NOFREQ;
}

int dr_check_min_unit(dr_tmrec_p _trp, dr_ac_tm_p _atp, dr_tr_res_p _tsw)
{
	int _v0, _v1;
	if(!_trp || !_atp)
		return REC_ERR;
	switch(dr_get_min_interval(_trp)) {
		case FREQ_DAILY:
			break;
		case FREQ_WEEKLY:
			if(_trp->ts.tm_wday != _atp->t.tm_wday)
				return REC_NOMATCH;
			break;
		case FREQ_MONTHLY:
			if(_trp->ts.tm_mday != _atp->t.tm_mday)
				return REC_NOMATCH;
			break;
		case FREQ_YEARLY:
			if(_trp->ts.tm_mon != _atp->t.tm_mon
					|| _trp->ts.tm_mday != _atp->t.tm_mday)
				return REC_NOMATCH;
			break;
		default:
			return REC_NOMATCH;
	}
	_v0 = _trp->ts.tm_hour * 3600 + _trp->ts.tm_min * 60 + _trp->ts.tm_sec;
	_v1 = _atp->t.tm_hour * 3600 + _atp->t.tm_min * 60 + _atp->t.tm_sec;
	if(_v1 >= _v0 && _v1 < _v0 + _trp->duration) {
		if(_tsw) {
			if(_tsw->flag & TSW_RSET) {
				if(_tsw->rest > _v0 + _trp->duration - _v1)
					_tsw->rest = _v0 + _trp->duration - _v1;
			} else {
				_tsw->flag |= TSW_RSET;
				_tsw->rest = _v0 + _trp->duration - _v1;
			}
		}
		return REC_MATCH;
	}

	return REC_NOMATCH;
}

int dr_check_byxxx(dr_tmrec_p _trp, dr_ac_tm_p _atp)
{
	int i;
	dr_ac_maxval_p _amp = NULL;
	if(!_trp || !_atp)
		return REC_ERR;
	if(!_trp->byday && !_trp->bymday && !_trp->byyday && !_trp->bymonth
			&& !_trp->byweekno)
		return REC_MATCH;

	_amp = dr_ac_get_maxval(_atp, 0);
	if(!_amp)
		return REC_NOMATCH;

	if(_trp->bymonth) {
		for(i = 0; i < _trp->bymonth->nr; i++) {
			if(_atp->t.tm_mon
					== (_trp->bymonth->xxx[i] * _trp->bymonth->req[i] + 12)
							   % 12)
				break;
		}
		if(i >= _trp->bymonth->nr)
			return REC_NOMATCH;
	}
	if(_trp->freq == FREQ_YEARLY && _trp->byweekno) {
		for(i = 0; i < _trp->byweekno->nr; i++) {
			if(_atp->yweek
					== (_trp->byweekno->xxx[i] * _trp->byweekno->req[i]
							   + _amp->yweek)
							   % _amp->yweek)
				break;
		}
		if(i >= _trp->byweekno->nr)
			return REC_NOMATCH;
	}
	if(_trp->byyday) {
		for(i = 0; i < _trp->byyday->nr; i++) {
			if(_atp->t.tm_yday
					== (_trp->byyday->xxx[i] * _trp->byyday->req[i]
							   + _amp->yday)
							   % _amp->yday)
				break;
		}
		if(i >= _trp->byyday->nr)
			return REC_NOMATCH;
	}
	if(_trp->bymday) {
		for(i = 0; i < _trp->bymday->nr; i++) {
#ifdef EXTRA_DEBUG
			LM_DBG("%d == %d\n", _atp->t.tm_mday,
					(_trp->bymday->xxx[i] * _trp->bymday->req[i] + _amp->mday)
									% _amp->mday
							+ ((_trp->bymday->req[i] < 0) ? 1 : 0));
#endif
			if(_atp->t.tm_mday
									== (_trp->bymday->xxx[i]
													   * _trp->bymday->req[i]
											   + _amp->mday)
													   % _amp->mday
											   + (_trp->bymday->req[i] < 0)
							? 1
							: 0)
				break;
		}
		if(i >= _trp->bymday->nr)
			return REC_NOMATCH;
	}
	if(_trp->byday) {
		for(i = 0; i < _trp->byday->nr; i++) {
			if(_trp->freq == FREQ_YEARLY) {
#ifdef EXTRA_DEBUG
				LM_DBG("%d==%d && %d==%d\n", _atp->t.tm_wday,
						_trp->byday->xxx[i], _atp->ywday + 1,
						(_trp->byday->req[i] + _amp->ywday) % _amp->ywday);
#endif
				if(_atp->t.tm_wday == _trp->byday->xxx[i]
						&& _atp->ywday + 1
								   == (_trp->byday->req[i] + _amp->ywday)
											  % _amp->ywday)
					break;
			} else {
				if(_trp->freq == FREQ_MONTHLY) {
#ifdef EXTRA_DEBUG
					LM_DBG("%d==%d && %d==%d\n", _atp->t.tm_wday,
							_trp->byday->xxx[i], _atp->mwday + 1,
							(_trp->byday->req[i] + _amp->mwday) % _amp->mwday);
#endif
					if(_atp->t.tm_wday == _trp->byday->xxx[i]
							&& _atp->mwday + 1
									   == (_trp->byday->req[i] + _amp->mwday)
												  % _amp->mwday)
						break;
				} else {
					if(_atp->t.tm_wday == _trp->byday->xxx[i])
						break;
				}
			}
		}
		if(i >= _trp->byday->nr)
			return REC_NOMATCH;
	}

	return REC_MATCH;
}
