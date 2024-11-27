/**
 * @file main.cpp
 * @author arcticwolf666
 * @brief PSDファイルを走査するサンプル
 * @version 0.1
 * @date 2024-05-27
 * 
 * @copyright Copyright (c) arcticwolf666 2024
 * @note Photoshop 2024で日本語でレイヤー名を指定した所ShiftJIS(CP932 ANSI)で保存されている事を確認した。
 * @todo レイヤー名を読むにはまだ幾つかのセクションの読み込みを実装しなくてはならない。
 */
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QDir>
#include <QList>
#include <QStringDecoder>
#include <QImage>
#include <cstddef>

static const quint32 PSDSignature8BPS = 0x38425053u;
static const quint32 PSDSignature8BIM = 0x3842494Du;
static const quint32 PSDSignature8B64 = 0x38623634u;

/*
 * このコードを元に実実装を行うなら
 * 構造体アラインメントの問題からPOD型を使用する必要はないので
 * 各セクションをクラスにしてしまい operator>> で読める様にしてしまう。
 */

struct PSDFileHeaderSection
{
    quint32 signature;
    quint16 version;
    char    reserved[6];
    quint16 channels;
    quint32 height;
    quint32 width;
    quint16 depth;
    quint16 colorMode;
};

struct PSDColorModeDataSection
{
    quint32 length;
    char    colorData[0];
};

struct PSDImageResouceSection
{
    quint32 length;
    char    imageResouces[0];
};

struct PSDLayerAndMaskInfoSection
{
    quint32 length;
    char    layerInfo[0];
};

struct PSDLayerInfo
{
    quint32 length;
    qint16  layerCount;
    char    layerData[0];
};

struct PSDChannelInfo
{
    qint16  channelId;
    quint32 correspondingChannelDataLength;
};

static const quint32 PSDChannelInfosize = 6;

struct PSDLayerRecord
{
    quint32                 top;
    quint32                 left;
    quint32                 bottom;
    quint32                 right;
    quint16                 channels;
    QList<PSDChannelInfo>   channelInfos;
    quint32                 signature;
    quint32                 blendModeKey;
    quint8                  opacity;
    quint8                  clipping;
    quint8                  flags;
    quint8                  filler;
    quint32                 extraDataFieldLength;
};

static const quint32 PSDLayerRecordSize = 34;

struct PSDGlobalLayerMaskInfo
{
    quint32 length;
    quint16 overlayColorSpace; // undocumented.
    quint16 colorComponents[4];
    quint16 opacity; // 0 transparent, 100 opaque.
    quint8  kind; // 0 = Color selected--i.e. inverted; 1 = Color protected;128 = use value stored per layer. This value is preferred. The others are for backward compatibility with beta versions.
    char    filler[0]; // zeros.
};

static const quint32 PSDGlboalLayerMaskInfoDataOffset = 13;

struct PSDAdditionalLayerInfo
{
    quint32 signature; // '8BIM' or '8B64'
    quint32 characterCode;
    quint32 length;
    char    data[0];
};

static const quint32 PSDAdditionalLayerInfoDataOffset = 8;
static const quint32 PSDAdditionalLayerInfoSize = 12;

void dumpPSDFileHeaderSection(const PSDFileHeaderSection& d)
{
    qDebug() << QString("--- PSD File Header Section ---");
    qDebug() << QString("           signature: %1%2%3%4")
        .arg(static_cast<char>((d.signature >> 24) & 0xFF))
        .arg(static_cast<char>((d.signature >> 16) & 0xFF))
        .arg(static_cast<char>((d.signature >>  8) & 0xFF))
        .arg(static_cast<char>((d.signature >>  0) & 0xFF))
        ;
    qDebug() << QString("             version: %1").arg(d.version);
    qDebug() << QString("            channels: %1 with alpha channel.").arg(d.channels);
    qDebug() << QString("              height: %1").arg(d.height);
    qDebug() << QString("               width: %1").arg(d.width);
    qDebug() << QString("               depth: %1").arg(d.depth);
    qDebug() << QString("           colorMode: %1 Bitmap=0 Grayscale=1 Indexed=2 RGB=3 CMYK=4 Multichannel=7 Duotone=8 Lab=9").arg(d.colorMode);
}

void dumpPSDColorModeDataSection(const PSDColorModeDataSection& d)
{
    qDebug() << QString("--- PSD Color Mode Data Section ---");
    qDebug() << QString("              length: %1").arg(d.length);
}

