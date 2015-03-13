#include "mxfsource.h"

#define J2K_CFMT 0

GetSample::GetSample()
    : m_pCtx(NULL)
    , m_pHmac(NULL)
    , m_iEntryPoint(0)
{
    ZeroMemory(&m_AudioDescriptor, sizeof(m_AudioDescriptor));
    ZeroMemory(&m_PictureDescriptor, sizeof(m_PictureDescriptor));
    ZeroMemory(&m_WriterInfo, sizeof(m_WriterInfo));
    ZeroMemory(&m_vi, sizeof(m_vi));
}

GetSample::~GetSample()
{
    if (m_pCtx != NULL)
	delete m_pCtx;
    if (m_pHmac != NULL)
	delete m_pHmac;
}

void GetSample::InitStructures()
{
    if (m_AudioDescriptor.ContainerDuration != 0) {
	// audio track
	int audio_framerate = m_AudioDescriptor.EditRate.Numerator / m_AudioDescriptor.EditRate.Denominator;
	m_vi.audio_samples_per_second = m_AudioDescriptor.AudioSamplingRate.Numerator / m_AudioDescriptor.AudioSamplingRate.Denominator;
	switch (m_AudioDescriptor.QuantizationBits) {
	    case 24:
		m_vi.sample_type = SAMPLE_INT24;
		break;
	}
	m_vi.nchannels = m_AudioDescriptor.ChannelCount;
	m_vi.num_audio_samples = ((m_AudioDescriptor.ContainerDuration - m_iEntryPoint) / audio_framerate ) * m_vi.audio_samples_per_second;
	m_vi.SetFPS(m_AudioDescriptor.EditRate.Numerator, m_AudioDescriptor.EditRate.Denominator);
    } else {
	m_vi.num_frames = m_PictureDescriptor.ContainerDuration - m_iEntryPoint;
	m_vi.width = m_PictureDescriptor.Xsize;
	m_vi.height = m_PictureDescriptor.Ysize;
	m_vi.pixel_type = VideoInfo::CS_BGR32;
	m_vi.SetFPS(m_PictureDescriptor.EditRate.Numerator, m_PictureDescriptor.EditRate.Denominator);
    }
}

Result_t GetSample::SetKey(const char *Key, bool UsesHMAC)
{
    byte_t key_value[KeyLen] = { 0, };
    ui32_t length;
    Kumu::hex2bin(Key, key_value, KeyLen, &length);
    if (length != KeyLen)
	return ASDCP::RESULT_CRYPT_INIT;

    if (m_pCtx == NULL)
	m_pCtx = new AESDecContext();

    Result_t result = m_pCtx->InitKey(key_value);
    if (ASDCP_SUCCESS(result)) {
	if (UsesHMAC && m_WriterInfo.UsesHMAC) {
	    if (m_pHmac == NULL)
		m_pHmac = new HMACContext();

	    return m_pHmac->InitKey(key_value, m_WriterInfo.LabelSetType);
	} else
	    return RESULT_OK;
    }

    return result;
}

GetPCM::GetPCM(const char *filename, int entrypoint, IScriptEnvironment *env)
{
    Result_t result = m_Reader.OpenRead(filename);

    m_iEntryPoint = entrypoint;

    if (ASDCP_SUCCESS(result)) {
	// read various file stats
	m_Reader.FillAudioDescriptor(m_AudioDescriptor);
	m_Reader.FillWriterInfo(m_WriterInfo);
	InitStructures();
    } else {
	env->ThrowError("%s", result.Label());
    }
}

GetSample2D::GetSample2D(const char *filename, int entrypoint, IScriptEnvironment *env)
{
    Result_t result = m_Reader.OpenRead(filename);

    m_iEntryPoint = entrypoint;

    if (ASDCP_SUCCESS(result)) {
	// read various file stats
	m_Reader.FillPictureDescriptor(m_PictureDescriptor);
	m_Reader.FillWriterInfo(m_WriterInfo);
	InitStructures();
    } else {
	env->ThrowError("%s", result.Label());
    }
}

GetSample3D::GetSample3D(const char *filename, int entrypoint, IScriptEnvironment *env)
{
    Result_t result = m_Reader.OpenRead(filename);

    m_iEntryPoint = entrypoint;

    if (ASDCP_SUCCESS(result)) {
	// read various file stats
	m_Reader.FillPictureDescriptor(m_PictureDescriptor);
	m_Reader.FillWriterInfo(m_WriterInfo);
	InitStructures();
    } else {
	env->ThrowError("%s", result.Label());
    }
}

Result_t GetPCM::GetAudio(PCM::FrameBuffer &FrameBuffer, int n)
{
    return m_Reader.ReadFrame(n + m_iEntryPoint, FrameBuffer, m_pCtx, m_pHmac);
}

Result_t GetSample2D::GetFrame(JP2K::FrameBuffer &FrameBuffer, int n, int Flags)
{
    return m_Reader.ReadFrame(n + m_iEntryPoint, FrameBuffer, m_pCtx, m_pHmac);
}

Result_t GetSample3D::GetFrame(JP2K::FrameBuffer &FrameBuffer, int n, int Flags)
{
    if (Flags == ASDCP::JP2K::SP_RIGHT) {
	m_Reader.ReadFrame(n + m_iEntryPoint, ASDCP::JP2K::SP_LEFT, FrameBuffer, m_pCtx, m_pHmac);
	return m_Reader.ReadFrame(n + m_iEntryPoint, ASDCP::JP2K::SP_RIGHT, FrameBuffer, m_pCtx, m_pHmac);
    } else 
	return m_Reader.ReadFrame(n + m_iEntryPoint, ASDCP::JP2K::SP_LEFT, FrameBuffer, m_pCtx, m_pHmac);
}

