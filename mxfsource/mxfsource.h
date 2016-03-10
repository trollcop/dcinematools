#pragma once

#define KM_WIN32
#define ASDCP_PLATFORM "win32"
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_WARNINGS
#define PACKAGE_VERSION "1.5.32"
#define OPJ_STATIC

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#include "avisynth.h"
#include <KM_fileio.h>
#include <KM_prng.h>
#include <AS_DCP.h>
#include <openjpeg.h>

#ifdef USE_MAINCONCEPT
#include <dec_j2k.h>
typedef void* (MC_EXPORT_API *pMJ2_DecoderNew)(mj2_init_params_t *init_params);
typedef void (MC_EXPORT_API *pMJ2_DecoderRelease)(void *instance, uint32_t low_delay);
typedef uint32_t(MC_EXPORT_API *pMJ2_DecompressBuffer)(void                *instance,
    mj2_decode_params_t *params,
    unsigned char       *pb_src,
    uint32_t             src_buf_len,
    unsigned char       *pb_dst,
    uint32_t             fourcc,
    int32_t              stride,
    uint32_t             width,
    uint32_t             height);
#endif // USE_MAINCONCEPT

using namespace ASDCP;

const ui32_t FRAME_BUFFER_SIZE = 4 * Kumu::Megabyte;

extern "C" {

    typedef struct
    {
        OPJ_UINT8* pData; //Our data.
        OPJ_SIZE_T dataSize; //How big is our data.
        OPJ_SIZE_T offset; //Where are we currently in our data.
    } opj_memory_stream;

    opj_stream_t* opj_stream_create_default_memory_stream(opj_memory_stream* p_memoryStream, OPJ_BOOL p_is_read_stream);

}

class GetSample
{
protected:
    // avisynth
    VideoInfo m_vi;

    // crypto-related
    AESDecContext *m_pCtx;
    HMACContext *m_pHmac;

    // various MXF-related data read from the source file on open
    PCM::AudioDescriptor m_AudioDescriptor;
    JP2K::PictureDescriptor m_PictureDescriptor;
    ASDCP::WriterInfo m_WriterInfo;
    int m_iEntryPoint;	// clip delay/offset from start

    // initialize stuff after opening file
    void InitStructures();

public:
    GetSample();
    ~GetSample();

    const VideoInfo& GetVideoInfo() { return m_vi; }

    bool IsAudio() { return m_AudioDescriptor.QuantizationBits != 0; }
    bool EncryptedEssence() { return m_WriterInfo.EncryptedEssence; }


    Result_t SetKey(const char *Key, bool UsesHMAC);
    virtual Result_t GetFrame(JP2K::FrameBuffer &FrameBuffer, int n, int Flags = 0) { return RESULT_FALSE; }
    virtual Result_t GetAudio(PCM::FrameBuffer &FrameBuffer, int n) { return RESULT_FALSE; }
};

class GetPCM : public GetSample
{
private:
    PCM::MXFReader     m_Reader;

public:
    GetPCM(const char *filename, int entrypoint, IScriptEnvironment* env);
    Result_t GetAudio(PCM::FrameBuffer &FrameBuffer, int n);
};

class GetSample3D : public GetSample
{
private:
    JP2K::MXFSReader    m_Reader;

public:
    GetSample3D(const char *filename, int entrypoint, IScriptEnvironment* env);
    Result_t GetFrame(JP2K::FrameBuffer &FrameBuffer, int n, int Flags = 0);
};

class GetSample2D : public GetSample
{
private:
    JP2K::MXFReader    m_Reader;

public:
    GetSample2D(const char *filename, int entrypoint, IScriptEnvironment* env);
    Result_t GetFrame(JP2K::FrameBuffer &FrameBuffer, int n, int Flags = 0);
};

class MXFSource : public IClip
{
private:
    GetSample *m_pSample;
    JP2K::FrameBuffer m_FrameBuffer;

    // MainConcept stuff
    HANDLE m_hMDec;
#ifdef USE_MAINCONCEPT
    pMJ2_DecoderNew m_pMJ2_DecoderNew;
    pMJ2_DecoderRelease m_pMJ2_DecoderRelease;
    pMJ2_DecompressBuffer m_pMJ2_DecompressBuffer;
    mj2_init_params_t m_init_params;
    mj2_decode_params_t m_decode_params;
    void *m_decoder;
    BYTE *m_tmp;
#endif

    // gamma, xyz>rgb conversion
    int xyzgamma[4096];
    int rgbgamma[4096];
    int matrix[3][3];

    int m_iDecoderMode;		// which decoder gets used = 0: openJPEG 1: mainconcept
    int m_iEntryPoint;		// for a/v offset stuff this is the entry point of clip
    int m_iFlags;		// stereo essence shit

public:

    MXFSource(const char *filename, const char *key, const bool hmac, int entrypoint, int eyephase, IScriptEnvironment *env, float ingamma, float outgamma);
    ~MXFSource();

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment *env);
    void __stdcall GetAudio(void *buf, __int64 start, __int64 count, IScriptEnvironment *env);

#ifdef USE_MAINCONCEPT
    PVideoFrame __stdcall GetFrameMainConcept(IScriptEnvironment *env);
#endif
    PVideoFrame __stdcall GetFrameOpenJPEG(IScriptEnvironment *env);
    bool __stdcall GetParity(int n) { return false; }
    void __stdcall SetCacheHints(int cachehints, int frame_range) { };
    const VideoInfo& __stdcall GetVideoInfo() { return m_pSample->GetVideoInfo(); }
};

AVSValue __cdecl Create_MXFSource(AVSValue args, void*, IScriptEnvironment* env);
