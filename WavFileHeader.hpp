/*
  Header and header-filler for .WAV files
  FIXME: currently hard-coded to S16_LE sample format
*/

#ifndef WAVFILEHEADER_HPP
#define WAVFILEHEADER_HPP

#include <string.h>

struct WavFileHeader {

protected:
  struct {
    char RIFFlabel[4];
    uint32_t remFileSize;
    char WAVElabel[4];
    char FMTlabel[4];
    uint32_t remFmtSize;
    uint16_t fmtCode;
    uint16_t numChan;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t frameSize;
    uint16_t sampleSize;
    char DATAlabel[4];
    uint32_t remDataSize;
  } __attribute__((packed)) hdrBuf;

public:

  const static int BITS_PER_SAMPLE_S16_LE = 16;
  const static int SAMPLE_FMT_CODE_PCM_S16_LE = 1;

  WavFileHeader(int rate, int channels, uint32_t frames, int bitsPerSample = BITS_PER_SAMPLE_S16_LE, int fmtCode = SAMPLE_FMT_CODE_PCM_S16_LE)
  {
    uint32_t bytes = channels * bitsPerSample / 8 * frames;

    memcpy(hdrBuf.RIFFlabel, "RIFF", 4);
    hdrBuf.remFileSize = bytes + 36;
    memcpy(hdrBuf.WAVElabel, "WAVE", 4);
    memcpy(hdrBuf.FMTlabel, "fmt ", 4);
    hdrBuf.remFmtSize = bitsPerSample;
    hdrBuf.fmtCode = fmtCode;
    hdrBuf.numChan = channels;
    hdrBuf.sampleRate = rate;
    hdrBuf.byteRate = rate * channels * bitsPerSample / 8;
    hdrBuf.frameSize = channels * 2;
    hdrBuf.sampleSize = bitsPerSample;
    memcpy(hdrBuf.DATAlabel, "data", 4);
    hdrBuf.remDataSize = bytes;
  };

  char * address() { return (char*) & hdrBuf;};
  size_t size() { return sizeof(hdrBuf);};
};

#endif // WAVFILEHEADER_HPP