void dumpPSDImageResouceSection(const PSDImageResouceSection& d)
{
    qDebug() << QString("--- PSD Image Resouce Section ---");
    qDebug() << QString("              length: %1").arg(d.length);
}

void dumpPSDLayerAndMaskInfoSection(const PSDLayerAndMaskInfoSection& d)
{
    qDebug() << QString("--- PSD Layer and Mask Info Section ---");
    qDebug() << QString("              length: %1").arg(d.length);
}

void dumpPSDLayerInfo(const PSDLayerInfo& d)
{
    qDebug() << QString("--- PSD Layer Info ---");
    qDebug() << QString("              length: %1").arg(d.length);
    qDebug() << QString("         layer count: %1").arg(d.layerCount);
}

void dumpPSDGlobalLayerMaskInfo(const PSDGlobalLayerMaskInfo& d)
{
    qDebug() << QString("--- PSD Global Layer Mask Info ---");
    qDebug() << QString("              length: %1").arg(d.length);
    if (d.length != 0 )
    {
        qDebug() << QString(" overlay color space: %1").arg(d.overlayColorSpace);
        qDebug() << QString("    color components: %1 %2 %3 %4")
            .arg(d.colorComponents[0])
            .arg(d.colorComponents[1])
            .arg(d.colorComponents[2])
            .arg(d.colorComponents[3])
            ;
        qDebug() << QString("             opacity: %1").arg(d.opacity);
        qDebug() << QString("                kind: %1").arg(d.kind);
    }
}

void dumpPSDAdditionalLayerInfo(const PSDAdditionalLayerInfo& d)
{
    qDebug() << QString("--- PSD Additional Layer Info ---");
    qDebug() << QString("           signature: %1%2%3%4")
        .arg(static_cast<char>((d.signature >> 24) & 0xFF))
        .arg(static_cast<char>((d.signature >> 16) & 0xFF))
        .arg(static_cast<char>((d.signature >>  8) & 0xFF))
        .arg(static_cast<char>((d.signature >>  0) & 0xFF))
        ;
    qDebug() << QString("      character code: %1%2%3%4")
        .arg(static_cast<char>((d.characterCode >> 24) & 0xFF))
        .arg(static_cast<char>((d.characterCode >> 16) & 0xFF))
        .arg(static_cast<char>((d.characterCode >>  8) & 0xFF))
        .arg(static_cast<char>((d.characterCode >>  0) & 0xFF))
        ;
    qDebug() << QString("              length: %1").arg(d.length);
}

void dumpPSDLayerRecord(const PSDLayerRecord& d)
{
    qDebug() << QString("--- PSD Layer Record 1 ---");
    qDebug() << QString("                 top: %1").arg(d.top);
    qDebug() << QString("                left: %1").arg(d.left);
    qDebug() << QString("              bottom: %1").arg(d.bottom);
    qDebug() << QString("               right: %1").arg(d.right);
    qDebug() << QString("            channels: %1").arg(d.channels);
    foreach(const PSDChannelInfo &info, d.channelInfos)
    {
        qDebug() << QString("    PSD Channel Info");
        qDebug() << QString("          channel id: %1").arg(info.channelId);
        qDebug() << QString("         data length: %1").arg(info.correspondingChannelDataLength);
    }
    qDebug() << QString("           signature: %1%2%3%4")
        .arg(static_cast<char>((d.signature >> 24) & 0xFF))
        .arg(static_cast<char>((d.signature >> 16) & 0xFF))
        .arg(static_cast<char>((d.signature >>  8) & 0xFF))
        .arg(static_cast<char>((d.signature >>  0) & 0xFF))
        ;
    qDebug() << QString("      blend mode key: %1%2%3%4")
        .arg(static_cast<char>((d.blendModeKey >> 24) & 0xFF))
        .arg(static_cast<char>((d.blendModeKey >> 16) & 0xFF))
        .arg(static_cast<char>((d.blendModeKey >>  8) & 0xFF))
        .arg(static_cast<char>((d.blendModeKey >>  0) & 0xFF))
        ;
    qDebug() << QString("             opacity: %1").arg(d.opacity);
    qDebug() << QString("            clipping: %1").arg(d.clipping);
    qDebug() << QString("               flags: %1").arg(static_cast<uint>(d.flags), 2, 16, QChar('0'));
    qDebug() << QString("              filler: %1").arg(d.filler);
    qDebug() << QString("        extra length: %1").arg(d.extraDataFieldLength);
}

