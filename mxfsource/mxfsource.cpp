#include "mxfsource.h"

#define J2K_CFMT 0

static HINSTANCE m_hInstance;

BOOL WINAPI DllMain(HINSTANCE hInstance, ULONG ulReason, LPVOID Reserved)
{
    m_hInstance = hInstance;
    return TRUE;
}

static void error_callback(const char *msg, void *client_data)
{
    OutputDebugStringA(msg);
}


MXFSource::MXFSource(const char *filename, const char *key, const bool hmac, int entrypoint, int eyephase, IScriptEnvironment* env, float ingamma, float outgamma)
    : m_pSample(NULL)
    , m_iFlags(eyephase)
    , m_iEntryPoint(entrypoint)
    , m_FrameBuffer(FRAME_BUFFER_SIZE)
#ifdef USE_MAINCONCEPT
    , m_decoder(NULL)
    , m_pMJ2_DecoderNew(NULL)
    , m_pMJ2_DecoderRelease(NULL)
    , m_pMJ2_DecompressBuffer(NULL)
#endif
    , m_iDecoderMode(0)
{
    Result_t result = RESULT_OK;
    int i;

    try {
        m_pSample = new GetPCM(filename, entrypoint, env);
    } catch (...) {
	try {
	    m_pSample = new GetSample2D(filename, entrypoint, env);
	} catch (...) {
    	    try {
		m_pSample = new GetSample3D(filename, entrypoint, env);
	    } catch (AvisynthError e) {
		env->ThrowError("Cannot open source file: %s", e.msg);
	    }
	}
    }

    if (m_pSample->EncryptedEssence()) {
	result = m_pSample->SetKey(key, hmac);
	if (ASDCP_FAILURE(result))
	    env->ThrowError("Cannot set encryption key: %s", result.Label());
    }

    // no need to initialize jpeg decoders for audio
    if (m_pSample->IsAudio())
	return;

    // MainConcept init
    TCHAR pszPath[MAX_PATH];
    GetModuleFileName(m_hInstance, pszPath, MAX_PATH);
    TCHAR *whence = strrchr(pszPath, '\\');
    whence++;
    strcpy(whence, _T("dec_j2k.dll"));

    // try to load mainconcept dll
    m_hMDec = LoadLibrary(pszPath);
    if (m_hMDec) {
#ifdef USE_MAINCONCEPT
        m_pMJ2_DecoderNew = (pMJ2_DecoderNew)GetProcAddress((HMODULE)m_hMDec, _T("MJ2_DecoderNew"));
	m_pMJ2_DecoderRelease = (pMJ2_DecoderRelease)GetProcAddress((HMODULE)m_hMDec, _T("MJ2_DecoderRelease"));
	m_pMJ2_DecompressBuffer = (pMJ2_DecompressBuffer)GetProcAddress((HMODULE)m_hMDec, _T("MJ2_DecompressBuffer"));

	ZeroMemory(&m_init_params, sizeof(mj2_init_params_t));
	ZeroMemory(&m_decode_params, sizeof(mj2_decode_params_t));
	m_init_params.low_delay_mode = 1;
	m_init_params.num_threads = 8;
	m_decode_params.acceleration = 1;
	m_decode_params.skip_passes = 3;
	m_decode_params.trans_xyz = 1;
	m_decoder = m_pMJ2_DecoderNew(&m_init_params);
	m_tmp = new BYTE[FRAME_BUFFER_SIZE * 4];
	m_iDecoderMode = 1;
#endif
    } else {
        // precalculate in/out gamma values
        for (i = 0; i < 4096; i++) {
            xyzgamma[i] = (int)(pow(i / 4095.0, ingamma) * 4095.0 + 0.5);
            rgbgamma[i] = (int)(pow(i / 4095.0, outgamma) * 4095.0 + 0.5);
        }

        // precalculate xyz>rgb conversion matrix
        matrix[0][0] = (int)(3.2404542 * 4095.0 + 0.5);
        matrix[0][1] = (int)(-1.5371385 * 4095.0 - 0.5);
        matrix[0][2] = (int)(-0.4985314 * 4095.0 - 0.5);
        matrix[1][0] = (int)(-0.9692660 * 4095.0 - 0.5);
        matrix[1][1] = (int)(1.8760108 * 4095.0 + 0.5);
        matrix[1][2] = (int)(0.0415560 * 4095.0 + 0.5);
        matrix[2][0] = (int)(0.0556434 * 4095.0 + 0.5);
        matrix[2][1] = (int)(-0.2040259 * 4095.0 - 0.5);
        matrix[2][2] = (int)(1.0572252  * 4095.0 + 0.5);
    }
}

