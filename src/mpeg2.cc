/*
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "mpeg2.h"

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <va/va.h>

extern "C" {
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
}

#include "buffer.h"
#include "context.h"
#include "request.h"
#include "surface.h"
#include "v4l2.h"

namespace {

const uint8_t default_non_intra_quantisation_matrix_value = 16;
const uint8_t default_intra_quantisation_matrix[] = {
	 8, 16, 19, 22, 26, 27, 29, 34,
	16, 16, 22, 24, 27, 29, 34, 37,
	19, 22, 26, 27, 29, 34, 34, 38,
	22, 22, 26, 27, 29, 34, 37, 40,
	22, 26, 27, 29, 32, 35, 40, 48,
	26, 27, 29, 32, 35, 40, 48, 58,
	26, 27, 29, 34, 38, 46, 56, 69,
	27, 29, 35, 38, 46, 56, 69, 83,
};

}


VAStatus mpeg2_store_buffer(RequestData *driver_data,
				   Surface& surface,
				   const Buffer& buffer)
{
	const auto source_data = driver_data->device.buffer(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, surface.destination_index).mapping()[0];

	switch (buffer.type) {
	case VAPictureParameterBufferType:
		surface.params.mpeg2.picture = reinterpret_cast<VAPictureParameterBufferMPEG2*>(buffer.data.get());
		break;

	case VAIQMatrixBufferType:
		surface.params.mpeg2.iqmatrix = reinterpret_cast<VAIQMatrixBufferMPEG2*>(buffer.data.get());
		break;

	case VASliceParameterBufferType:
		// FIXME: This is passed by libva but ignoring it works for the
		// current test media. If you are looking for a reason why MPEG2
		// decoding isn't working, this is likely it.
		break;

	case VASliceDataBufferType:
		/*
		 * Since there is no guarantee that the allocation
		 * order is the same as the submission order (via
		 * RenderPicture), we can't use a V4L2 buffer directly
		 * and have to copy from a regular buffer.
		 */
		if (surface.source_size_used + buffer.size * buffer.count > source_data.size()) {
			return VA_STATUS_ERROR_NOT_ENOUGH_BUFFER;
		}
		memcpy(source_data.data() + surface.source_size_used, buffer.data.get(), buffer.size * buffer.count);
		surface.source_size_used += buffer.size * buffer.count;
		break;

	default:
		return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
	}

	return VA_STATUS_SUCCESS;
}