static QDataStream& operator>>(QDataStream& ds, PSDFileHeaderSection& d)
{
    qDebug() << QString("PSDFileHeaderSection offset: 0x%1").arg(ds.device()->pos(), 0, 16);
    const auto currentEndian = ds.byteOrder();
    ds.setByteOrder(QDataStream::BigEndian);
    ds >> d.signature;
    ds >> d.version;
    ds.readRawData(d.reserved, 6);
    ds >> d.channels;
    ds >> d.height;
    ds >> d.width;
    ds >> d.depth;
    ds >> d.colorMode;
    ds.setByteOrder(currentEndian);
    return ds;
}

static QDataStream& operator>>(QDataStream& ds, PSDColorModeDataSection& d)
{
    qDebug() << QString("PSDColorModeDataSection offset: 0x%1").arg(ds.device()->pos(), 0, 16);
    const auto currentEndian = ds.byteOrder();
    ds.setByteOrder(QDataStream::BigEndian);
    ds >> d.length;
    ds.skipRawData(d.length);
    ds.setByteOrder(currentEndian);
    return ds;
}

static QDataStream& operator>>(QDataStream& ds, PSDImageResouceSection& d)
{
    qDebug() << QString("PSDImageResouceSection offset: 0x%1").arg(ds.device()->pos(), 0, 16);
    const auto currentEndian = ds.byteOrder();
    ds.setByteOrder(QDataStream::BigEndian);
    ds >> d.length;
    ds.skipRawData(d.length);
    ds.setByteOrder(currentEndian);
    return ds;
}

static QDataStream& operator>>(QDataStream& ds, PSDLayerAndMaskInfoSection& d)
{
    qDebug() << QString("PSDLayerAndMaskInfoSection offset: 0x%1").arg(ds.device()->pos(), 0, 16);
    const auto currentEndian = ds.byteOrder();
    ds.setByteOrder(QDataStream::BigEndian);
    ds >> d.length;
    ds.setByteOrder(currentEndian);
    return ds;
}

static QDataStream& operator>>(QDataStream& ds, PSDLayerInfo& d)
{
    qDebug() << QString("PSDLayerInfo offset: 0x%1").arg(ds.device()->pos(), 0, 16);
    const auto currentEndian = ds.byteOrder();
    ds.setByteOrder(QDataStream::BigEndian);
    ds >> d.length;
    ds >> d.layerCount;
    ds.setByteOrder(currentEndian);
    return ds;
}

static QDataStream& operator>>(QDataStream& ds, PSDGlobalLayerMaskInfo& d)
{
    qDebug() << QString("PSDGlobalLayerMaskInfo offset: 0x%1").arg(ds.device()->pos(), 0, 16);
    const auto currentEndian = ds.byteOrder();
    ds.setByteOrder(QDataStream::BigEndian);
    ds >> d.length;
    // https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
    // に記載は無いがPhotoshop 2024で出力したPSDではサイズゼロであった。
    if (d.length != 0 )
    {
        ds >> d.overlayColorSpace;
        ds >> d.colorComponents[0];
        ds >> d.colorComponents[1];
        ds >> d.colorComponents[2];
        ds >> d.colorComponents[3];
        ds >> d.opacity;
        ds >> d.kind;
        ds.skipRawData(d.length - PSDGlboalLayerMaskInfoDataOffset);
    }
    ds.setByteOrder(currentEndian);
    return ds;
}

static QDataStream& operator>>(QDataStream& ds, PSDAdditionalLayerInfo& d)
{
    qDebug() << QString("PSDAdditionalLayerInfo offset: 0x%1").arg(ds.device()->pos(), 0, 16);
    const auto currentEndian = ds.byteOrder();
    ds.setByteOrder(QDataStream::BigEndian);
    ds >> d.signature;
    if ((d.signature != PSDSignature8BIM) && (d.signature != PSDSignature8B64))
        return ds;
    ds >> d.characterCode;
    ds >> d.length;
    ds.setByteOrder(currentEndian);
    return ds;
}

