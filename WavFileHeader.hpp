#ifndef WAVFILEHEADER_HPP
#define WAVFILEHEADER_HPP

#include <stdint.h>
/*
  provide a struct for a vanilla .WAV file header

*/

struct __attribute__((packed)) WavFileHeader {
  unsigned char RIFF_tag[4];       // "RIFF"
  uint32_t      RIFF_chunk_size;   // not including the 8 bytes up to the end of this item
  // WAVE subchunk
  unsigned char WAVE_tag[4];       // "WAVE"
  // fmt chunk
  unsigned char fmt_tag[4];        // "fmt "
  uint32_t      fmt_chunk_size;    // 0x10 (not including the 8 bytes up to the end of this item
  uint16_t      format;            // 0x01 = PCM format
  uint16_t      num_channels;      // fill this in
  uint32_t      frames_per_second; // fill this in
  uint32_t      bytes_per_second;  // fill this in
  uint16_t      bytes_per_frame;   // fill this in
  uint16_t      bits_per_sample;   // fill this in
  // data chunk
  unsigned char data_tag[4];       // "data"
  uint32_t      data_chunk_size;   // not including the 8 bytes up to the end of this item

  // default constructor is for streaming a pre-specified number of seconds of audio at given rate, depth, and number of channels 
  WavFileHeader(uint32_t frames_per_second, uint32_t num_channels, uint32_t bits_per_sample, uint32_t num_seconds) :
    RIFF_tag({'R', 'I', 'F', 'F'}),
    RIFF_chunk_size(sizeof(WavFileHeader) - sizeof(RIFF_tag) - sizeof(RIFF_chunk_size) + num_seconds * num_channels * bits_per_sample / 8 * frames_per_second),
    WAVE_tag({'W', 'A', 'V', 'E'}),
    fmt_tag({'f', 'm', 't', ' '}),
    fmt_chunk_size(0x10),
    format(0x01),
    num_channels(num_channels),
    frames_per_second(frames_per_second),
    bits_per_sample(bits_per_sample),
    data_tag({'d', 'a', 't', 'a'}),
    data_chunk_size(num_seconds * num_channels * bits_per_sample / 8 * frames_per_second - sizeof(WavFileHeader))
    {
    };      
    
};

#endif // WAVFILEHEADER_HPP