MXFSource::~MXFSource()
{
#ifdef USE_MAINCONCEPT
    if (m_decoder)
	m_pMJ2_DecoderRelease(m_decoder, 1);
#endif
}

#ifdef USE_MAINCONCEPT
PVideoFrame __stdcall MXFSource::GetFrameMainConcept(IScriptEnvironment* env)
{
    VideoInfo vi = GetVideoInfo();
    PVideoFrame pvf = env->NewVideoFrame(vi);

    m_pMJ2_DecompressBuffer(m_decoder, &m_decode_params, m_FrameBuffer.Data(), m_FrameBuffer.Size(), m_tmp, FOURCC_BGR4, vi.width * 4, vi.width, vi.height);
    BYTE *ptr = pvf->GetWritePtr();
    int d_stride = pvf->GetPitch();
    int s_stride = vi.width * 4;
    BYTE *m_ptr_end = m_tmp + ((vi.height - 1) * s_stride);

    for (int i = 0; i < vi.height; i++, ptr += d_stride, m_ptr_end -= s_stride)
	RtlCopyMemory(ptr, m_ptr_end, s_stride);

    return pvf;
}
#endif

PVideoFrame __stdcall MXFSource::GetFrameOpenJPEG(IScriptEnvironment* env)
{
    VideoInfo vi = GetVideoInfo();
    PVideoFrame pvf = env->NewVideoFrame(vi);

    opj_codec_t *codec = NULL;
    opj_memory_stream ms;
    opj_stream_t *stream = NULL;
    opj_image_t *image = NULL;
    opj_dparameters_t o_parameters;	/* decompression parameters */
    OPJ_BOOL rv;

    ms.pData = m_FrameBuffer.Data();
    ms.dataSize = m_FrameBuffer.Size();
    ms.offset = 0;
    stream = opj_stream_create_default_memory_stream(&ms, OPJ_TRUE);

    // initialize decoder
    opj_set_default_decoder_parameters(&o_parameters);
    o_parameters.decod_format = J2K_CFMT;
    codec = opj_create_decompress(OPJ_CODEC_J2K);

    // catch events using our callbacks
    opj_set_info_handler(codec, NULL, 0);
    opj_set_warning_handler(codec, NULL, 0);
    opj_set_error_handler(codec, error_callback, 0);

    // setup the decoder decoding parameters using user parameters
    opj_setup_decoder(codec, &o_parameters);

    if (!opj_read_header(stream, codec, &image)) {
        opj_stream_destroy(stream);
        opj_image_destroy(image);
        if (codec)
            opj_destroy_codec(codec);
        return pvf;
    }

    rv = opj_decode(codec, stream, image);
    if (!rv) {
        opj_stream_destroy(stream);
        if (codec)
            opj_destroy_codec(codec);
        return pvf;
    }
    // end decompression (which does nothing, fucking opensauce LOL)
    opj_end_decompress(codec, stream);

    int w = image->comps[0].w;	    
    int h = image->comps[0].h;

    BYTE *ptr = pvf->GetWritePtr();
    BYTE *m_ptr = ptr;
    int d_stride = pvf->GetPitch();
    int j = 0;

    for (int i = 0; i < d_stride / 4 * h; i++) {
        if (i % (d_stride / 4) < w) {
            int r, g, b;
            int x, y, z;

            x = image->comps[0].data[w * h - ((j) / (w)+1) * w + (j) % (w)];
            y = image->comps[1].data[w * h - ((j) / (w)+1) * w + (j) % (w)];
            z = image->comps[2].data[w * h - ((j) / (w)+1) * w + (j) % (w)];

            // apply input gamma
            x = xyzgamma[x];
            y = xyzgamma[y];
            z = xyzgamma[z];

            // xyz -> rgb matrix
            r = (matrix[0][0] * x + matrix[0][1] * y + matrix[0][2] * z) >> 12;
            g = (matrix[1][0] * x + matrix[1][1] * y + matrix[1][2] * z) >> 12;
            b = (matrix[2][0] * x + matrix[2][1] * y + matrix[2][2] * z) >> 12;

            // clipping
            if (r > 4095) r = 4095; if (r < 0) r = 0;
            if (g > 4095) g = 4095; if (g < 0) g = 0;
            if (b > 4095) b = 4095; if (b < 0) b = 0;

            // output gamma + bits
            r = rgbgamma[r] >> 4; // /16 12bit->8bit
            g = rgbgamma[g] >> 4;
            b = rgbgamma[b] >> 4;

            BYTE *pixel = ptr + i * 4;
            pixel[0] = (BYTE)b;
            pixel[1] = (BYTE)g;
            pixel[2] = (BYTE)r;

            j++;
        } else {
            BYTE *pixel = ptr + i * 4;
            pixel[0] = 0;
            pixel[1] = 0;
            pixel[2] = 0;
        }
    }

    // free image data structure
    opj_image_destroy(image);
    opj_stream_destroy(stream);
    if (codec)
        opj_destroy_codec(codec);

    return pvf;
}