static QDataStream& operator>>(QDataStream& ds, PSDLayerRecord& d)
{
    qDebug() << QString("PSDLayerRecord offset: 0x%1").arg(ds.device()->pos(), 0, 16);
    const auto currentEndian = ds.byteOrder();
    ds.setByteOrder(QDataStream::BigEndian);
    ds >> d.top;
    ds >> d.left;
    ds >> d.bottom;
    ds >> d.right;
    ds >> d.channels;

    for (int i = 0; i < d.channels; i++)
    {
        PSDChannelInfo channelInfo;
        ds >> channelInfo.channelId;
        ds >> channelInfo.correspondingChannelDataLength;
        d.channelInfos.append(channelInfo);
    }

    ds >> d.signature;
    if (d.signature != PSDSignature8BIM)
    {
        qDebug() << QString("PSDLayerRecord invalid signature: %1%2%3%4")
            .arg(static_cast<char>((d.signature >> 24) & 0xFF))
            .arg(static_cast<char>((d.signature >> 16) & 0xFF))
            .arg(static_cast<char>((d.signature >>  8) & 0xFF))
            .arg(static_cast<char>((d.signature >>  0) & 0xFF))
            ;
        return ds;
    }
    ds >> d.blendModeKey;
    ds >> d.opacity;
    ds >> d.clipping;
    ds >> d.flags;
    ds >> d.filler;
    ds >> d.extraDataFieldLength;
    ds.setByteOrder(currentEndian);
    return ds;
}

int scanAdditionalLayerInfo(QFile &file, QDataStream& ds, qint64 remBytes)
{
    while(remBytes > 0)
    {
        if (remBytes < PSDAdditionalLayerInfoDataOffset)
        {
            qDebug() << QString("remainder bytes too small %1").arg(remBytes);
            return -1;
        }
        if (ds.atEnd())
        {
            qDebug() << QString("invalid PSD format, end of file stream was reached while reading additional layer info.");
            return -1;
        }
        qDebug() << QString("remBytes: %1").arg(remBytes);
        PSDAdditionalLayerInfo additionalLayerInfo;
        ds >> additionalLayerInfo;
        if (file.error() != QFileDevice::NoError)
        {
            qDebug() << "file i/o error occurred.";
            return -1;

        }
        if (additionalLayerInfo.signature != PSDSignature8BIM)
        {
            qDebug() << QString("invalid additional layer info signature: %1%2%3%4")
                .arg(static_cast<char>((additionalLayerInfo.signature >> 24) & 0xFF))
                .arg(static_cast<char>((additionalLayerInfo.signature >> 16) & 0xFF))
                .arg(static_cast<char>((additionalLayerInfo.signature >>  8) & 0xFF))
                .arg(static_cast<char>((additionalLayerInfo.signature >>  0) & 0xFF))
                ;
            return -1;
        }
        // https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
        // によれば偶数に丸めると書いてあるが、4バイト境界に合せないとオフセットの計算が合わない。
        const quint32 align = 4;
        const quint32 rem = additionalLayerInfo.length % align;
        const quint32 padding = (rem == 0 ? 0 : align - rem);
        ds.skipRawData(additionalLayerInfo.length + padding);
        remBytes -= additionalLayerInfo.length + padding + PSDAdditionalLayerInfoSize;
        qDebug() << QString("remBytes: %1").arg(remBytes);
        dumpPSDAdditionalLayerInfo(additionalLayerInfo);
    }

    return 0;
}

/**
 * @brief compostite layer channel.
 * 
 * @param img source/destination image.
 * @param bytes raw channel data.
 * @param channel 0=red/1=green/3=blue/-1=alpha
 */
void compoundLayerChannel(QImage &img, const QByteArray &bytes, int channel)
{
    for (int y = 0; y < img.height(); y++)
    {
        for (int x = 0; x < img.width(); x++)
        {
            quint32 pixel = img.pixel(x, y);
            const auto offset = y * img.width() + x;
            if (offset >= bytes.size())
            {
                qDebug() << QString("compoundLayerChannel: source bytes offset out of range");
                return;
            }
            const quint32 subPixel = bytes.at(offset) & 0xFFU;
            switch(channel)
            {
            case -1: // A
                pixel = (pixel & 0x00FFFFFFU) | (subPixel << 24);
                break;
            case 0: // R
                pixel = (pixel & 0xFF00FFFFU) | (subPixel << 16);
                break;
            case 1: // G
                pixel = (pixel & 0xFFFF00FFU) | (subPixel << 8);
                break;
            case 2: // B
                pixel = (pixel & 0xFFFFFF00U) | (subPixel << 0);
                break;
            default:
                qDebug() << QString("unknown channels is passed %1").arg(channel);
                return;
            }
            img.setPixel(x, y, pixel);
        }
    }
}

