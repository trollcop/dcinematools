#include "mxfsource.h"

#define J2K_CFMT 0

static HINSTANCE m_hInstance;

BOOL WINAPI DllMain(HINSTANCE hInstance, ULONG ulReason, LPVOID Reserved)
{
    m_hInstance = hInstance;
    return TRUE;
}

MXFSource::MXFSource(const char *filename, const char *key, const bool hmac, int entrypoint, int eyephase, IScriptEnvironment* env)
    : m_decoder(NULL)
    , o_dinfo(NULL)
    , m_pSample(NULL)
    , m_iFlags(eyephase)
    , m_iEntryPoint(entrypoint)
    , m_FrameBuffer(FRAME_BUFFER_SIZE)
    , m_pMJ2_DecoderNew(NULL)
    , m_pMJ2_DecoderRelease(NULL)
    , m_pMJ2_DecompressBuffer(NULL)
    , m_iDecoderMode(0)
{
    Result_t result = RESULT_OK;

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
    } else {
	// OpenJPEG init
	ZeroMemory(&o_event_mgr, sizeof(o_event_mgr));
	o_event_mgr.error_handler = NULL; // error_callback;
	o_event_mgr.warning_handler = NULL; // warning_callback;
	o_event_mgr.info_handler = NULL; // info_callback;

	/* set decoding parameters to default values */
	opj_set_default_decoder_parameters(&o_parameters);
	o_parameters.decod_format = J2K_CFMT;
	o_dinfo = opj_create_decompress(CODEC_J2K);
	/* catch events using our callbacks and give a local context */
	opj_set_event_mgr((opj_common_ptr)o_dinfo, &o_event_mgr, stderr);
	/* setup the decoder decoding parameters using user parameters */
	opj_setup_decoder(o_dinfo, &o_parameters);
    }
}

MXFSource::~MXFSource()
{
    if (m_decoder)
	m_pMJ2_DecoderRelease(m_decoder, 1);
    if (o_dinfo)
	opj_destroy_decompress(o_dinfo);
}

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

PVideoFrame __stdcall MXFSource::GetFrameOpenJPEG(IScriptEnvironment* env)
{
    VideoInfo vi = GetVideoInfo();
    PVideoFrame pvf = env->NewVideoFrame(vi);

    opj_cio_t *cio = NULL;
    opj_image_t *image = NULL;

    cio = opj_cio_open((opj_common_ptr)o_dinfo, m_FrameBuffer.Data(), m_FrameBuffer.Size());
    image = opj_decode(o_dinfo, cio);
    if (!image) {
	opj_cio_close(cio);
	return pvf;
    }

    int adjustR = 0, adjustG = 0, adjustB = 0;
    adjustR = image->comps[0].prec - 8;
    adjustG = image->comps[1].prec - 8;
    adjustB = image->comps[2].prec - 8;

    int w = image->comps[0].w;	    
    int h = image->comps[0].h;

    BYTE *ptr = pvf->GetWritePtr();
    BYTE *m_ptr = ptr;
    int d_stride = pvf->GetPitch();

    for (int i = 0; i < w * h; i++) {
	unsigned char rc, gc, bc;
	int r, g, b;

	r = image->comps[0].data[w * h - ((i) / (w) + 1) * w + (i) % (w)];
	r += (image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0);
	rc = (unsigned char) ((r >> adjustR)+((r >> (adjustR-1))%2));
	g = image->comps[1].data[w * h - ((i) / (w) + 1) * w + (i) % (w)];
	g += (image->comps[1].sgnd ? 1 << (image->comps[1].prec - 1) : 0);
	gc = (unsigned char) ((g >> adjustG)+((g >> (adjustG-1))%2));
	b = image->comps[2].data[w * h - ((i) / (w) + 1) * w + (i) % (w)];
	b += (image->comps[2].sgnd ? 1 << (image->comps[2].prec - 1) : 0);
	bc = (unsigned char) ((b >> adjustB)+((b >> (adjustB-1))%2));

	BYTE *pixel = ptr + i * 4;
	pixel[0] = bc;
	pixel[1] = gc;
	pixel[2] = rc;
    }

    // todo: XYZ shit

    // free image data structure
    opj_image_destroy(image);

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
	    case 1:
		return GetFrameMainConcept(env);
		break;
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

    int eyephase;
    if (!lstrcmpi(eye, "LEFT")) { eyephase = ASDCP::JP2K::SP_LEFT; }
    else if (!lstrcmpi(eye, "RIGHT")) { eyephase = ASDCP::JP2K::SP_RIGHT; }
    else env->ThrowError("MXFSource: eye must be either Left or Right");

    PClip clip = new MXFSource(filename, key, hmac, entrypoint, eyephase, env);

    return clip;
}

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit2(IScriptEnvironment* env)
{
    env->AddFunction("MXFSource",
	"s+[key]s[hmac]b[entrypoint]i[eye]s",
	Create_MXFSource, 0);
    return "MXFSource";
}
