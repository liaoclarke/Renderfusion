#include "imgsupport.hpp"
#include <iostream>

#define PNG_BYTES_TO_CHECK 8

namespace vina{
  PngImage::PngImage() : file_(0)
  {}
  PngImage::~PngImage() 
  {
    if ( file_ ) {
      if ( reader_ ) {
        // png_read_end can be omitted if no processing of post-IDAT text/time/etc.l
        //png_read_end(handle_, info_);
        png_destroy_read_struct(&handle_, &info_, png_infopp_NULL);
      }
      else {
        png_write_end(handle_, info_);
        png_destroy_write_struct(&handle_, &info_);
      }
      fclose(file_);
    }
   
  }
  
  bool PngImage::loadFromFile(const char * filename)
  {
    char buf[PNG_BYTES_TO_CHECK];

    if ((file_ = fopen(filename, "rb")) == NULL) 
      throw io_exception();

    if (fread(buf, 1, PNG_BYTES_TO_CHECK, file_) != PNG_BYTES_TO_CHECK) {
      fclose(file_);
      file_ = 0;
      throw io_exception();
    }
    if (!png_check_sig((png_byte*)buf, 8)) {
      fclose(file_);
      file_ = 0;
      return false;
    }
    
    handle_ = png_create_read_struct(PNG_LIBPNG_VER_STRING,  NULL, NULL, NULL);
    if (handle_ == NULL) {
      fclose(file_);
      file_ = 0;
      return false;
    }
    
    info_ = png_create_info_struct(handle_);
    if ( info_ == NULL ) {
      png_destroy_read_struct(&handle_, NULL, NULL);
      fclose(file_);
      file_ = 0;
      return false;
    }
    if ( setjmp(png_jmpbuf(handle_)) ) {
      png_destroy_read_struct(&handle_, &info_, NULL);
      fclose(file_);
      file_ = 0;
      throw io_exception();
    }
    png_init_io(handle_, file_);
    png_set_sig_bytes(handle_, PNG_BYTES_TO_CHECK);
    png_read_info(handle_, info_);
    png_get_IHDR(handle_, info_, &width_, &height_, 
        &bit_depth_,  &color_type_, 
        NULL, NULL, NULL);
  
    return reader_ = true;
  }
  bool PngImage::storeToFile(const char * filename, 
            unsigned width, unsigned height,
            int bit_depth, int color_type)
  {
    if ((file_ = fopen(filename, "wb")) == NULL ) 
      throw io_exception();

    handle_ = png_create_write_struct(PNG_LIBPNG_VER_STRING,
            NULL, NULL, NULL);
    if (handle_ == NULL) {
      fclose(file_);
      file_ = 0;
      return false;
    }

    info_ = png_create_info_struct(handle_);
    if (info_ == NULL) {
      png_destroy_write_struct(&handle_, png_infopp_NULL);
      fclose(file_);
      file_ = 0;
      return false;
    }
    if (setjmp(png_jmpbuf(handle_))) {
      png_destroy_write_struct(&handle_, png_infopp_NULL);
      fclose(file_);
      file_ = 0;
      throw io_exception();
    }
    png_init_io(handle_, file_);

    width_ = width;
    height_ = height;
    bit_depth_ = bit_depth;
    color_type_ = color_type;
    return !(reader_ = false);
  }

  double PngImage::getGamma() const {
    double image_gamma;
    png_get_gAMA(handle_, info_, &image_gamma);
    return image_gamma;
  }
  void PngImage::setWidth(unsigned w) {
    width_ = w;
  }
  void PngImage::setHeight(unsigned h) {
    height_ = h;
  }
  void PngImage::setColorType(int ct) {
    color_type_ = ct;
  }
  void PngImage::setGamma(double file_gamma=.45455) {
#ifdef __DARWIN__
    double screen_gamma = 1.7; // a good guess for Mac systems
#else 
    double screen_gamma = 2.2;
#endif
    png_set_gamma(handle_, screen_gamma, file_gamma);
  }
}// end of NS