int mpeg2_set_controls(RequestData *driver_data,
		       const Context& context,
		       Surface& surface)
{
	VAPictureParameterBufferMPEG2 *va_picture = surface.params.mpeg2.picture;
	VAIQMatrixBufferMPEG2 *iqmatrix = surface.params.mpeg2.iqmatrix;
	v4l2_ctrl_mpeg2_picture picture = {};
	v4l2_ctrl_mpeg2_sequence sequence = {};
	v4l2_ctrl_mpeg2_quantisation quantisation = {};
	unsigned int i;

	sequence.horizontal_size = va_picture->horizontal_size;
	sequence.vertical_size = va_picture->vertical_size;
	sequence.vbv_buffer_size = SOURCE_SIZE_MAX;

	sequence.profile_and_level_indication = 0;
	sequence.chroma_format = 1; // 4:2:0

	try {
		driver_data->device.set_control(surface.request_fd, V4L2_CID_STATELESS_MPEG2_SEQUENCE,
				&sequence, sizeof(sequence));
	} catch(std::runtime_error& e) {
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	picture.picture_coding_type = va_picture->picture_coding_type;
	picture.f_code[0][0] = (va_picture->f_code >> 12) & 0x0f;
	picture.f_code[0][1] = (va_picture->f_code >> 8) & 0x0f;
	picture.f_code[1][0] = (va_picture->f_code >> 4) & 0x0f;
	picture.f_code[1][1] = (va_picture->f_code >> 0) & 0x0f;

	picture.intra_dc_precision = va_picture->picture_coding_extension.bits.intra_dc_precision;
	picture.picture_structure = va_picture->picture_coding_extension.bits.picture_structure;

	const auto& backward_reference_surface = driver_data->surfaces.find(va_picture->backward_reference_picture);
	picture.backward_ref_ts = v4l2_timeval_to_ns((backward_reference_surface != driver_data->surfaces.end()) ? &backward_reference_surface->second.timestamp : &surface.timestamp);

	const auto& forward_reference_surface = driver_data->surfaces.find(va_picture->forward_reference_picture);
	picture.forward_ref_ts = v4l2_timeval_to_ns((forward_reference_surface != driver_data->surfaces.end()) ? &forward_reference_surface->second.timestamp : &surface.timestamp);

	picture.flags = (
		(va_picture->picture_coding_extension.bits.top_field_first ? V4L2_MPEG2_PIC_FLAG_TOP_FIELD_FIRST : 0) |
		(va_picture->picture_coding_extension.bits.frame_pred_frame_dct ? V4L2_MPEG2_PIC_FLAG_FRAME_PRED_DCT : 0) |
		(va_picture->picture_coding_extension.bits.concealment_motion_vectors ? V4L2_MPEG2_PIC_FLAG_CONCEALMENT_MV : 0) |
		(va_picture->picture_coding_extension.bits.q_scale_type ? V4L2_MPEG2_PIC_FLAG_Q_SCALE_TYPE : 0) |
		(va_picture->picture_coding_extension.bits.intra_vlc_format ? V4L2_MPEG2_PIC_FLAG_INTRA_VLC : 0) |
		(va_picture->picture_coding_extension.bits.alternate_scan ? V4L2_MPEG2_PIC_FLAG_ALT_SCAN : 0) |
		(va_picture->picture_coding_extension.bits.repeat_first_field ? V4L2_MPEG2_PIC_FLAG_REPEAT_FIRST : 0) |
		(va_picture->picture_coding_extension.bits.progressive_frame ? V4L2_MPEG2_PIC_FLAG_PROGRESSIVE : 0)
	);

	try {
		driver_data->device.set_control(surface.request_fd, V4L2_CID_STATELESS_MPEG2_PICTURE,
				&picture, sizeof(picture));
	} catch(std::runtime_error& e) {
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}


	if (iqmatrix) {
		for (i = 0; i < 64; i++) {
			// The V4L2 API allows to set all or none of the quantisation matrices, so use default values for matrices that are not to be loaded.
			quantisation.intra_quantiser_matrix[i] = iqmatrix->load_intra_quantiser_matrix ? iqmatrix->intra_quantiser_matrix[i] : default_non_intra_quantisation_matrix_value;
			quantisation.non_intra_quantiser_matrix[i] = iqmatrix->load_non_intra_quantiser_matrix ? iqmatrix->non_intra_quantiser_matrix[i] : default_non_intra_quantisation_matrix_value;
			quantisation.chroma_intra_quantiser_matrix[i] = iqmatrix->load_chroma_intra_quantiser_matrix ? iqmatrix->chroma_intra_quantiser_matrix[i] : default_intra_quantisation_matrix[i];
			quantisation.chroma_non_intra_quantiser_matrix[i] = iqmatrix->load_chroma_non_intra_quantiser_matrix ? iqmatrix->chroma_non_intra_quantiser_matrix[i] : default_intra_quantisation_matrix[i];
		}

		try {
			driver_data->device.set_control(surface.request_fd, V4L2_CID_STATELESS_MPEG2_QUANTISATION,
					&quantisation, sizeof(quantisation));
		} catch(std::runtime_error& e) {
			return VA_STATUS_ERROR_OPERATION_FAILED;
		}
	}

	return 0;
}

std::vector<VAProfile> mpeg2_supported_profiles(const V4L2M2MDevice& device) {
	return (device.format_supported(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_PIX_FMT_MPEG2_SLICE)) ?
		std::vector<VAProfile>({VAProfileMPEG2Main, VAProfileMPEG2Simple}) : std::vector<VAProfile>();
};