PVideoFrame MXFSource::GetFrame(int n, IScriptEnvironment* env)
{
    Result_t result = m_pSample->GetFrame(m_FrameBuffer, n, m_iFlags);
    if (ASDCP_SUCCESS(result)) {
	switch (m_iDecoderMode) {
	    case 0:
		return GetFrameOpenJPEG(env);
		break;
#ifdef USE_MAINCONCEPT
            case 1:
		return GetFrameMainConcept(env);
		break;
#endif
        }
    } else {
	if (result == RESULT_CHECKFAIL)
	    env->ThrowError("Check value did not decrypt correctly. Check key passed to MXFSource()");
	else
	    return env->NewVideoFrame(GetVideoInfo());
	    // env->ThrowError("Error getting frame: %s", result.Label());
    }
    return NULL;
}

void MXFSource::GetAudio(void *buf, __int64 start, __int64 count, IScriptEnvironment *env)
{
    PCM::FrameBuffer pcm(FRAME_BUFFER_SIZE);
    VideoInfo vi = GetVideoInfo();

    ui32_t samples_per_frame = vi.audio_samples_per_second / (vi.fps_numerator / vi.fps_denominator);
    ui32_t fstart = (ui32_t)(start / samples_per_frame);
    ui32_t fend = (ui32_t)((start / samples_per_frame) + (count / samples_per_frame));

    byte_t *ptr = (byte_t *)buf;
    for (ui32_t n = fstart; n < fend; n++) {
	Result_t result = m_pSample->GetAudio(pcm, n);
	if (result == RESULT_CHECKFAIL)
	    env->ThrowError("Check value for audio did not decrypt correctly. Check key passed to MXFSource()");
	RtlCopyMemory(ptr, pcm.RoData(), pcm.Size());
	ptr += pcm.Size();
    }
}

AVSValue __cdecl Create_MXFSource(AVSValue args, void*, IScriptEnvironment* env) {

    if (args[0].ArraySize() != 1)
	env->ThrowError("MXFSource: Only 1 filename currently supported!");

    const char* filename = args[0][0].AsString();
    const char* key = args[1].AsString("");
    const bool hmac = args[2].AsBool(false);
    const int entrypoint = args[3].AsInt(0);
    const char* eye = args[4].AsString("LEFT");
    const float ingamma = (float)args[5].AsFloat(2.6f);
    const float outgamma = (float)args[6].AsFloat(1.0f / 2.22222222f);

    int eyephase;
    if (!lstrcmpi(eye, "LEFT")) { eyephase = ASDCP::JP2K::SP_LEFT; }
    else if (!lstrcmpi(eye, "RIGHT")) { eyephase = ASDCP::JP2K::SP_RIGHT; }
    else env->ThrowError("MXFSource: eye must be either Left or Right");

    PClip clip = new MXFSource(filename, key, hmac, entrypoint, eyephase, env, ingamma, outgamma);

    return clip;
}

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit2(IScriptEnvironment* env)
{
    env->AddFunction("MXFSource",
        "s+[key]s[hmac]b[entrypoint]i[eye]s[ingamma]f[outgamma]f",
	Create_MXFSource, 0);
    return "MXFSource";
}
