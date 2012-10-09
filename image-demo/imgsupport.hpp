#ifndef VINA_IMG_SUPPORT_HXX
#define VINA_IMG_SUPPORT_HXX

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
namespace vina{
  struct image_exception{};
  struct io_exception{};
  class PngImage{
  public:
    PngImage();
    ~PngImage();
    bool loadFromFile(const char * file);
    bool storeToFile(const char * file, 
		     unsigned width, unsigned height, 
		     int bit_depth, int color_type);
    
    unsigned getWidth() const { return width_; }
    void setWidth(unsigned w);
    unsigned getHeight() const { return height_; }
    void setHeight(unsigned h);

    int getColorType() const { return color_type_; }
    void setColorType(int ct);

    int getBitDepth() const { return bit_depth_; }
    void setBitDepth();

    double getGamma() const ;
    void setGamma(double gamma);

    int getChannel() const { 
      int channel;
      if ( reader_ ) 
        channel = (int)png_get_channels(handle_, info_);
      else {   
        if ( color_type_ & PNG_COLOR_MASK_PALETTE) {
            channel = 1;
            return channel;
        }
        else if ( color_type_ & PNG_COLOR_MASK_COLOR ) 
            channel = 3;
        else channel = 1; // gray

        channel = color_type_ & PNG_COLOR_MASK_ALPHA ? (channel+1) : channel;
      }
      return channel;
    }
    /*
    ///return 0 if the data is not available or
    // res_x != res_y
    int getDpi() const {
      return png_get_pixels_per_inch(handle_, info_);
    }
    int getDpi_x() const {
      return png_get_x_pixels_per_inch(handle_, info_);
    }
    int getDpi_y() const {
      return png_get_y_pixels_per_inch(handle_, info_);
    }
    */
    unsigned char * getData() {
      assert( file_ != 0 && reader_ && "reader file is invalid");

      unsigned char * data = (unsigned char *)malloc(height_ * png_get_rowbytes(handle_, info_));
      unsigned char * row_pointers[height_];

      for (int row=0; row<height_; ++row) {
        row_pointers[row] = data + row * png_get_rowbytes(handle_, info_);
      }
      png_read_image(handle_, (png_byte **)row_pointers);

      return raw_data_ = data;
    }

    void setData(unsigned char * raw) {
      assert( file_ != 0 && !reader_ && "writer file is invalid");
      unsigned char * row_pointers[height_];

      int bytes_per_pixel = getChannel() * (bit_depth_ >> 3);
      png_set_IHDR(handle_, info_, width_, height_, bit_depth_, color_type_, 0, 0, 0);
      png_write_info(handle_, info_);
      for (int k=0; k<height_; ++k)
        row_pointers[k] = raw + k * width_ * bytes_per_pixel;
      png_write_image(handle_, (png_byte **)row_pointers);
      raw_data_ = raw;
    }
    int getBPP() const {
        return  getChannel() * (bit_depth_ >> 3);
    }
    int getRowBytes() const {
        return png_get_rowbytes(handle_, info_);
    }
  private:
    //properties
    png_uint_32 width_, height_;
    int bit_depth_;
    int color_type_;
    double gamma_;

    png_structp handle_;
    png_infop info_;
    FILE * file_;
    bool reader_;
    unsigned char * raw_data_;
  };
}
#endif //VINA_IMG_SUPPORT_HXX
