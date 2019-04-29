// Mythtv
#include "mythmainwindow.h"
#include "mythlogging.h"
#include "mythcodecid.h"
#include "mythframe.h"
#include "avformatdecoder.h"
#include "mythrender_opengl.h"
#include "videobuffers.h"
#include "mythvtbinterop.h"
#include "mythhwcontext.h"
#include "mythvtbcontext.h"

extern "C" {
#include "libavutil/hwcontext_videotoolbox.h"
#include "libavutil/pixdesc.h"
}

#define LOC QString("VTBDec: ")

MythVTBContext::MythVTBContext(MythCodecID CodecID)
  : MythCodecContext(),
    m_codecID(CodecID)
{
}

int MythVTBContext::HwDecoderInit(AVCodecContext *Context)
{
    int res = -1;
    if (codec_is_vtb_dec(m_codecID))
    {
        AVBufferRef *device = nullptr;
        res = av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
        if (res < 0)
        {
            LOG(VB_GENERAL, LOG_ERR, LOC + QString("Failed to create hw device type '%1'")
                .arg(av_hwdevice_get_type_name(AV_HWDEVICE_TYPE_VIDEOTOOLBOX)));
            return res;
        }

        LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Created hw device '%1' (decode only)")
                .arg(av_hwdevice_get_type_name(AV_HWDEVICE_TYPE_VIDEOTOOLBOX)));
        AVHWDeviceContext* devicectx = reinterpret_cast<AVHWDeviceContext*>(device->data);
        devicectx->free        = MythHWContext::DeviceContextFinished;
        Context->hw_device_ctx = device;
        return res;
    }
    else if (codec_is_vtb(m_codecID))
    {
        return MythHWContext::InitialiseDecoder2(Context, MythVTBContext::InitialiseDecoder, "Create VTB decoder");
    }

    return -1;
}

bool MythVTBContext::CheckDecoderSupport(uint StreamType, AVCodec **Codec)
{
    static bool initialised = false;
    static QVector<uint> supported;
    if (!initialised)
    {
        initialised = true;
        if (__builtin_available(macOS 10.13, *))
        {
            QStringList debug;
            if (VTIsHardwareDecodeSupported(kCMVideoCodecType_MPEG1Video)) { supported.append(1); debug << "MPEG1"; }
            if (VTIsHardwareDecodeSupported(kCMVideoCodecType_MPEG2Video)) { supported.append(2); debug << "MPEG2"; }
            if (VTIsHardwareDecodeSupported(kCMVideoCodecType_H263))       { supported.append(3); debug << "H.263"; }
            if (VTIsHardwareDecodeSupported(kCMVideoCodecType_MPEG4Video)) { supported.append(4); debug << "MPEG4"; }
            if (VTIsHardwareDecodeSupported(kCMVideoCodecType_H264))       { supported.append(5); debug << "H.264"; }
            if (VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC))       { supported.append(10); debug << "HEVC"; }
            LOG(VB_GENERAL, LOG_INFO, LOC + QString("Supported VideoToolBox codecs: %1").arg(debug.join(",")));
        }
        else
        {
            LOG(VB_GENERAL, LOG_INFO, LOC + "Unable to check hardware decode support. Assuming all.");
            supported.append(1); supported.append(2); supported.append(3);
            supported.append(4); supported.append(5); supported.append(10);
        }
    }

    if (!supported.contains(StreamType))
    {
        LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("HW device type '%1' does not support decoding '%2'")
                .arg(av_hwdevice_get_type_name(AV_HWDEVICE_TYPE_VIDEOTOOLBOX)).arg((*Codec)->name));
        return false;
    }

    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("HW device type '%1' supports decoding '%2'")
            .arg(av_hwdevice_get_type_name(AV_HWDEVICE_TYPE_VIDEOTOOLBOX)).arg((*Codec)->name));
    return true;
}

MythCodecID MythVTBContext::GetSupportedCodec(AVCodecContext *,
                                              AVCodec **Codec,
                                              const QString &Decoder,
                                              uint StreamType,
                                              AVPixelFormat &PixFmt)
{
    bool decodeonly = Decoder == "vtb-dec"; 
    MythCodecID success = static_cast<MythCodecID>((decodeonly ? kCodec_MPEG1_VTB_DEC : kCodec_MPEG1_VTB) + (StreamType - 1));
    MythCodecID failure = static_cast<MythCodecID>(kCodec_MPEG1 + (StreamType - 1));

    if ((Decoder != "vtb") && (Decoder != "vtb-dec"))
        return failure;

    // Check decoder support
    if (!CheckDecoderSupport(StreamType, Codec))
        return failure;

    PixFmt = AV_PIX_FMT_VIDEOTOOLBOX;
    return success;
}

int MythVTBContext::InitialiseDecoder(AVCodecContext *Context)
{
    if (!gCoreContext->IsUIThread())
        return -1;

    // Retrieve OpenGL render context
    MythRenderOpenGL* render = MythRenderOpenGL::GetOpenGLRender();
    if (!render)
        return -1;

    // Lock
    OpenGLLocker locker(render);

    MythCodecID vtbid = static_cast<MythCodecID>(kCodec_MPEG1_VTB + (mpeg_version(Context->codec_id) - 1));
    MythVTBInterop::Type type = MythVTBInterop::GetInteropType(vtbid, render);
    if (type == MythOpenGLInterop::Unsupported)
        return -1;

    // Allocate the device context
    AVBufferRef* deviceref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VIDEOTOOLBOX);
    if (!deviceref)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to create device context");
        return -1;
    }
    
    // Add our interop class and set the callback for its release
    AVHWDeviceContext* devicectx = reinterpret_cast<AVHWDeviceContext*>(deviceref->data);
    devicectx->user_opaque = MythVTBInterop::Create(render, type);
    devicectx->free        = MythHWContext::DeviceContextFinished;

    // Create
    if (av_hwdevice_ctx_init(deviceref) < 0)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to initialise device context");
        av_buffer_unref(&deviceref);
        return -1;
    }

    Context->hw_device_ctx = deviceref;
    LOG(VB_PLAYBACK, LOG_INFO, LOC + QString("Created hw device '%1'")
        .arg(av_hwdevice_get_type_name(AV_HWDEVICE_TYPE_VIDEOTOOLBOX)));
    return 0;
}

enum AVPixelFormat MythVTBContext::GetFormat(struct AVCodecContext*, const enum AVPixelFormat *PixFmt)
{
    enum AVPixelFormat ret = AV_PIX_FMT_NONE;
    while (*PixFmt != AV_PIX_FMT_NONE)
    {
        if (*PixFmt == AV_PIX_FMT_VIDEOTOOLBOX)
            return *PixFmt;
        PixFmt++;
    }
    return ret;
}