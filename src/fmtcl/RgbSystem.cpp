/*****************************************************************************

        RgbSystem.cpp
        Author: Laurent de Soras, 2016

--- Legal stuff ---

This program is free software. It comes without any warranty, to
the extent permitted by applicable law. You can redistribute it
and/or modify it under the terms of the Do What The Fuck You Want
To Public License, Version 2, as published by Sam Hocevar. See
http://sam.zoy.org/wtfpl/COPYING for more details.

*Tab=3***********************************************************************/



#if defined (_MSC_VER)
	#pragma warning (1 : 4130 4223 4705 4706)
	#pragma warning (4 : 4355 4786 4800)
#endif



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include "fmtcl/RgbSystem.h"

#include <cassert>



namespace fmtcl
{



/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



RgbSystem::Vec2::Vec2 (double c0, double c1)
:	Inherited ({ { c0, c1 } })
{
	// Nothing
}



RgbSystem::RgbSystem ()
:	_rgb ()
,	_white ()
,	_init_flag_arr ({ {false, false, false, false } })
,	_preset (fmtcl::PrimariesPreset_UNDEF)
{
	// Nothing
}



void	RgbSystem::set (PrimariesPreset preset)
{
	assert (   (   preset >= 0
	            && preset < PrimariesPreset_NBR_ELT)
	        || (   preset > PrimariesPreset_ISO_RANGE_LAST
	            && preset < PrimariesPreset_NBR_ELT_CUSTOM));

	bool           found_flag = true;
	switch (preset)
	{
	case PrimariesPreset_BT709:
		_rgb [0] = { 0.640 , 0.330  };
		_rgb [1] = { 0.300 , 0.600  };
		_rgb [2] = { 0.150 , 0.060  };
		_white   = { 0.3127, 0.3290 };
		break;
	case PrimariesPreset_FCC:
		_rgb [0] = { 0.670 , 0.330  };
		_rgb [1] = { 0.210 , 0.710  };
		_rgb [2] = { 0.140 , 0.080  };
		_white   = { 0.3100, 0.3160 };
		break;
	case PrimariesPreset_NTSCJ:
		_rgb [0] = { 0.670 , 0.330  };
		_rgb [1] = { 0.210 , 0.710  };
		_rgb [2] = { 0.140 , 0.080  };
		_white   = { 0.2848, 0.2932 };
		break;
	case PrimariesPreset_BT470BG:
		_rgb [0] = { 0.640 , 0.330  };
		_rgb [1] = { 0.290 , 0.600  };
		_rgb [2] = { 0.150 , 0.060  };
		_white   = { 0.3127, 0.3290 };
		break;
	case PrimariesPreset_SMPTE170M:
	case PrimariesPreset_SMPTE240M:
		_rgb [0] = { 0.630 , 0.340  };
		_rgb [1] = { 0.310 , 0.595  };
		_rgb [2] = { 0.155 , 0.070  };
		_white   = { 0.3127, 0.3290 };
		break;
	case PrimariesPreset_GENERIC_FILM:
		_rgb [0] = { 0.681 , 0.319  };
		_rgb [1] = { 0.243 , 0.692  };
		_rgb [2] = { 0.145 , 0.049  };
		_white   = { 0.3100, 0.3160 };
		break;
	case PrimariesPreset_BT2020:
		_rgb [0] = { 0.70792, 0.29203 };
		_rgb [1] = { 0.17024, 0.79652 };
		_rgb [2] = { 0.13137, 0.04588 };
		_white   = { 0.31271, 0.32902 };
		break;
	case PrimariesPreset_SCRGB:
		_rgb [0] = { 0.640  , 0.330   };
		_rgb [1] = { 0.300  , 0.600   };
		_rgb [2] = { 0.150  , 0.060   };
		_white   = { 0.31271, 0.32902 };
		break;
	case PrimariesPreset_ADOBE_RGB_98:
		_rgb [0] = { 0.640  , 0.330   };
		_rgb [1] = { 0.210  , 0.710   };
		_rgb [2] = { 0.150  , 0.060   };
		_white   = { 0.31271, 0.32902 };
		break;
	case PrimariesPreset_ADOBE_RGB_WIDE:
		_rgb [0] = { 0.73469, 0.26531 };
		_rgb [1] = { 0.11416, 0.82621 };
		_rgb [2] = { 0.15664, 0.01770 };
		_white   = { 0.34567, 0.35850 };
		break;
	case PrimariesPreset_APPLE_RGB:
		_rgb [0] = { 0.625  , 0.340   };
		_rgb [1] = { 0.280  , 0.595   };
		_rgb [2] = { 0.155  , 0.070   };
		_white   = { 0.31271, 0.32902 };
		break;
	case PrimariesPreset_ROMM:
		_rgb [0] = { 0.7347 , 0.2653  };
		_rgb [1] = { 0.1596 , 0.8404  };
		_rgb [2] = { 0.0366 , 0.0001  };
		_white   = { 0.34567, 0.35850 };
		break;
	case PrimariesPreset_CIERGB:
		_rgb [0] = { 0.7347 , 0.2653  };
		_rgb [1] = { 0.2738 , 0.7174  };
		_rgb [2] = { 0.1666 , 0.0089  };
		_white   = { 1.0 / 3, 1.0 / 3 };
		break;
	case PrimariesPreset_CIEXYZ:
		_rgb [0] = { 1.0    , 0.0     };
		_rgb [1] = { 0.0    , 1.0     };
		_rgb [2] = { 0.0    , 0.0     };
		_white   = { 1.0 / 3, 1.0 / 3 };
		break;
	case PrimariesPreset_DCIP3:
		_rgb [0] = { 0.680 , 0.320  };
		_rgb [1] = { 0.265 , 0.690  };
		_rgb [2] = { 0.150 , 0.060  };
		_white   = { 0.3127, 0.3290 };
		break;
	case PrimariesPreset_ACES:
		_rgb [0] = { 0.7347 , 0.2653  };
		_rgb [1] = { 0.0    , 1.0     };
		_rgb [2] = { 0.0001 ,-0.0770  };
		_white   = { 0.32168, 0.33767 };
		break;
	case PrimariesPreset_ACESAP1:
		_rgb [0] = { 0.713  , 0.293   };
		_rgb [1] = { 0.165  , 0.830   };
		_rgb [2] = { 0.128  , 0.044   };
		_white   = { 0.32168, 0.33767 };
		break;
	case PrimariesPreset_SGAMUT:
		_rgb [0] = { 0.730 , 0.280  };
		_rgb [1] = { 0.140 , 0.855  };
		_rgb [2] = { 0.100 ,-0.050  };
		_white   = { 0.3127, 0.3290 };
		break;
	case PrimariesPreset_SGAMUT3CINE:
		_rgb [0] = { 0.766 , 0.275  };
		_rgb [1] = { 0.225 , 0.800  };
		_rgb [2] = { 0.089 ,-0.087  };
		_white   = { 0.3127, 0.3290 };
		break;
	case PrimariesPreset_ALEXA:
		_rgb [0] = { 0.6840, 0.3130 };
		_rgb [1] = { 0.2210, 0.8480 };
		_rgb [2] = { 0.0861,-0.1020 };
		_white   = { 0.3127, 0.3290 };
		break;
	case PrimariesPreset_VGAMUT:
		_rgb [0] = { 0.730 , 0.280  };
		_rgb [1] = { 0.165 , 0.840  };
		_rgb [2] = { 0.100 ,-0.030  };
		_white   = { 0.3127, 0.3290 };
		break;
	default:
		assert (false);
		found_flag = false;
		break;
	}

	if (found_flag)
	{
		for (bool &init_flag : _init_flag_arr)
		{
			init_flag = true;
		}
	}
}



bool	RgbSystem::is_ready () const
{
	for (bool init_flag : _init_flag_arr)
	{
		if (! init_flag)
		{
			return false;
		}
	}
	return (true);
}



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



}  // namespace fmtcl



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
