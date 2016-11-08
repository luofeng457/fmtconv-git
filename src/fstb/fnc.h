/*****************************************************************************

        fnc.h
        Author: Laurent de Soras, 2010

--- Legal stuff ---

This program is free software. It comes without any warranty, to
the extent permitted by applicable law. You can redistribute it
and/or modify it under the terms of the Do What The Fuck You Want
To Public License, Version 2, as published by Sam Hocevar. See
http://sam.zoy.org/wtfpl/COPYING for more details.

*Tab=3***********************************************************************/



#pragma once
#if ! defined (fstb_fnc_HEADER_INCLUDED)
#define	fstb_fnc_HEADER_INCLUDED

#if defined (_MSC_VER)
	#pragma warning (4 : 4250) // "Inherits via dominance."
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include <string>

#include <cstdint>



namespace fstb
{



template <class T>
inline T       limit (T x, T mi, T ma);
template <class T>
inline bool    is_pow_2 (T x);
inline double  round (double x);
inline int     round_int (double x);
inline int     floor_int (double x);
inline int     ceil_int (double x);
template <class T>
inline int     conv_int_fast (T x);
template <class T>
inline bool    is_null (T val, T eps = T (1e-9));
template <class T>
inline bool    is_eq (T v1, T v2, T eps = T (1e-9));
template <class T>
inline bool    is_eq_rel (T v1, T v2, T tol = T (1e-6));
inline int     get_prev_pow_2 (uint32_t x);
inline double  sinc (double x);
template <class T, int S>
inline T       sshift_l (T x);
template <class T, int S>
inline T       sshift_r (T x);

void           conv_to_lower_case (std::string &str);

int            snprintf4all (char *out_0, size_t size, const char *format_0, ...);



}	// namespace fstb



#include "fstb/fnc.hpp"



#endif	// fstb_fnc_HEADER_INCLUDED



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
