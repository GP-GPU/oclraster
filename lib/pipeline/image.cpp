/*
 *  Flexible OpenCL Rasterizer (oclraster)
 *  Copyright (C) 2012 - 2013 Florian Ziesche
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License only.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "image.h"
#include "oclraster.h"

static constexpr array<size_t, (size_t)IMAGE_TYPE::__MAX_TYPE> image_type_sizes {
	{
		0,
		1, 2, 4, 8, // INT*
		1, 2, 4, 8, // UINT*
		2, 4, 8, // FLOAT*
	}
};
static constexpr array<size_t, (size_t)IMAGE_CHANNEL::__MAX_CHANNEL> image_channel_sizes {
	{
		0,
		1, 2, 3, 4, // R/RG/RGB/RGBA
	}
};

bool is_correct_format(const SDL_PixelFormat& format, const IMAGE_CHANNEL& channel_order);
bool is_correct_format(const SDL_PixelFormat& format, const IMAGE_CHANNEL& channel_order) {
	switch(channel_order) {
#if defined(SDL_LIL_ENDIAN)
		case IMAGE_CHANNEL::R:
			if(format.Rmask != 0xFF ||
			   format.Gmask != 0 ||
			   format.Bmask != 0 ||
			   format.Amask != 0) return false;
			break;
		case IMAGE_CHANNEL::RG:
			if(format.Rmask != 0xFF ||
			   format.Gmask != 0xFF00 ||
			   format.Bmask != 0 ||
			   format.Amask != 0) return false;
			break;
		case IMAGE_CHANNEL::RGB:
			if(format.Rmask != 0xFF ||
			   format.Gmask != 0xFF00 ||
			   format.Bmask != 0xFF0000 ||
			   format.Amask != 0) return false;
			break;
		case IMAGE_CHANNEL::RGBA:
			if(format.Rmask != 0xFF ||
			   format.Gmask != 0xFF00 ||
			   format.Bmask != 0xFF0000 ||
			   format.Amask != 0xFF000000) return false;
			break;
#elif defined(SDL_BIG_ENDIAN)
		case IMAGE_CHANNEL::R:
			if(format.Rmask != 0xFF ||
			   format.Gmask != 0 ||
			   format.Bmask != 0 ||
			   format.Amask != 0) return false;
			break;
		case IMAGE_CHANNEL::RG:
			if(format.Rmask != 0xFF00 ||
			   format.Gmask != 0xFF ||
			   format.Bmask != 0 ||
			   format.Amask != 0) return false;
			break;
		case IMAGE_CHANNEL::RGB:
			if(format.Rmask != 0xFF0000 ||
			   format.Gmask != 0xFF00 ||
			   format.Bmask != 0xFF ||
			   format.Amask != 0) return false;
			break;
		case IMAGE_CHANNEL::RGBA:
			if(format.Rmask != 0xFF000000 ||
			   format.Gmask != 0xFF0000 ||
			   format.Bmask != 0xFF00 ||
			   format.Amask != 0xFF) return false;
			break;
#else
#error "unknown endianness"
#endif
		case IMAGE_CHANNEL::NONE:
		case IMAGE_CHANNEL::__MAX_CHANNEL:
			oclr_unreachable();
	}
	return true;
}

image image::from_file(const string& filename, const BACKING& backing,
					   const IMAGE_TYPE& type, const IMAGE_CHANNEL& channel_order) {
	const auto fail_return = [&filename, &backing](const string& error_msg) -> image {
		oclr_error("%s (\"%s\"): %s!", error_msg, filename, SDL_GetError());
		const unsigned int fail_pixel = 0xDEADBEEF;
		return image(1, 1, backing, IMAGE_TYPE::UINT_8, IMAGE_CHANNEL::RGBA, &fail_pixel);
	};
	if(type >= IMAGE_TYPE::__MAX_TYPE) return fail_return("invalid image type");
	if(channel_order >= IMAGE_CHANNEL::__MAX_CHANNEL) return fail_return("invalid channel type");
	
	//
	SDL_Surface* surface = IMG_Load(filename.c_str());
	if(surface == nullptr) {
		return fail_return("failed to load image");
	}
	
	// check if the loaded surface must be converted to match the requested channel order (format)
	// note that this will only work for INT_8 and UINT_8 images (SDL only supports these directly)
	SDL_PixelFormat* format = surface->format;
	const size_t channel_count = image_channel_sizes[static_cast<underlying_type<IMAGE_CHANNEL>::type>(channel_order)];
	const size_t type_size = image_type_sizes[static_cast<underlying_type<IMAGE_TYPE>::type>(type)];
	if((type == IMAGE_TYPE::INT_8 || type == IMAGE_TYPE::UINT_8) &&
	   (format->BytesPerPixel != (channel_count * type_size) ||
		!is_correct_format(*format, channel_order))) {
		SDL_PixelFormat correct_format;
		memcpy(&correct_format, format, sizeof(SDL_PixelFormat));
		
		//
		correct_format.BytesPerPixel = channel_count * type_size;
		correct_format.BitsPerPixel = channel_count * type_size * 8;
		switch(channel_order) {
#if defined(SDL_LIL_ENDIAN)
			case IMAGE_CHANNEL::R:
				correct_format.Gshift = 0;
				correct_format.Bshift = 0;
				correct_format.Ashift = 0;
				correct_format.Gmask = 0;
				correct_format.Bmask = 0;
				correct_format.Amask = 0;
				
				correct_format.Rmask = 0xFF;
				correct_format.Rshift = 0;
				break;
			case IMAGE_CHANNEL::RG:
				correct_format.Bshift = 0;
				correct_format.Ashift = 0;
				correct_format.Bmask = 0;
				correct_format.Amask = 0;
				
				correct_format.Rmask = 0xFF;
				correct_format.Rshift = 0;
				correct_format.Gmask = 0xFF00;
				correct_format.Gshift = 8;
				break;
			case IMAGE_CHANNEL::RGB:
				correct_format.Ashift = 0;
				correct_format.Amask = 0;
				
				correct_format.Rmask = 0xFF;
				correct_format.Rshift = 0;
				correct_format.Gmask = 0xFF00;
				correct_format.Gshift = 8;
				correct_format.Bmask = 0xFF0000;
				correct_format.Bshift = 16;
				break;
			case IMAGE_CHANNEL::RGBA:
				correct_format.Rmask = 0xFF;
				correct_format.Rshift = 0;
				correct_format.Gmask = 0xFF00;
				correct_format.Gshift = 8;
				correct_format.Bmask = 0xFF0000;
				correct_format.Bshift = 16;
				correct_format.Amask = 0xFF000000;
				correct_format.Ashift = 24;
				break;
#elif defined(SDL_BIG_ENDIAN)
			case IMAGE_CHANNEL::R:
				correct_format.Gshift = 0;
				correct_format.Bshift = 0;
				correct_format.Ashift = 0;
				correct_format.Gmask = 0;
				correct_format.Bmask = 0;
				correct_format.Amask = 0;
				
				correct_format.Rmask = 0xFF;
				correct_format.Rshift = 0;
				break;
			case IMAGE_CHANNEL::RG:
				correct_format.Bshift = 0;
				correct_format.Ashift = 0;
				correct_format.Bmask = 0;
				correct_format.Amask = 0;
				
				correct_format.Rmask = 0xFF00;
				correct_format.Rshift = 8;
				correct_format.Gmask = 0xFF;
				correct_format.Gshift = 0;
				break;
			case IMAGE_CHANNEL::RGB:
				correct_format.Ashift = 0;
				correct_format.Amask = 0;
				
				correct_format.Rmask = 0xFF0000;
				correct_format.Rshift = 16;
				correct_format.Gmask = 0xFF00;
				correct_format.Gshift = 8;
				correct_format.Bmask = 0xFF;
				correct_format.Bshift = 0;
				break;
			case IMAGE_CHANNEL::RGBA:
				correct_format.Rmask = 0xFF000000;
				correct_format.Rshift = 24;
				correct_format.Gmask = 0xFF0000;
				correct_format.Gshift = 16;
				correct_format.Bmask = 0xFF00;
				correct_format.Bshift = 8;
				correct_format.Amask = 0xFF;
				correct_format.Ashift = 0;
				break;
#else
#error "unknown endianness"
#endif
			case IMAGE_CHANNEL::NONE:
			case IMAGE_CHANNEL::__MAX_CHANNEL:
				oclr_unreachable();
		}
		
		SDL_Surface* converted_surface = SDL_ConvertSurface(surface, &correct_format, 0);
		if(converted_surface == nullptr) {
			return fail_return("failed to convert image to correct format");
		}
		SDL_FreeSurface(surface);
		surface = converted_surface;
	}
	else if(type != IMAGE_TYPE::INT_8 && type != IMAGE_TYPE::UINT_8) {
		return fail_return("automatic conversion to image types != INT_8 or UINT_8 not supported");
	}
	
	image img(surface->w, surface->h, backing, type, channel_order, surface->pixels);
	SDL_FreeSurface(surface);
	return img;
}

image::image(const unsigned int& width, const unsigned int& height,
			 const BACKING& backing_,
			 const IMAGE_TYPE& type,
			 const IMAGE_CHANNEL& channel_order_,
			 const void* pixels) :
backing(backing_), img_type(type, channel_order_), data_type(type), channel_order(channel_order_), size(width, height), native_format(0, 0) {
	create_buffer(pixels);
}

image::image(const unsigned int& width, const unsigned int& height,
			 const IMAGE_TYPE& type,
			 const IMAGE_CHANNEL& channel_order_,
			 const cl::ImageFormat& native_format_,
			 const void* pixels) :
backing(BACKING::IMAGE), img_type(type, channel_order_), data_type(type), channel_order(channel_order_), size(width, height), native_format(native_format_) {
	create_buffer(pixels);
}

void image::create_buffer(const void* pixels) {
#if defined(OCLRASTER_DEBUG)
	if(data_type >= IMAGE_TYPE::__MAX_TYPE) {
		oclr_error("invalid image type: %u!", data_type);
		return;
	}
	if(channel_order >= IMAGE_CHANNEL::__MAX_CHANNEL) {
		oclr_error("invalid image channel order type: %u!", channel_order);
		return;
	}
#endif
	
	if(backing == BACKING::IMAGE) {
		// if constructed with a native format, check if the specified format is supported
		if(native_format.image_channel_data_type != 0 ||
		   native_format.image_channel_order != 0) {
			bool found = false;
			for(const auto& format : ocl->get_image_formats()) {
				if(format.image_channel_order == native_format.image_channel_order &&
				   format.image_channel_data_type == native_format.image_channel_data_type) {
					found = true;
					break;
				}
			}
			if(!found) {
				// not supported, reset and look for a compatible one
				oclr_error("specified native image format (%X %X) not supported - checking for compatible image format ...",
						   native_format.image_channel_data_type, native_format.image_channel_order);
				native_format.image_channel_data_type = 0;
				native_format.image_channel_order = 0;
			}
		}
		
		// look for a supported/compatible image format
		if(native_format.image_channel_data_type == 0 ||
		   native_format.image_channel_order == 0) {
			native_format = ocl->get_image_format(data_type, channel_order);
		}
		if(native_format.image_channel_data_type == 0 ||
		   native_format.image_channel_order == 0) {
			oclr_error("image format \"%s\" is not natively supported - falling back to buffer based image backing!",
					   image_type { data_type, channel_order }.to_string());
			backing = BACKING::BUFFER;
		}
	}
	
	if(backing == BACKING::BUFFER) {
		const size_t data_size = (size.x * size.y *
								  image_type_sizes[static_cast<underlying_type<IMAGE_TYPE>::type>(data_type)] *
								  image_channel_sizes[static_cast<underlying_type<IMAGE_CHANNEL>::type>(channel_order)]);
		const size_t buffer_size = header_size() + data_size;
		buffer = ocl->create_buffer(opencl::BUFFER_FLAG::READ_WRITE |
									opencl::BUFFER_FLAG::BLOCK_ON_READ |
									opencl::BUFFER_FLAG::BLOCK_ON_WRITE,
									buffer_size);
		
		// init buffer ...
		auto mapped_ptr = ocl->map_buffer(buffer,
										  opencl::MAP_BUFFER_FLAG::WRITE_INVALIDATE |
										  opencl::MAP_BUFFER_FLAG::BLOCK);
		
		header* header_ptr = (header*)mapped_ptr;
		header_ptr->type = data_type;
		header_ptr->channel_order = channel_order;
		header_ptr->width = size.x;
		header_ptr->height = size.y;
		
		unsigned char* data_ptr = (unsigned char*)mapped_ptr + header_size();
		if(pixels == nullptr) {
			// ... with 0s
			fill_n(data_ptr, data_size, 0);
		}
		else {
			// ... with the specified pixels
			copy_n((const unsigned char*)pixels, data_size, data_ptr);
		}
		ocl->unmap_buffer(buffer, mapped_ptr);
	}
	else {
		img_type.native = true;
		buffer = ocl->create_image2d_buffer(opencl::BUFFER_FLAG::READ_WRITE |
											opencl::BUFFER_FLAG::BLOCK_ON_READ |
											opencl::BUFFER_FLAG::BLOCK_ON_WRITE |
											(pixels != NULL ? opencl::BUFFER_FLAG::INITIAL_COPY : opencl::BUFFER_FLAG::NONE),
											native_format.image_channel_order, native_format.image_channel_data_type,
											size.x, size.y, (void*)pixels);
		if(buffer->image_buffer == nullptr) {
			oclr_error("image buffer creation failed!");
		}
	}
}

image::~image() {
	if(buffer != nullptr && ocl != nullptr) {
		ocl->delete_buffer(buffer);
	}
}

image::image(image&& img) :
backing(img.backing), img_type(img.img_type), data_type(img.data_type), channel_order(img.channel_order),
size(img.size), buffer(img.buffer), native_format(img.native_format) {
	img.buffer = nullptr;
}

image::BACKING image::get_backing() const {
	return backing;
}

const opencl::buffer_object* image::get_buffer() const {
	return buffer;
}

opencl::buffer_object* image::get_buffer() {
	return buffer;
}

image_type image::get_image_type() const {
	return img_type;
}

IMAGE_TYPE image::get_data_type() const {
	return data_type;
}

IMAGE_CHANNEL image::get_channel_order() const {
	return channel_order;
}

const uint2& image::get_size() const {
	return size;
}

const cl::ImageFormat& image::get_native_format() const {
	return native_format;
}

// TODO: handle offset and size correclty for buffer-backed images (right now, it's wrapping around and not using the correct image region)
size2 image::compute_buffer_offset_and_size(const uint2& offset, const uint2& size_) const {
	const size_t pixel_size = img_type.pixel_size();
	const size_t buffer_size = (size_.x == ~0u && size_.y == ~0u ? 0 :
							   (size_.y * (size.x * pixel_size) + size_.x * pixel_size));
	const size_t buffer_offset = offset.y * (size.x * pixel_size) + offset.x * pixel_size;
	return { buffer_offset, buffer_size };
}

void image::write(const void* src, const uint2 offset, const uint2 size_) {
	if(backing == BACKING::BUFFER) {
		const auto buffer_info = compute_buffer_offset_and_size(offset, size_);
		ocl->write_buffer(buffer, src, buffer_info.x, buffer_info.y);
	}
	else {
		const uint2 write_size = (size_.x == ~0u && size_.y == ~0u ? size : size_);
		ocl->write_image2d(buffer, src, offset, write_size);
	}
}

void image::read(void* dst, const uint2 offset, const uint2 size_) {
	if(backing == BACKING::BUFFER) {
		const auto buffer_info = compute_buffer_offset_and_size(offset, size_);
		ocl->read_buffer(dst, buffer, buffer_info.x, buffer_info.y);
	}
	else {
		// TODO: implement this
	}
}

void image::copy(const image& src_img, const uint2 src_offset, const uint2 dst_offset, const uint2 size_) {
	// TODO: implement this
	if(backing == BACKING::BUFFER) {
		if(src_img.get_backing() == BACKING::BUFFER) {
			// buffer -> buffer
		}
		else {
			// image -> buffer
		}
	}
	else {
		if(src_img.get_backing() == BACKING::IMAGE) {
			// image -> image
		}
		else {
			// buffer -> image
		}
	}
}

void* image::map(const uint2 offset, const uint2 size) {
	// TODO: offset + size
	if(backing == BACKING::BUFFER) {
		auto mapped_ptr = ocl->map_buffer(buffer,
										  opencl::MAP_BUFFER_FLAG::READ_WRITE |
										  opencl::MAP_BUFFER_FLAG::BLOCK);
		return (void*)((unsigned char*)mapped_ptr + header_size());
	}
	else {
		return ocl->map_buffer(buffer, opencl::MAP_BUFFER_FLAG::READ_WRITE | opencl::MAP_BUFFER_FLAG::BLOCK);
	}
}

void image::unmap(void* mapped_ptr) {
	ocl->unmap_buffer(buffer,
					  (backing == BACKING::BUFFER ?
					   (void*)((unsigned char*)mapped_ptr - header_size()) :
					   mapped_ptr));
}

bool image::modify_backing(const BACKING& new_backing) {
	// TODO: implement this
	if(backing == BACKING::BUFFER) {
	}
	else {
	}
	return false;
}