QByteArray uncompressRLE(int width, int height, const QByteArray &compressed)
{
    qDebug() << QString("uncompressRLE width=%1 height=%2 compression=%3").arg(width).arg(height).arg(compressed.size());

    QDataStream in(compressed);
    in.setByteOrder(QDataStream::BigEndian);

    // read length table.
    QList<quint16> lengthTable(height, 0);
    for (int i = 0; i < height; i++)
    {
        if (in.atEnd())
        {
            qDebug() << QString("can't uncompress RLE, compression source byte too small.");
            return QByteArray();
        }
        in >> lengthTable[i];
    }
    qDebug() << "scanline length table loaded.";

    // uncompress scanlines.
    QByteArray channel(width * height, '\0');
    for (int y = 0; y < height; y++)
    {
        QByteArray scanLine(width, '\0');
        int scanLinePos = 0;
        for (int i = 0; i < lengthTable[y];)
        {
            char code;
            in >> code;
            i++;
            //qDebug() << QString("code %1 i=%2 length=%3").arg(code).arg(i).arg(length);
            if (code < 0)
            {
                // continuous
                int continuousLength = 1 - code;
                if ((continuousLength + scanLinePos) > width) 
                {
                    qDebug() << QString("continuous length too large length=%1 width=%2").arg(continuousLength + scanLinePos).arg(width);
                    return QByteArray();
                }
                char data;
                in >> data;
                i++;
                for (int j = 0; j < continuousLength; j++, scanLinePos++)
                {
                    scanLine[scanLinePos] = data;
                    //qDebug() << QString("C scanLinePos %1 rem %2").arg(scanLinePos).arg(continuousLength - j);
                }
            }
            else
            {
                // discontinuity
                int discontinuousLength = code + 1;
                if ((discontinuousLength + scanLinePos) > width) 
                {
                    qDebug() << QString("discontinuous length too large length=%1 width=%2").arg(discontinuousLength + scanLinePos).arg(width);
                    return QByteArray();
                }
                for (int j = 0; j < discontinuousLength; j++, scanLinePos++)
                {
                    char data;
                    in >> data;
                    i++;
                    scanLine[scanLinePos] = data;
                    //qDebug() << QString("D scanLinePos %1 rem %2").arg(scanLinePos).arg(discontinuousLength - j);
                }
            }
        }
        // transfer scanline.
        for (int x = 0; x < width; x++)
        {
            channel[y * width + x] = scanLine[x];
        }
    }
    qDebug() << "uncompress RLE done.";
    return channel;
}

/**
 * @brief load PSD layer.
 * 
 * @param ds binary data stream.
 * @param record layer record.
 * @param ok set true if load successfully, false failed.
 * @return QImage loaded layer image(channels are compounded).
 */
