// Encode the game's PNG capture with Windows' built-in H.264 Media Foundation
// encoder. No third-party download or installation is required.
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <sstream>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT H,const char* Context)
{
    if(FAILED(H)) { std::ostringstream S; S<<Context<<" failed: 0x"<<std::hex<<H; throw std::runtime_error(S.str()); }
}
#define HR(X) Check((X),#X)
static ComPtr<IWICImagingFactory> Factory;
// NV12 is explicitly top-down. Using it avoids RGB32's bottom-up DIB convention
// being interpreted differently by a sink writer and a source reader.
static std::vector<BYTE> ToNv12(const std::vector<BYTE>& Pixels,UINT W,UINT H)
{
    if((W&1)||(H&1)) throw std::runtime_error("NV12 requires even dimensions");
    std::vector<BYTE> Data(size_t(W)*H*3/2);
    auto Clamp=[](int V){ return BYTE(std::clamp(V,0,255)); };
    for(UINT Y=0;Y<H;++Y) for(UINT X=0;X<W;++X)
    {
        const BYTE* P=Pixels.data()+(size_t(Y)*W+X)*4;
        Data[size_t(Y)*W+X]=Clamp(((66*P[2]+129*P[1]+25*P[0]+128)>>8)+16);
    }
    for(UINT Y=0;Y<H;Y+=2) for(UINT X=0;X<W;X+=2)
    {
        int R=0,G=0,B=0;
        for(UINT DY=0;DY<2;++DY) for(UINT DX=0;DX<2;++DX)
        {
            const BYTE* P=Pixels.data()+(size_t(Y+DY)*W+X+DX)*4;
            B+=P[0]; G+=P[1]; R+=P[2];
        }
        R=(R+2)/4; G=(G+2)/4; B=(B+2)/4;
        const size_t UV=size_t(W)*H+size_t(Y/2)*W+X;
        Data[UV]=Clamp(((-38*R-74*G+112*B+128)>>8)+128);
        Data[UV+1]=Clamp(((112*R-94*G-18*B+128)>>8)+128);
    }
    return Data;
}
static std::vector<BYTE> ReadPng(const std::filesystem::path& Path,UINT& W,UINT& H)
{
    ComPtr<IWICBitmapDecoder> Decoder;
    HR(Factory->CreateDecoderFromFilename(Path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&Decoder));
    ComPtr<IWICBitmapFrameDecode> Frame; HR(Decoder->GetFrame(0,&Frame)); HR(Frame->GetSize(&W,&H));
    ComPtr<IWICFormatConverter> Converter; HR(Factory->CreateFormatConverter(&Converter));
    HR(Converter->Initialize(Frame.Get(),GUID_WICPixelFormat32bppBGR,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    std::vector<BYTE> Data(size_t(W)*H*4); HR(Converter->CopyPixels(nullptr,W*4,UINT(Data.size()),Data.data())); return Data;
}
static void WritePng(const wchar_t* Path,UINT W,UINT H,const std::vector<BYTE>& Data)
{
    ComPtr<IWICBitmap> Bitmap;
    HR(Factory->CreateBitmapFromMemory(W,H,GUID_WICPixelFormat32bppBGR,W*4,UINT(Data.size()),const_cast<BYTE*>(Data.data()),&Bitmap));
    ComPtr<IWICStream> Stream; HR(Factory->CreateStream(&Stream)); HR(Stream->InitializeFromFilename(Path,GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> Encoder; HR(Factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&Encoder)); HR(Encoder->Initialize(Stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> Frame; ComPtr<IPropertyBag2> Bag; HR(Encoder->CreateNewFrame(&Frame,&Bag)); HR(Frame->Initialize(Bag.Get()));
    HR(Frame->SetSize(W,H)); WICPixelFormatGUID Format=GUID_WICPixelFormat32bppBGR; HR(Frame->SetPixelFormat(&Format));
    HR(Frame->WriteSource(Bitmap.Get(),nullptr)); HR(Frame->Commit()); HR(Encoder->Commit());
}
static void Extract(const wchar_t* Input,const wchar_t* Output,double Seconds)
{
    ComPtr<IMFAttributes> Attributes; HR(MFCreateAttributes(&Attributes,2)); HR(Attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE));
    ComPtr<IMFSourceReader> Reader; HR(MFCreateSourceReaderFromURL(Input,Attributes.Get(),&Reader));
    ComPtr<IMFMediaType> Type; HR(MFCreateMediaType(&Type)); HR(Type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video)); HR(Type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_NV12));
    HR(Reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,Type.Get()));
    ComPtr<IMFMediaType> Current; HR(Reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&Current));
    UINT W=0,H=0; HR(MFGetAttributeSize(Current.Get(),MF_MT_FRAME_SIZE,&W,&H));
    UINT32 Matrix=MFVideoTransferMatrix_BT601; Current->GetUINT32(MF_MT_YUV_MATRIX,&Matrix);
    PROPVARIANT Seek; PropVariantInit(&Seek); Seek.vt=VT_I8; Seek.hVal.QuadPart=LONGLONG(Seconds*10000000); HR(Reader->SetCurrentPosition(GUID_NULL,Seek));
    ComPtr<IMFSample> Sample;
    LONGLONG SelectedTime=0;
    for(int I=0;I<1000;++I)
    {
        DWORD Flags=0; HR(Reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&Flags,&SelectedTime,&Sample));
        if(Flags&MF_SOURCE_READERF_ENDOFSTREAM) break;
        if(Sample&&SelectedTime>=Seek.hVal.QuadPart) break;
        Sample.Reset();
    }
    if(!Sample) throw std::runtime_error("No video sample available");
    ComPtr<IMFMediaBuffer> Buffer; HR(Sample->ConvertToContiguousBuffer(&Buffer));
    std::vector<BYTE> Packed;
    ComPtr<IMF2DBuffer> Image;
    if(SUCCEEDED(Buffer.As(&Image)))
    {
        DWORD Length=0; HR(Image->GetContiguousLength(&Length)); Packed.resize(Length);
        HR(Image->ContiguousCopyTo(Packed.data(),Length));
    }
    else
    {
        BYTE* Bytes=nullptr; DWORD Length=0; HR(Buffer->Lock(&Bytes,nullptr,&Length));
        Packed.assign(Bytes,Bytes+Length); HR(Buffer->Unlock());
    }
    UINT32 RawStride=W; Current->GetUINT32(MF_MT_DEFAULT_STRIDE,&RawStride);
    std::wcout<<L"NV12 buffer: "<<W<<L"x"<<H<<L", stride="<<RawStride<<L", bytes="<<Packed.size()<<L"\n";
    // H.264 decoders may allocate 1088 rows for a visible 1080-row image.
    const size_t Pitch=RawStride,StoredHeight=Packed.size()*2/(Pitch*3);
    if(Pitch<W||StoredHeight<H||Pitch*StoredHeight*3/2!=Packed.size())
        throw std::runtime_error("Unexpected NV12 buffer layout");
    std::vector<BYTE> TopDown(size_t(W)*H*4);
    for(UINT Y=0;Y<H;++Y) for(UINT X=0;X<W;++X)
    {
        const int L=int(Packed[size_t(Y)*Pitch+X])-16;
        const size_t UV=Pitch*StoredHeight+size_t(Y/2)*Pitch+(X&~1u);
        const int U=int(Packed[UV])-128,V=int(Packed[UV+1])-128;
        BYTE* P=TopDown.data()+(size_t(Y)*W+X)*4;
        if(Matrix==MFVideoTransferMatrix_BT709)
        {
            P[2]=BYTE(std::clamp((298*L+459*V+128)>>8,0,255));
            P[1]=BYTE(std::clamp((298*L-55*U-136*V+128)>>8,0,255));
            P[0]=BYTE(std::clamp((298*L+541*U+128)>>8,0,255));
        }
        else
        {
            P[2]=BYTE(std::clamp((298*L+409*V+128)>>8,0,255));
            P[1]=BYTE(std::clamp((298*L-100*U-208*V+128)>>8,0,255));
            P[0]=BYTE(std::clamp((298*L+516*U+128)>>8,0,255));
        }
        P[3]=255;
    }
    WritePng(Output,W,H,TopDown);
    std::wcout<<L"Extracted "<<Output<<L" ("<<W<<L"x"<<H<<L") at "<<double(SelectedTime)/10000000<<L" seconds via NV12\n";
}
int wmain(int Argc,wchar_t** Argv)
{
    try
    {
        HR(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); HR(MFStartup(MF_VERSION));
        HR(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&Factory)));
        if(Argc>=4&&std::wstring(Argv[1])==L"--extract") { Extract(Argv[2],Argv[3],Argc>4?_wtof(Argv[4]):0); return 0; }
        if(Argc<3) { std::wcerr<<L"Usage: EncodeCapture <PNG directory> <output.mp4> [fps]\n"; return 2; }
        const UINT FPS=Argc>3?_wtoi(Argv[3]):30;
        std::vector<std::filesystem::path> Files;
        for(const auto& F:std::filesystem::directory_iterator(Argv[1])) if(F.path().extension()==L".png") Files.push_back(F.path());
        std::sort(Files.begin(),Files.end()); if(Files.empty()) throw std::runtime_error("No PNG frames found");
        UINT W=0,H=0; auto First=ReadPng(Files.front(),W,H);
        ComPtr<IMFAttributes> Attributes; HR(MFCreateAttributes(&Attributes,3));
        HR(Attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE));
        HR(Attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,TRUE));
        ComPtr<IMFSinkWriter> Writer; HR(MFCreateSinkWriterFromURL(Argv[2],nullptr,Attributes.Get(),&Writer));
        ComPtr<IMFMediaType> Out; HR(MFCreateMediaType(&Out));
        HR(Out->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video)); HR(Out->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264));
        HR(Out->SetUINT32(MF_MT_AVG_BITRATE,10000000)); HR(Out->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        HR(Out->SetUINT32(MF_MT_MPEG2_PROFILE,eAVEncH264VProfile_Main));
        HR(Out->SetUINT32(MF_MT_YUV_MATRIX,MFVideoTransferMatrix_BT601));
        HR(Out->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_16_235));
        HR(MFSetAttributeSize(Out.Get(),MF_MT_FRAME_SIZE,W,H)); HR(MFSetAttributeRatio(Out.Get(),MF_MT_FRAME_RATE,FPS,1)); HR(MFSetAttributeRatio(Out.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
        DWORD Stream=0; HR(Writer->AddStream(Out.Get(),&Stream));
        ComPtr<IMFMediaType> In; HR(MFCreateMediaType(&In)); HR(In->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video)); HR(In->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_NV12));
        HR(In->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive)); HR(In->SetUINT32(MF_MT_DEFAULT_STRIDE,W));
        HR(In->SetUINT32(MF_MT_YUV_MATRIX,MFVideoTransferMatrix_BT601));
        HR(In->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE,MFNominalRange_16_235));
        HR(MFSetAttributeSize(In.Get(),MF_MT_FRAME_SIZE,W,H)); HR(MFSetAttributeRatio(In.Get(),MF_MT_FRAME_RATE,FPS,1)); HR(MFSetAttributeRatio(In.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
        HR(Writer->SetInputMediaType(Stream,In.Get(),nullptr)); HR(Writer->BeginWriting());
        for(size_t I=0;I<Files.size();++I)
        {
            UINT IW=0,IH=0; auto Pixels=I==0?std::move(First):ReadPng(Files[I],IW,IH);
            if(I>0&&(IW!=W||IH!=H)) throw std::runtime_error("Capture dimensions changed");
            auto Nv12=ToNv12(Pixels,W,H);
            ComPtr<IMFMediaBuffer> Buffer; HR(MFCreateMemoryBuffer(DWORD(Nv12.size()),&Buffer)); BYTE* Destination=nullptr; HR(Buffer->Lock(&Destination,nullptr,nullptr));
            memcpy(Destination,Nv12.data(),Nv12.size());
            HR(Buffer->Unlock()); HR(Buffer->SetCurrentLength(DWORD(Nv12.size())));
            ComPtr<IMFSample> Sample; HR(MFCreateSample(&Sample)); HR(Sample->AddBuffer(Buffer.Get()));
            const LONGLONG Start=LONGLONG(I)*10000000/FPS,End=LONGLONG(I+1)*10000000/FPS;
            HR(Sample->SetSampleTime(Start)); HR(Sample->SetSampleDuration(End-Start)); HR(Writer->WriteSample(Stream,Sample.Get()));
            if(I%100==0) std::wcout<<I<<L" / "<<Files.size()<<L" frames\n";
        }
        HR(Writer->Finalize());
        std::wcout<<L"Encoded "<<Files.size()<<L" frames, "<<double(Files.size())/FPS<<L" seconds, "<<W<<L"x"<<H<<L" to "<<Argv[2]<<L"\n";
        return 0;
    }
    catch(const std::exception& E) { std::cerr<<E.what()<<"\n"; return 1; }
}
