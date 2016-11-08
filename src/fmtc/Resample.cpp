/*****************************************************************************

        Resample.cpp
        Author: Laurent de Soras, 2012

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

#include "fmtc/Resample.h"
#include "fmtc/SplFmtUtl.h"
#include "vsutl/CpuOpt.h"
#include "vsutl/fnc.h"
#include "vsutl/FrameRefSPtr.h"
#include "vsutl/PlaneProcMode.h"

#include <algorithm>

#include <cassert>
#include <cctype>
#include <cstring>

#if VAPOURSYNTH_API_VERSION < 0x30002
	#define FMTC_RESAMPLE_OLD_FIELDBASED_BEHAVIOUR
#endif



namespace fmtc
{



/*\\\ PUBLIC \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



Resample::Resample (const ::VSMap &in, ::VSMap &out, void *user_data_ptr, ::VSCore &core, const ::VSAPI &vsapi)
:	vsutl::FilterBase (vsapi, "resample", ::fmParallel, 0)
,	_clip_src_sptr (vsapi.propGetNode (&in, "clip", 0, 0), vsapi)
,	_vi_in (*_vsapi.getVideoInfo (_clip_src_sptr.get ()))
,	_vi_out (_vi_in)
,	_plane_arr ()
,	_src_width (0)
,	_src_height (0)
,	_norm_val_h (0)
,	_norm_val_v (0)
,	_interlaced_src (static_cast <InterlacingParam> (
		get_arg_int (in, out, "interlaced", InterlacingParam_AUTO)
	))
,	_interlaced_dst (static_cast <InterlacingParam> (
		get_arg_int (in, out, "interlacedd", _interlaced_src)
	))
,	_field_order_src (static_cast <FieldOrder> (
		get_arg_int (in, out, "tff", FieldOrder_AUTO)
	))
,	_field_order_dst (static_cast <FieldOrder> (
		get_arg_int (in, out, "tffd", _field_order_src)
	))
,	_int_flag (get_arg_int (in, out, "flt", 0) == 0)
,	_norm_flag (get_arg_int (in, out, "cnorm", 1) != 0)
,	_range_set_in_flag (false)
,	_range_set_out_flag (false)
,	_full_range_in_flag (false)
,	_full_range_out_flag (false)
,	_cplace_d_set_flag (false)
,	_cplace_s (fmtcl::ChromaPlacement_MPEG2)
,	_cplace_d (fmtcl::ChromaPlacement_MPEG2)
,	_sse2_flag (false)
,	_avx2_flag (false)
,	_plane_processor (vsapi, *this, "resample", true)
,	_filter_mutex ()
,	_filter_uptr_map ()
,	_plane_data_arr ()
{
	vsutl::CpuOpt  cpu_opt (*this, in, out);
	_sse2_flag = cpu_opt.has_sse2 ();
	_avx2_flag = cpu_opt.has_avx2 ();

	// Checks the input clip
	if (! vsutl::is_constant_format (_vi_in))
	{
		throw_inval_arg ("only constant formats are supported.");
	}

	const ::VSFormat &   fmt_src = *_vi_in.format;

	{
		const int            st  = fmt_src.sampleType;
		const int            bps = fmt_src.bytesPerSample;
		const int            res = fmt_src.bitsPerSample;
		if (! (   (st == ::stInteger && bps == 1 &&     res ==  8 )
		       || (st == ::stInteger && bps == 2 && (   res ==  9
		                                             || res == 10
		                                             || res == 12
		                                             || res == 16))
		       || (st == ::stFloat   && bps == 4 &&     res == 32 )))
		{
			throw_inval_arg ("input pixel bitdepth not supported.");
		}
		if (fmt_src.colorFamily == ::cmCompat)
		{
			throw_inval_arg ("\"compat\"colorspace not supported.");
		}
	}

	_src_width  = _vi_in.width;
	_src_height = _vi_in.height;
	SplFmtUtl::conv_from_vsformat (_src_type, _src_res, *_vi_in.format);

	// Destination colorspace
	::VSFormat     fmt_def = fmt_src;
	if (fmt_def.sampleType == ::stInteger && fmt_def.bitsPerSample < 16)
	{
		fmt_def.bitsPerSample = 16;
	}

	const ::VSFormat& fmt_dst = get_output_colorspace (in, out, core, fmt_def);

	// Checks the provided format
	const int      st  = fmt_dst.sampleType;
	const int      bps = fmt_dst.bytesPerSample;
	const int      res = fmt_dst.bitsPerSample;
	if (res < 16)
	{
		throw_inval_arg (
			"cannot output 8-, 9-, 12- or 12-bit data directly. "
			"Output to 16 bits then dither with fmtc.bitdepth."
		);
	}
	if (! (   (st == ::stInteger && bps == 2 && res == 16)
	       || (st == ::stFloat   && bps == 4 && res == 32)))
	{
		throw_inval_arg ("specified output pixel bitdepth not supported.");
	}
	if (   fmt_dst.colorFamily  != fmt_src.colorFamily
	    || fmt_dst.numPlanes    != fmt_src.numPlanes)
	{
		throw_inval_arg (
			"specified output colorspace is not compatible with input."
		);
	}

	// Done with the format
	_vi_out.format = &fmt_dst;

	SplFmtUtl::conv_from_vsformat (_dst_type, _dst_res, *_vi_out.format);

	if (_interlaced_src < 0 || _interlaced_src >= InterlacingParam_NBR_ELT)
	{
		throw_inval_arg ("interlaced argument out of range.");
	}
	if (_interlaced_dst < 0 || _interlaced_dst >= InterlacingParam_NBR_ELT)
	{
		throw_inval_arg ("interlacedd argument out of range.");
	}
	if (_field_order_src < 0 || _field_order_src >= FieldOrder_NBR_ELT)
	{
		throw_inval_arg ("tff argument out of range.");
	}
	if (_field_order_dst < 0 || _field_order_dst >= FieldOrder_NBR_ELT)
	{
		throw_inval_arg ("tffd argument out of range.");
	}

	_full_range_in_flag  = (get_arg_int (
		in, out, "fulls" ,
		vsutl::is_full_range_default (fmt_src) ? 1 : 0,
		0, &_range_set_in_flag
	) != 0);
	_full_range_out_flag = (get_arg_int (
		in, out, "fulld",
		_full_range_in_flag ? 1 : 0,
		0, &_range_set_out_flag
	) != 0);

	for (int plane_index = 0; plane_index < fmt_src.numPlanes; ++plane_index)
	{
		PlaneData &    plane_data = _plane_data_arr [plane_index];
		vsutl::compute_fmt_mac_cst (
			plane_data._gain,
			plane_data._add_cst,
			*_vi_out.format, _full_range_out_flag,
			fmt_src, _full_range_in_flag,
			plane_index
		);
	}

	// Target size: scale
	const double   scale  = get_arg_flt (in, out, "scale" ,     0);
	const double   scaleh = get_arg_flt (in, out, "scaleh", scale);
	const double   scalev = get_arg_flt (in, out, "scalev", scale);
	if (scaleh < 0 || scalev < 0)
	{
		throw_inval_arg ("scale parameters must be positive or 0.");
	}
	if (scaleh > 0)
	{
		const int      cssh = 1 << _vi_out.format->subSamplingW;
		const int      wtmp = fstb::round_int (_vi_out.width  * scaleh / cssh);
		_vi_out.width = std::max (wtmp, 1) * cssh;
	}
	if (scalev > 0)
	{
		const int      cssv = 1 << _vi_out.format->subSamplingH;
		const int      htmp = fstb::round_int (_vi_out.height * scalev / cssv);
		_vi_out.height = std::max (htmp, 1) * cssv;
	}

	// Target size: explicit dimensions
	_vi_out.width = get_arg_int (in, out, "w", _vi_out.width);
	if (_vi_out.width < 1)
	{
		throw_inval_arg ("w must be positive.");
	}
	else if ((_vi_out.width & ((1 << _vi_out.format->subSamplingW) - 1)) != 0)
	{
		throw_inval_arg (
			"w is not compatible with the output chroma subsampling."
		);
	}

	_vi_out.height = get_arg_int (in, out, "h", _vi_out.height);
	if (_vi_out.height < 1)
	{
		throw_inval_arg ("h must be positive.");
	}
	else if ((_vi_out.height & ((1 << _vi_out.format->subSamplingH) - 1)) != 0)
	{
		throw_inval_arg (
			"h is not compatible with the output chroma subsampling."
		);
	}

	// Chroma placement
	const std::string cplace_str = get_arg_str (in, out, "cplace", "mpeg2");
	_cplace_s = conv_str_to_chroma_placement (
		*this, get_arg_str (in, out, "cplaces", cplace_str)
	);
	_cplace_d = conv_str_to_chroma_placement (
		*this, get_arg_str (in, out, "cplaced", cplace_str, 0, &_cplace_d_set_flag)
	);

	// Per-plane parameters
	const int      nbr_sx = _vsapi.propNumElements (&in, "sx");
	const int      nbr_sy = _vsapi.propNumElements (&in, "sy");
	const int      nbr_sw = _vsapi.propNumElements (&in, "sw");
	const int      nbr_sh = _vsapi.propNumElements (&in, "sh");
	for (int plane_index = 0; plane_index < fmt_src.numPlanes; ++plane_index)
	{
		PlaneData &    plane_data = _plane_data_arr [plane_index];

		// Source window
		Win &          s = plane_data._win;
		if (plane_index > 0)
		{
			s = _plane_data_arr [plane_index - 1]._win;
		}
		else
		{
			s._x = 0;
			s._y = 0;
			s._w = 0;
			s._h = 0;
		}

		if (plane_index < nbr_sx)
		{
			s._x = get_arg_flt (in, out, "sx", s._x, plane_index);
		}
		if (plane_index < nbr_sy)
		{
			s._y = get_arg_flt (in, out, "sy", s._y, plane_index);
		}
		if (plane_index < nbr_sw)
		{
			s._w = get_arg_flt (in, out, "sw", s._w, plane_index);
		}
		if (plane_index < nbr_sh)
		{
			s._h = get_arg_flt (in, out, "sh", s._h, plane_index);
		}

		const double   eps = 1e-9;

		if (fstb::is_null (s._w))
		{
			s._w = _src_width;
		}
		else if (s._w < eps)
		{
			s._w = _src_width + s._w - s._x;
			if (s._w <= 0)
			{
				throw_inval_arg ("sw must be positive.");
			}
		}

		if (fstb::is_null (s._h))
		{
			s._h = _src_height;
		}
		else if (s._h < 0)
		{
			s._h = _src_height + s._h - s._y;
			if (s._h < eps)
			{
				throw_inval_arg ("sh must be positive.");
			}
		}

		// Kernel
		std::string    kernel_fnc     = get_arg_str (in, out, "kernel", "spline36", -plane_index);
		std::string    kernel_fnc_h   = get_arg_str (in, out, "kernelh", ""       , -plane_index);
		std::string    kernel_fnc_v   = get_arg_str (in, out, "kernelv", ""       , -plane_index);
		std::vector <double> impulse  =
			get_arg_vflt (in, out, "impulse", std::vector <double> ());
		std::vector <double> impulse_h =
			get_arg_vflt (in, out, "impulseh", impulse);
		std::vector <double> impulse_v =
			get_arg_vflt (in, out, "impulsev", impulse);
		const int      kovrspl        = get_arg_int (in, out, "kovrspl", 0   , -plane_index);
		const int      taps           = get_arg_int (in, out, "taps"   , 4   , -plane_index);
		const int      taps_h         = get_arg_int (in, out, "tapsh"  , taps, -plane_index);
		const int      taps_v         = get_arg_int (in, out, "tapsv"  , taps, -plane_index);
		bool           a1_flag, a1_h_flag, a1_v_flag;
		bool           a2_flag, a2_h_flag, a2_v_flag;
		bool           a3_flag, a3_h_flag, a3_v_flag;
		const double   a1             = get_arg_flt (in, out, "a1", 0.0, -plane_index, &a1_flag);
		const double   a2             = get_arg_flt (in, out, "a2", 0.0, -plane_index, &a2_flag);
		const double   a3             = get_arg_flt (in, out, "a3", 0.0, -plane_index, &a3_flag);
		const double   a1_h           = get_arg_flt (in, out, "a1h", a1, -plane_index, &a1_h_flag);
		const double   a2_h           = get_arg_flt (in, out, "a2h", a2, -plane_index, &a2_h_flag);
		const double   a3_h           = get_arg_flt (in, out, "a3h", a3, -plane_index, &a3_h_flag);
		const double   a1_v           = get_arg_flt (in, out, "a1v", a1, -plane_index, &a1_v_flag);
		const double   a2_v           = get_arg_flt (in, out, "a2v", a2, -plane_index, &a2_v_flag);
		const double   a3_v           = get_arg_flt (in, out, "a3v", a3, -plane_index, &a3_v_flag);
		const double   kernel_total_h = get_arg_flt (in, out, "totalh", 0.0, -plane_index);
		const double   kernel_total_v = get_arg_flt (in, out, "totalv", 0.0, -plane_index);
		const bool     invks_flag     = (get_arg_int (in, out, "invks", 0, -plane_index) != 0);
		const bool     invks_h_flag   = cumulate_flag (invks_flag, in, out, "invksh", -plane_index);
		const bool     invks_v_flag   = cumulate_flag (invks_flag, in, out, "invksv", -plane_index);
		const int      invks_taps     = get_arg_int (in, out, "invkstaps" , 4         , -plane_index);
		const int      invks_taps_h   = get_arg_int (in, out, "invkstapsh", invks_taps, -plane_index);
		const int      invks_taps_v   = get_arg_int (in, out, "invkstapsv", invks_taps, -plane_index);
		plane_data._kernel_scale_h    = get_arg_flt (in, out, "fh", 1.0, -plane_index);
		plane_data._kernel_scale_v    = get_arg_flt (in, out, "fv", 1.0, -plane_index);
		plane_data._preserve_center_flag = (get_arg_int (in, out, "center", 1, -plane_index) != 0);
		if (kernel_fnc_h.empty ())
		{
			kernel_fnc_h = kernel_fnc;
		}
		if (kernel_fnc_v.empty ())
		{
			kernel_fnc_v = kernel_fnc;
		}
		if (fstb::is_null (plane_data._kernel_scale_h))
		{
			throw_inval_arg ("fh cannot be null.");
		}
		if (fstb::is_null (plane_data._kernel_scale_v))
		{
			throw_inval_arg ("fv cannot be null.");
		}
		if (   taps_h < 1 || taps_h > 128
		    || taps_v < 1 || taps_v > 128)
		{
			throw_inval_arg ("taps* must be in the 1-128 range.");
		}
		if (kernel_total_h < 0)
		{
			throw_inval_arg ("totalh must be positive or null.");
		}
		if (kernel_total_v < 0)
		{
			throw_inval_arg ("totalv must be positive or null.");
		}
		if (   invks_taps_h < 1 || invks_taps_h > 128
		    || invks_taps_v < 1 || invks_taps_v > 128)
		{
			throw_inval_arg ("invkstaps* must be in the 1-128 range.");
		}

		// Serious stuff now
		plane_data._kernel_arr [fmtcl::FilterResize::Dir_H].create_kernel (
			kernel_fnc_h, impulse_h, taps_h,
			(a1_flag || a1_h_flag), a1_h,
			(a2_flag || a2_h_flag), a2_h,
			(a3_flag || a3_h_flag), a3_h,
			kovrspl,
			invks_h_flag,
			invks_taps_h
		);

		plane_data._kernel_arr [fmtcl::FilterResize::Dir_V].create_kernel (
			kernel_fnc_v, impulse_v, taps_v,
			(a1_flag || a1_v_flag), a1_v,
			(a2_flag || a2_v_flag), a2_v,
			(a3_flag || a3_v_flag), a3_v,
			kovrspl,
			invks_v_flag,
			invks_taps_v
		);
	}

	create_plane_specs ();
}



void	Resample::init_filter (::VSMap &in, ::VSMap &out, ::VSNode &node, ::VSCore &core)
{
	_vsapi.setVideoInfo (&_vi_out, 1, &node);
	_plane_processor.set_filter (in, out, _vi_out);
}



const ::VSFrameRef *	Resample::get_frame (int n, int activation_reason, void * &frame_data_ptr, ::VSFrameContext &frame_ctx, ::VSCore &core)
{
	assert (n >= 0);

	::VSFrameRef *    dst_ptr = 0;
	::VSNodeRef &     node = *_clip_src_sptr;

	if (activation_reason == ::arInitial)
	{
		_vsapi.requestFrameFilter (n, &node, &frame_ctx);
	}
	else if (activation_reason == ::arAllFramesReady)
	{
		vsutl::FrameRefSPtr	src_sptr (
			_vsapi.getFrameFilter (n, &node, &frame_ctx),
			_vsapi
		);
		const ::VSFrameRef & src = *src_sptr;

		dst_ptr = _vsapi.newVideoFrame (
			_vi_out.format,
			_vi_out.width,
			_vi_out.height,
			&src,
			&core
		);

		// Collects informations from the input frame properties
		FrameInfo      frame_info;
		get_interlacing_param (
			frame_info._itl_s_flag, frame_info._top_s_flag,
			n, src, _interlaced_src, _field_order_src
		);
		get_interlacing_param (
			frame_info._itl_d_flag, frame_info._top_d_flag,
			n, src, _interlaced_dst, _field_order_dst
		);
		frame_data_ptr = &frame_info;

		const int      ret_val = _plane_processor.process_frame (
			*dst_ptr, n, frame_data_ptr, frame_ctx, core, _clip_src_sptr
		);

		// Output frame properties
		if (ret_val == 0 && (   _range_set_out_flag
		                     || _cplace_d_set_flag
		                     || _interlaced_dst != InterlacingParam_AUTO))
		{
			::VSMap &      dst_prop = *(_vsapi.getFramePropsRW (dst_ptr));
			if (_range_set_out_flag)
			{
				const int      cr_val = (_full_range_out_flag) ? 0 : 1;
				_vsapi.propSetInt (&dst_prop, "_ColorRange", cr_val, ::paReplace);
			}
			if (_cplace_d_set_flag)
			{
				int            cl_val = -1; // Unknown or cannot be expressed
				if (   _cplace_d == fmtcl::ChromaPlacement_MPEG2
				    || (   _cplace_d == fmtcl::ChromaPlacement_DV
				        && _vi_out.format->subSamplingW == 2
				        && _vi_out.format->subSamplingH == 0))
				{
					cl_val = 0; // Left
				}
				else if (_cplace_d == fmtcl::ChromaPlacement_MPEG1)
				{
					cl_val = 1; // Center
				}

				if (cl_val >= 0)
				{
					_vsapi.propSetInt (&dst_prop, "_ChromaLocation", cl_val, ::paReplace);
				}
			}
			if (_interlaced_dst != InterlacingParam_AUTO)
			{
				if (frame_info._itl_d_flag)
				{
#if defined (FMTC_RESAMPLE_OLD_FIELDBASED_BEHAVIOUR)
					_vsapi.propSetInt (&dst_prop, "_FieldBased", 1, ::paReplace);
#endif
					if (_field_order_dst != FieldOrder_AUTO)
					{
#if ! defined (FMTC_RESAMPLE_OLD_FIELDBASED_BEHAVIOUR)
						_vsapi.propSetInt (
							&dst_prop, "_FieldBased",
							(_field_order_dst == FieldOrder_TFF) ? 1 : 2,
							::paReplace
						);
#endif
						_vsapi.propSetInt (
							&dst_prop, "_Field",
							(frame_info._top_d_flag) ? 1 : 0,
							::paReplace
						);
					}
				}
				else
				{
					_vsapi.propSetInt (&dst_prop, "_FieldBased", 0, ::paReplace);
					_vsapi.propDeleteKey (&dst_prop, "_Field");
				}
			}
		}

		if (ret_val != 0)
		{
			_vsapi.freeFrame (dst_ptr);
			dst_ptr = 0;
		}
	}

	return (dst_ptr);
}



fmtcl::ChromaPlacement	Resample::conv_str_to_chroma_placement (const vsutl::FilterBase &flt, std::string cplace)
{
	fmtcl::ChromaPlacement  cp_val = fmtcl::ChromaPlacement_MPEG1;

	fstb::conv_to_lower_case (cplace);
	if (strcmp (cplace.c_str (), "mpeg1") == 0)
	{
		cp_val = fmtcl::ChromaPlacement_MPEG1;
	}
	else if (strcmp (cplace.c_str (), "mpeg2") == 0)
	{
		cp_val = fmtcl::ChromaPlacement_MPEG2;
	}
	else if (strcmp (cplace.c_str (), "dv") == 0)
	{
		cp_val = fmtcl::ChromaPlacement_DV;
	}
	else
	{
		flt.throw_inval_arg ("unexpected cplace string.");
	}

	return (cp_val);
}



void	Resample::conv_str_to_chroma_subspl (const vsutl::FilterBase &flt, int &ssh, int &ssv, std::string css)
{
	const int      ret_val = vsutl::conv_str_to_chroma_subspl (ssh, ssv, css);
	if (ret_val != 0)
	{
		flt.throw_inval_arg ("unsupported css value.");
	}
}



/*\\\ PROTECTED \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



int	Resample::do_process_plane (::VSFrameRef &dst, int n, int plane_index, void *frame_data_ptr, ::VSFrameContext &frame_ctx, ::VSCore &core, const vsutl::NodeRefSPtr &src_node1_sptr, const vsutl::NodeRefSPtr &src_node2_sptr, const vsutl::NodeRefSPtr &src_node3_sptr)
{
	assert (src_node1_sptr.get () != 0);
	assert (frame_data_ptr != 0);

	int            ret_val = 0;

	const vsutl::PlaneProcMode proc_mode =
		_plane_processor.get_mode (plane_index);

	if (proc_mode == vsutl::PlaneProcMode_PROCESS)
	{
		process_plane_proc (
			dst, n, plane_index, frame_data_ptr, frame_ctx, core, src_node1_sptr
		);
	}

	// Copy (and convert)
	else if (proc_mode == vsutl::PlaneProcMode_COPY1)
	{
		process_plane_copy (
			dst, n, plane_index, frame_data_ptr, frame_ctx, core, src_node1_sptr
		);
	}

	// Fill
	else if (proc_mode < vsutl::PlaneProcMode_COPY1)
	{
		const double   val = _plane_processor.get_mode_val (plane_index);
		_plane_processor.fill_plane (dst, val, plane_index);
	}

	return (ret_val);
}



/*\\\ PRIVATE \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/



const ::VSFormat &	Resample::get_output_colorspace (const ::VSMap &in, ::VSMap &out, ::VSCore &core, const ::VSFormat &fmt_src) const
{
	const ::VSFormat *   fmt_dst_ptr = &fmt_src;

	// Full colorspace
	int            csp_dst = get_arg_int (in, out, "csp", ::pfNone);
	if (csp_dst != ::pfNone)
	{
		fmt_dst_ptr = _vsapi.getFormatPreset (csp_dst, &core);
		if (fmt_dst_ptr == 0)
		{
			throw_inval_arg ("unknown output colorspace.");
		}
	}

	int            col_fam  = fmt_dst_ptr->colorFamily;
	int            spl_type = fmt_dst_ptr->sampleType;
	int            bits     = fmt_dst_ptr->bitsPerSample;
	int            ssh      = fmt_dst_ptr->subSamplingW;
	int            ssv      = fmt_dst_ptr->subSamplingH;

	// Chroma subsampling
	std::string    css (get_arg_str (in, out, "css", ""));
	if (! css.empty ())
	{
		conv_str_to_chroma_subspl (*this, ssh, ssv, css);
	}

	// Combines the modified parameters and validates the format
	try
	{
		fmt_dst_ptr = register_format (
			col_fam,
			spl_type,
			bits,
			ssh,
			ssv,
			core
		);
	}
	catch (...)
	{
		fmt_dst_ptr = 0;
	}
	if (fmt_dst_ptr == 0)
	{
		throw_rt_err (
			"couldn\'t get a pixel format identifier for the output clip."
		);
	}

	return (*fmt_dst_ptr);
}



bool	Resample::cumulate_flag (bool flag, const ::VSMap &in, ::VSMap &out, const char name_0 [], int pos) const
{
	assert (name_0 != 0);

	if (is_arg_defined (in, name_0))
	{
		const int      val = get_arg_int (in, out, name_0, 0, pos);
		flag = (val != 0);
	}

	return (flag);
}



void	Resample::get_interlacing_param (bool &itl_flag, bool &top_flag, int field_index, const ::VSFrameRef &src, InterlacingParam interlaced, FieldOrder field_order) const
{
	assert (interlaced >= 0);
	assert (interlaced < InterlacingParam_NBR_ELT);
	assert (field_order >= 0);
	assert (field_order < FieldOrder_NBR_ELT);

	// Default: assumes interlaced only if explicitely specified.
	itl_flag = (interlaced == InterlacingParam_FIELDS);
	top_flag = true;

	// Fields specified or automatic
	if (interlaced != InterlacingParam_FRAMES)
	{
		const ::VSMap* src_prop_ptr = _vsapi.getFramePropsRO (&src);
		int            field_based = 0;
		if (src_prop_ptr != 0)
		{
			int            err = 0;
			field_based =
				int (_vsapi.propGetInt (src_prop_ptr, "_FieldBased", 0, &err));
			if (err == 0)
			{
				itl_flag = (itl_flag || field_based != 0);
			}
			else
			{
				field_based = 0;
			}
		}

		// Now finds the field order. First check explicit parameters.
		if (field_order == FieldOrder_BFF)
		{
			top_flag = ((field_index & 1) != 0);
		}
		else if (field_order == FieldOrder_TFF)
		{
			top_flag = ((field_index & 1) == 0);
		}

		// Else, assumes auto-detection. If it cannot be detected,
		// assumes simple frames.
		else if (src_prop_ptr == 0)
		{
			itl_flag = false;
		}
		else if (itl_flag)
		{
			int            err = 0;
			const int      field =
				int (_vsapi.propGetInt (src_prop_ptr, "_Field", 0, &err));
			if (err == 0)
			{
				top_flag = (field != 0);
			}
#if ! defined (FMTC_RESAMPLE_OLD_FIELDBASED_BEHAVIOUR)
			else if (field_based == 1 || field_based == 2)
			{
				top_flag = ((field_index & 1) == (field_based - 1));
			}
#endif
			else
			{
				// Or should we emit an error?
				itl_flag = false;
			}
		}
	}
}



int	Resample::process_plane_proc (::VSFrameRef &dst, int n, int plane_index, void *frame_data_ptr, ::VSFrameContext &frame_ctx, ::VSCore &core, const vsutl::NodeRefSPtr &src_node1_sptr)
{
	int            ret_val = 0;

	vsutl::FrameRefSPtr	src_sptr (
		_vsapi.getFrameFilter (n, src_node1_sptr.get (), &frame_ctx),
		_vsapi
	);
	const ::VSFrameRef & src = *src_sptr;

	const uint8_t* data_src_ptr = _vsapi.getReadPtr (&src, plane_index);
	const int      stride_src   = _vsapi.getStride (&src, plane_index);
	uint8_t *      data_dst_ptr = _vsapi.getWritePtr (&dst, plane_index);
	const int      stride_dst   = _vsapi.getStride (&dst, plane_index);

	const FrameInfo & frame_info =
		*reinterpret_cast <const FrameInfo *> (frame_data_ptr);
	const InterlacingType   itl_s =
		get_itl_type (frame_info._itl_s_flag, frame_info._top_s_flag);
	const InterlacingType   itl_d =
		get_itl_type (frame_info._itl_d_flag, frame_info._top_d_flag);

	try
	{
		fmtcl::FilterResize *   filter_ptr = create_or_access_plane_filter (
			plane_index,
			itl_d,
			itl_s
		);

		const bool     chroma_flag =
			vsutl::is_chroma_plane (*_vi_in.format, plane_index);

		filter_ptr->process_plane (
			data_dst_ptr, 0,
			data_src_ptr, 0,
			stride_dst,
			stride_src,
			chroma_flag
		);
	}

	catch (std::exception &e)
	{
		_vsapi.setFilterError (e.what (), &frame_ctx);
		ret_val = -1;
	}
	catch (...)
	{
		_vsapi.setFilterError ("resample: exception.", &frame_ctx);
		ret_val = -1;
	}

	return (ret_val);
}



int	Resample::process_plane_copy (::VSFrameRef &dst, int n, int plane_index, void *frame_data_ptr, ::VSFrameContext &frame_ctx, ::VSCore &core, const vsutl::NodeRefSPtr &src_node1_sptr)
{
	int            ret_val = 0;

	vsutl::FrameRefSPtr	src_sptr (
		_vsapi.getFrameFilter (n, src_node1_sptr.get (), &frame_ctx),
		_vsapi
	);
	const ::VSFrameRef & src = *src_sptr;

	const int      src_w = _vsapi.getFrameWidth (&src, plane_index);
	const int      src_h = _vsapi.getFrameHeight (&src, plane_index);
	const int      dst_w = _vsapi.getFrameWidth (&dst, plane_index);
	const int      dst_h = _vsapi.getFrameHeight (&dst, plane_index);

	const uint8_t* data_src_ptr = _vsapi.getReadPtr (&src, plane_index);
	const int      stride_src   = _vsapi.getStride (&src, plane_index);
	uint8_t *      data_dst_ptr = _vsapi.getWritePtr (&dst, plane_index);
	const int      stride_dst   = _vsapi.getStride (&dst, plane_index);

	const int      w = std::min (src_w, dst_w);
	const int      h = std::min (src_h, dst_h);

	// Copied from fmtcl::FilterResize::process_plane_bypass()
	fmtcl::BitBltConv::ScaleInfo *   scale_info_ptr = 0;
	fmtcl::BitBltConv::ScaleInfo     scale_info;
	const bool     dst_flt_flag = (_dst_type == fmtcl::SplFmt_FLOAT);
	const bool     src_flt_flag = (_src_type == fmtcl::SplFmt_FLOAT);
	if (dst_flt_flag != src_flt_flag)
	{
		const PlaneData & plane_data = _plane_data_arr [plane_index];
		scale_info._gain    = plane_data._gain;
		scale_info._add_cst = plane_data._add_cst;

		scale_info_ptr = &scale_info;
	}

	fmtcl::BitBltConv blitter (_sse2_flag, _avx2_flag);
	blitter.bitblt (
		_dst_type, _dst_res, data_dst_ptr, 0, stride_dst,
		_src_type, _src_res, data_src_ptr, 0, stride_src,
		w, h, scale_info_ptr
	);

	return (ret_val);
}



fmtcl::FilterResize *	Resample::create_or_access_plane_filter (int plane_index, InterlacingType itl_d, InterlacingType itl_s)
{
	assert (plane_index >= 0);
	assert (itl_d >= 0);
	assert (itl_d < InterlacingType_NBR_ELT);
	assert (itl_s >= 0);
	assert (itl_s < InterlacingType_NBR_ELT);

	const PlaneData & plane_data         = _plane_data_arr [plane_index];
	const fmtcl::ResampleSpecPlane & key = plane_data._spec_arr [itl_d] [itl_s];

	std::lock_guard <std::mutex>  autolock (_filter_mutex);

	std::unique_ptr <fmtcl::FilterResize> &   filter_uptr = _filter_uptr_map [key];
	if (filter_uptr.get () == 0)
	{
		filter_uptr = std::unique_ptr <fmtcl::FilterResize> (new fmtcl::FilterResize (
			key,
			*(plane_data._kernel_arr [fmtcl::FilterResize::Dir_H]._k_uptr),
			*(plane_data._kernel_arr [fmtcl::FilterResize::Dir_V]._k_uptr),
			_norm_flag, _norm_val_h, _norm_val_v,
			plane_data._gain,
			_src_type, _src_res, _dst_type, _dst_res,
			_int_flag, _sse2_flag, _avx2_flag
		));
	}

	return (filter_uptr.get ());
}



void	Resample::create_plane_specs ()
{
	fmtcl::ResampleSpecPlane   spec;

	const int      src_w = _vi_in.width;
	const int      src_h = _vi_in.height;
	const int      dst_w = _vi_out.width;
	const int      dst_h = _vi_out.height;
	const int      nbr_planes = _vi_in.format->numPlanes;

	for (int plane_index = 0; plane_index < nbr_planes; ++plane_index)
	{
		PlaneData &    plane_data = _plane_data_arr [plane_index];

		spec._src_width  =
			vsutl::compute_plane_width (*_vi_in.format, plane_index, src_w);
		spec._src_height =
			vsutl::compute_plane_height (*_vi_in.format, plane_index, src_h);
		spec._dst_width  =
			vsutl::compute_plane_width (*_vi_out.format, plane_index, dst_w);
		spec._dst_height =
			vsutl::compute_plane_height (*_vi_out.format, plane_index, dst_h);

		const int      subspl_h = src_w / spec._src_width;
		const int      subspl_v = src_h / spec._src_height;

		const Win &    s = plane_data._win;
		spec._win_x   = s._x / subspl_h;
		spec._win_y   = s._y / subspl_v;
		spec._win_w   = s._w / subspl_h;
		spec._win_h   = s._h / subspl_v;

		spec._add_cst        = plane_data._add_cst;
		spec._kernel_scale_h = plane_data._kernel_scale_h;
		spec._kernel_scale_v = plane_data._kernel_scale_v;
		spec._kernel_hash_h  = plane_data._kernel_arr [fmtcl::FilterResize::Dir_H].get_hash ();
		spec._kernel_hash_v  = plane_data._kernel_arr [fmtcl::FilterResize::Dir_V].get_hash ();

		for (int itl_d = 0; itl_d < InterlacingType_NBR_ELT; ++itl_d)
		{
			for (int itl_s = 0; itl_s < InterlacingType_NBR_ELT; ++itl_s)
			{
				double         cp_s_h = 0;
				double         cp_s_v = 0;
				double         cp_d_h = 0;
				double         cp_d_v = 0;
				if (plane_data._preserve_center_flag)
				{
					fmtcl::ChromaPlacement_compute_cplace (
						cp_s_h, cp_s_v, _cplace_s, plane_index,
						_vi_in.format->subSamplingW, _vi_in.format->subSamplingH,
						(_vi_in.format->colorFamily == ::cmRGB),
						(itl_s != InterlacingType_FRAME),
						(itl_s == InterlacingType_TOP)
					);
					fmtcl::ChromaPlacement_compute_cplace (
						cp_d_h, cp_d_v, _cplace_d, plane_index,
						_vi_out.format->subSamplingW, _vi_out.format->subSamplingH,
						(_vi_out.format->colorFamily == ::cmRGB),
						(itl_d != InterlacingType_FRAME),
						(itl_d == InterlacingType_TOP)
					);
				}

				spec._center_pos_src_h = cp_s_h;
				spec._center_pos_src_v = cp_s_v;
				spec._center_pos_dst_h = cp_d_h;
				spec._center_pos_dst_v = cp_d_v;

				plane_data._spec_arr [itl_d] [itl_s] = spec;
			}  // for itl_s
		}  // for itl_d
	}  // for plane_index
}



Resample::InterlacingType	Resample::get_itl_type (bool itl_flag, bool top_flag)
{
	return (
		(itl_flag) ? ((top_flag) ? InterlacingType_TOP
		                         : InterlacingType_BOT)
		           :               InterlacingType_FRAME
	);
}



}	// namespace fmtc



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
