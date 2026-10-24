// Prove the three libraries work together in this environment before any more
// code gets written against them. Compiled directly with g++ and pkg-config,
// deliberately not through CMake, so a failure here means a library problem
// rather than a build-system problem.

#include <QGuiApplication>
#include <QImageReader>
#include <QByteArray>
#include <QFile>

#include <libraw/libraw.h>
#include <libheif/heif.h>
#include <lcms2.h>

#include <cstdio>
#include <string>

static void heading(const char *text) { std::printf("\n=== %s ===\n", text); }

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    const std::string samples = argc > 1 ? argv[1] : ".";

    heading("versions");
    std::printf("  Qt        %s\n", qVersion());
    std::printf("  LibRaw    %s\n", LibRaw::version());
    std::printf("  libheif   %s\n", heif_get_version());
    std::printf("  lcms2     %d\n", cmsGetEncodedCMMversion());

    heading("formats Qt can read here");
    std::printf("  ");
    for (const QByteArray &f : QImageReader::supportedImageFormats())
        std::printf("%s ", f.constData());
    std::printf("\n");

    heading("Qt decode: sample.jpg");
    {
        QImageReader reader(QString::fromStdString(samples + "/sample.jpg"));
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (image.isNull())
            std::printf("  FAILED: %s\n", qPrintable(reader.errorString()));
        else
            std::printf("  %dx%d, format %s\n", image.width(), image.height(),
                        reader.format().constData());
    }

    heading("Qt decode with EXIF rotation: rotated_exif.jpg (stored 600x400)");
    {
        QImageReader reader(QString::fromStdString(samples + "/rotated_exif.jpg"));
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        std::printf("  %dx%d  %s\n", image.width(), image.height(),
                    image.width() == 400 ? "EXIF applied" : "EXIF IGNORED");
    }

    heading("LibRaw: synthetic.dng");
    {
        LibRaw raw;
        const std::string path = samples + "/synthetic.dng";
        int status = raw.open_file(path.c_str());
        if (status != LIBRAW_SUCCESS) {
            std::printf("  open FAILED: %s\n", LibRaw::strerror(status));
        } else {
            std::printf("  sizes %dx%d  colors %d  pattern %s\n",
                        raw.imgdata.sizes.width, raw.imgdata.sizes.height,
                        raw.imgdata.idata.colors, raw.imgdata.idata.cdesc);

            if (raw.unpack_thumb() == LIBRAW_SUCCESS) {
                int err = 0;
                libraw_processed_image_t *thumb = raw.dcraw_make_mem_thumb(&err);
                if (thumb) {
                    std::printf("  thumbnail %dx%d type %d\n",
                                thumb->width, thumb->height, thumb->type);
                    LibRaw::dcraw_clear_mem(thumb);
                }
            } else {
                std::printf("  no embedded thumbnail\n");
            }

            raw.imgdata.params.use_camera_wb = 1;
            raw.imgdata.params.output_color = 1;
            raw.imgdata.params.output_bps = 8;
            if (raw.unpack() == LIBRAW_SUCCESS && raw.dcraw_process() == LIBRAW_SUCCESS) {
                int err = 0;
                libraw_processed_image_t *full = raw.dcraw_make_mem_image(&err);
                if (full) {
                    std::printf("  demosaiced %dx%d  %d colors  %d bits\n",
                                full->width, full->height, full->colors, full->bits);
                    LibRaw::dcraw_clear_mem(full);
                } else {
                    std::printf("  make_mem_image FAILED: %s\n", LibRaw::strerror(err));
                }
            } else {
                std::printf("  demosaic FAILED\n");
            }
        }
    }

    heading("libheif: sample.heic");
    {
        heif_context *ctx = heif_context_alloc();
        const std::string path = samples + "/sample.heic";
        heif_error err = heif_context_read_from_file(ctx, path.c_str(), nullptr);
        if (err.code != heif_error_Ok) {
            std::printf("  read FAILED: %s\n", err.message);
        } else {
            heif_image_handle *handle = nullptr;
            err = heif_context_get_primary_image_handle(ctx, &handle);
            if (err.code == heif_error_Ok) {
                heif_image *image = nullptr;
                err = heif_decode_image(handle, &image, heif_colorspace_RGB,
                                        heif_chroma_interleaved_RGB, nullptr);
                if (err.code == heif_error_Ok) {
                    std::printf("  decoded %dx%d\n",
                                heif_image_get_width(image, heif_channel_interleaved),
                                heif_image_get_height(image, heif_channel_interleaved));
                    heif_image_release(image);
                } else {
                    std::printf("  decode FAILED: %s\n", err.message);
                }
                heif_image_handle_release(handle);
            }
        }
        heif_context_free(ctx);
    }

    heading("libheif: sample.avif (same library, different codec)");
    {
        heif_context *ctx = heif_context_alloc();
        const std::string path = samples + "/sample.avif";
        heif_error err = heif_context_read_from_file(ctx, path.c_str(), nullptr);
        if (err.code != heif_error_Ok) {
            std::printf("  read FAILED: %s\n", err.message);
        } else {
            heif_image_handle *handle = nullptr;
            if (heif_context_get_primary_image_handle(ctx, &handle).code == heif_error_Ok) {
                heif_image *image = nullptr;
                err = heif_decode_image(handle, &image, heif_colorspace_RGB,
                                        heif_chroma_interleaved_RGB, nullptr);
                if (err.code == heif_error_Ok) {
                    std::printf("  decoded %dx%d\n",
                                heif_image_get_width(image, heif_channel_interleaved),
                                heif_image_get_height(image, heif_channel_interleaved));
                    heif_image_release(image);
                } else {
                    std::printf("  decode FAILED: %s\n", err.message);
                }
                heif_image_handle_release(handle);
            }
        }
        heif_context_free(ctx);
    }

    std::printf("\nprobe finished\n");
    return 0;
}