QImage loadPSDLayer(QDataStream &ds, const PSDLayerRecord &record, bool *ok)
{
    *ok = false;

    const auto currentEndian = ds.byteOrder();
    ds.setByteOrder(QDataStream::BigEndian);

    qsizetype totalSize = 0;

    const int width = record.right - record.left;
    const int height = record.bottom - record.top;
    QImage image(width, height, QImage::Format_ARGB32);
    foreach(const PSDChannelInfo &info, record.channelInfos)
    {
        const auto fileOffset = ds.device()->pos();
        qDebug() << QString("loadPSDLayer file offset %1 length %2").arg(fileOffset, 8, 16, QChar('0')).arg(info.correspondingChannelDataLength);

        quint16 compressionMode;
        ds >> compressionMode;
        if (ds.status() != QDataStream::Ok)
        {
            qDebug() << "loadPSDLayer: bad data stream status.";
            goto compression_error;
        }

        const qsizetype length = info.correspondingChannelDataLength - 2;
        totalSize += info.correspondingChannelDataLength;

        switch(compressionMode)
        {
        case 0: // raw image.
            {
                QByteArray raw(length, '\0');
                ds.readRawData(raw.data(), length);
                compoundLayerChannel(image, raw, info.channelId);
            }
            break;
        case 1: // RLE compressed image.
            {
                QByteArray compressed(length, '\0');
                ds.readRawData(compressed.data(), length);
                QByteArray raw = uncompressRLE(width, height, compressed);
                if (raw.size() != (width * height))
                {
                    qDebug() << QString("uncompressRLE failed. compression length %1").arg(compressed.size());
                    goto compression_error;
                }
                compoundLayerChannel(image, raw, info.channelId);
                qDebug() << QString("RLE compression channel %1 loaded.").arg(info.channelId);
            }
            break;
        case 2:
            qDebug() << QString("ZIP without prediction not supported.");
            goto compression_error;
        case 3:
            qDebug() << QString("ZIP with prediction not supported.");
            goto compression_error;
        default:
            qDebug() << QString("unsupported compression mode %1").arg(compressionMode);
            goto compression_error;
        }
    }

    ds.setByteOrder(currentEndian);
    *ok = true;
    return image;
compression_error:
    ds.setByteOrder(currentEndian);
    *ok = false;
    return QImage();
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "cwd: " << QDir::currentPath();
    if (argc < 1)
    {
        qDebug() << "argument missing, require path to PSD file.";
        return -1;
    }

    QFile file(argv[1]);
    if (!file.open(QIODevice::ReadOnly))
    {
        qDebug() << "failed to open file processed.psd";
        return -1;
    }
    
    QDataStream in(&file);
    in.setByteOrder(QDataStream::BigEndian);

    PSDFileHeaderSection fileHeader;
    in >> fileHeader;
    if (file.error() != QFileDevice::NoError)
        qDebug() << "file i/o error occurred.";
    if (fileHeader.signature != PSDSignature8BPS)
    {
        qDebug() << QString("invalid additional lyer info signature: %1%2%3%4")
            .arg(static_cast<char>((fileHeader.signature >> 24) & 0xFF))
            .arg(static_cast<char>((fileHeader.signature >> 16) & 0xFF))
            .arg(static_cast<char>((fileHeader.signature >>  8) & 0xFF))
            .arg(static_cast<char>((fileHeader.signature >>  0) & 0xFF))
            ;
        return -1;
    }
    if (fileHeader.version != 1)
    {
       qDebug() << QString("PSD file version doesn't match: %1").arg(fileHeader.version); 
       return -1;
    }
    dumpPSDFileHeaderSection(fileHeader);

    PSDColorModeDataSection colorModeDataSection;
    in >> colorModeDataSection;
    if (file.error() != QFileDevice::NoError)
        qDebug() << "file i/o error occurred.";
    dumpPSDColorModeDataSection(colorModeDataSection);

    PSDImageResouceSection imageResouceSection;
    in >> imageResouceSection;
    if (file.error() != QFileDevice::NoError)
        qDebug() << "file i/o error occurred.";
    dumpPSDImageResouceSection(imageResouceSection);

    PSDLayerAndMaskInfoSection layerAndMaskInfoSection;
    in >> layerAndMaskInfoSection;
    if (file.error() != QFileDevice::NoError)
        qDebug() << "file i/o error occurred.";
    dumpPSDLayerAndMaskInfoSection(layerAndMaskInfoSection);

    // layerAndMaskInfoSection.length の内読み込んだかスキップしたバイト数。
    quint32 consumedLayerInfoSize = 0;

    PSDLayerInfo layerInfo;
    in >> layerInfo;
    if (file.error() != QFileDevice::NoError)
        qDebug() << "file i/o error occurred.";
    dumpPSDLayerInfo(layerInfo);
    consumedLayerInfoSize += sizeof(layerInfo.layerCount);

    // layerCountが負の場合最終的に透過したイメージになる事を示す。
    const auto absoluteLayerCount = static_cast<quint16>(std::abs(layerInfo.layerCount));
    qDebug() << QString("absolute layer count: %1").arg(absoluteLayerCount);

    QList<PSDLayerRecord> records;
    for (int layer = 0; layer < absoluteLayerCount; layer++)
    {
        qDebug() << QString("### Layer %1").arg(layer);
        PSDLayerRecord record;
        in >> record;
        if (file.error() != QFileDevice::NoError)
        {
            qDebug() << "file i/o error occurred.";
            return -1;
        }
        dumpPSDLayerRecord(record);
        records.append(record);
        consumedLayerInfoSize += PSDLayerRecordSize + (PSDChannelInfosize * record.channelInfos.size());

        //! @note not implemented, Additional Layer Info を読みユニコードレイヤー名やグループを解析しなければならない。
        consumedLayerInfoSize += record.extraDataFieldLength;
        in.skipRawData(record.extraDataFieldLength);
        if (file.error() != QFileDevice::NoError)
        {
            qDebug() << "file i/o error occurred.";
            return -1;
        }
    }    

    // read image(layer and channels).
    for (int i = 0; i < records.size(); i++)
    {
        const auto &record = records.at(i);
        const int width = record.right - record.left;
        const int height = record.bottom - record.top;
        qDebug() << QString("layer %1 width %2 height %3").arg(i).arg(width).arg(height);
        bool ok;
        QImage image = loadPSDLayer(in, record, &ok);
        if (!ok)
        {
            qDebug() << QString("loadPSDLayer failed, layer record=%1").arg(i);
            return -1;
        }
        QString fileName = QString("layer%1.png").arg(i);
        image.save(fileName, "PNG");
        qDebug() << QString("layer %1 saved to %2").arg(i).arg(fileName);
    }

    quint32 channelImageDataSize = 0;
    bool requirePadding = false;
    foreach(const PSDLayerRecord &record, records)
    {
        foreach(const PSDChannelInfo &info, record.channelInfos)
        {
            channelImageDataSize += info.correspondingChannelDataLength;
        }
    }

    const quint32 align = 2;
    const quint32 rem = channelImageDataSize % align;
    const quint32 padding = (rem == 0 ? 0 : align - rem);
    if (padding)
    {
        qDebug() << QString("total channel data image size is odd value, require padding.");
        in.skipRawData(padding);
    }

    if (layerInfo.length != (consumedLayerInfoSize + channelImageDataSize))
        qInfo() << QString("layerInfo.length missmatch: %1 != %2").arg(layerInfo.length).arg(consumedLayerInfoSize + channelImageDataSize);
    qDebug() << QString("consumed layer info size: %1").arg(consumedLayerInfoSize);
    qDebug() << QString("total channel image data size: %1").arg(channelImageDataSize);

    quint32 layerAndMaskInfoRem = layerAndMaskInfoSection.length - (sizeof(layerAndMaskInfoSection.length) + consumedLayerInfoSize + channelImageDataSize);
    qDebug() << QString("layerAndMaskInfoRem: %1").arg(layerAndMaskInfoRem);

    PSDGlobalLayerMaskInfo globalLayerMaskInfo;
    in >> globalLayerMaskInfo;
    if (file.error() != QFileDevice::NoError)
    {
        qDebug() << "file i/o error occurred.";
        return -1;
    }
    layerAndMaskInfoRem -= globalLayerMaskInfo.length + sizeof(globalLayerMaskInfo.length);
    dumpPSDGlobalLayerMaskInfo(globalLayerMaskInfo);

    if (scanAdditionalLayerInfo(file, in, layerAndMaskInfoRem) != 0)
    {
        qDebug() << "scanAdditionalLayerInfo failed(each file).";
        return -1;
    }

    qDebug() << "PSD file analyze successfully.";
    return 0;
}

void sjisToQStringTest()
{
    QFile sjisFile("shiftjis.txt");
    if (!sjisFile.open(QFile::ReadOnly))
    {
        qDebug() << QString("can't open shiftjis.txt");
        return;
    }
    QByteArray buf = sjisFile.readAll();
    /* 
     * 恐らく QStringDecoder::System にしておけばWindowsにおいては ANSI から UTF16 に変換するので、
     * Photoshop のレイヤー名も実行環境に従ってUTF16に変換される。
     * https://doc.qt.io/qt-6/qstringconverter.html#Encoding-enum
     * Create a converter to or from the underlying encoding of the operating systems locale.
     * This is always assumed to be UTF-8 for Unix based systems.
     * On Windows, this converts to and from the locale code page.
     * Macの記述が無いがSystem7.1からUnicodeを使用している、
     * System6はShift-JISでPhothshopは1990年に発売されたので、
     * Pascal StringでShift-JISを継承しているかもしれない。
     */
    QStringDecoder toUtf16 = QStringDecoder(QStringDecoder::System);
    QString ustr = toUtf16(buf);
    qDebug() << QString("decoded: %1").arg(ustr);
}
