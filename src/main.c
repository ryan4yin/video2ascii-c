#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <libswscale/swscale.h>

#include <libavutil/imgutils.h>
#include <libavutil/frame.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <ncurses.h>
#include <time.h>

#define MAX_FRAMES 100000
#define RESIZE_WIDTH 64
#define RESIZE_HEIGHT 48
#define MAX_FPS 30

// print out the steps and errors
static void logging(const char *fmt, ...);
// open video files, guess it's codec, find and return the video stream's index
static int open_video_stream(char *filename, AVFormatContext *pFormatContext, AVCodec **pCodec, AVCodecParameters **pCodecParameters);
// decode packets into frames
static int decode_packet_and_resize_frames(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *tmpFrame1, AVFrame *tmpFrame2, int resized_width, int resized_height);
static int resize_image(AVCodecContext *codec_context, AVFrame *input_frame, AVFrame *output_frame, int resized_width, int resized_height);

static void print_charimg(unsigned char *buf, int wrap, int xsize, int ysize);

// 用于生成字符画的像素，越往后视觉上越明显。。这是我自己按感觉排的，你可以随意调整。
const char *PIXELS = "  ..,,--''``::!!11+*<>()\\/{}[]abcdefghijklmnopqrstuvwxyz 234567890ABCDEFGHIJKLMNOPQRSTUVWXYZ&@#$";

int main(int argc, const char *argv[])
{
    if (argc < 2)
    {
        printf("You need to specify a media file.\n");
        return -1;
    }

    logging("initializing all the containers, codecs and protocols.");

    // AVFormatContext holds the header information from the format (Container)
    // Allocating memory for this component
    // http://ffmpeg.org/doxygen/trunk/structAVFormatContext.html
    AVFormatContext *pFormatContext = avformat_alloc_context();
    if (!pFormatContext)
    {
        logging("ERROR could not allocate memory for Format Context");
        return -1;
    }

    // the component that knows how to enCOde and DECode the stream
    // it's the codec (audio or video)
    // http://ffmpeg.org/doxygen/trunk/structAVCodec.html
    AVCodec *pCodec = NULL;
    // this component describes the properties of a codec used by the stream i
    // https://ffmpeg.org/doxygen/trunk/structAVCodecParameters.html
    AVCodecParameters *pCodecParameters = NULL;

    char *filename = argv[1];
    int video_stream_index = (open_video_stream(filename, pFormatContext, &pCodec, &pCodecParameters) == -1);
    if (video_stream_index == -1)
    {
        logging("File %s does not contain a video stream!", filename);
        return -1;
    }

    // https://ffmpeg.org/doxygen/trunk/structAVCodecContext.html
    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);
    if (!pCodecContext)
    {
        logging("failed to allocated memory for AVCodecContext");
        return -1;
    }

    // Fill the codec context based on the values from the supplied codec parameters
    // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#gac7b282f51540ca7a99416a3ba6ee0d16
    if (avcodec_parameters_to_context(pCodecContext, pCodecParameters) < 0)
    {
        logging("failed to copy codec params to codec context");
        return -1;
    }

    // Initialize the AVCodecContext to use the given AVCodec.
    // https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#ga11f785a188d7d9df71621001465b0f1d
    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0)
    {
        logging("failed to open codec through avcodec_open2");
        return -1;
    }

    // https://ffmpeg.org/doxygen/trunk/structAVPacket.html
    AVPacket *pPacket = av_packet_alloc();
    if (!pPacket)
    {
        logging("failed to allocate memory for AVPacket");
        return -1;
    }

    // https://ffmpeg.org/doxygen/trunk/structAVFrame.html
    AVFrame *tmpFrame1 = av_frame_alloc();
    if (!tmpFrame1)
    {
        logging("failed to allocate memory for AVFrame");
        return -1;
    }
    AVFrame *tmpFrame2 = av_frame_alloc();
    if (!tmpFrame2)
    {
        logging("failed to allocate memory for AVFrame");
        return -1;
    }

    int response = 0;

    // fill the Packet with data from the Stream
    // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#ga4fdb3084415a82e3810de6ee60e46a61
    for (int count = 0; count < MAX_FRAMES; count++)
    {
        if (av_read_frame(pFormatContext, pPacket) < 0)
        {
            break;
        }

        // if it's the video stream
        if (pPacket->stream_index == video_stream_index)
        {
            // logging("AVPacket->pts %" PRId64, pPacket->pts);
            response = decode_packet_and_resize_frames(pPacket, pCodecContext, tmpFrame1, tmpFrame2, RESIZE_WIDTH, RESIZE_HEIGHT);
            if (response < 0)
                break;
        }
        // https://ffmpeg.org/doxygen/trunk/group__lavc__packet.html#ga63d5a489b419bd5d45cfd09091cbcbc2
        av_packet_unref(pPacket);
    }

    logging("releasing all the resources");
    avformat_close_input(&pFormatContext);
    av_packet_free(&pPacket);
    avcodec_free_context(&pCodecContext);
    av_frame_free(&tmpFrame1);
    av_frame_free(&tmpFrame2);
    return 0;
}

static void logging(const char *fmt, ...)
{
    va_list args;
    fprintf(stderr, "LOG: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static int open_video_stream(char *filename, AVFormatContext *pFormatContext, AVCodec **pCodec, AVCodecParameters **pCodecParameters)
{
    logging("opening the input file (%s) and loading format (container) header", filename);
    // Open the file and read its header. The codecs are not opened.
    // The function arguments are:
    // AVFormatContext (the component we allocated memory for),
    // url (filename),
    // AVInputFormat (if you pass NULL it'll do the auto detect)
    // and AVDictionary (which are options to the demuxer)
    // http://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#ga31d601155e9035d5b0e7efedc894ee49
    if (avformat_open_input(&pFormatContext, filename, NULL, NULL) != 0)
    {
        logging("ERROR could not open the file");
        return -1;
    }

    // now we have access to some information about our file
    // since we read its header we can say what format (container) it's
    // and some other information related to the format itself.
    logging("format %s, duration %lld us, bit_rate %lld", pFormatContext->iformat->name, pFormatContext->duration, pFormatContext->bit_rate);

    logging("finding stream info from format");
    // read Packets from the Format to get stream information
    // this function populates pFormatContext->streams
    // (of size equals to pFormatContext->nb_streams)
    // the arguments are:
    // the AVFormatContext
    // and options contains options for codec corresponding to i-th stream.
    // On return each dictionary will be filled with options that were not found.
    // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#gad42172e27cddafb81096939783b157bb
    if (avformat_find_stream_info(pFormatContext, NULL) < 0)
    {
        logging("ERROR could not get the stream info");
        return -1;
    }

    // loop though all the streams and print its main information
    int video_stream_index = -1;
    for (int i = 0; i < pFormatContext->nb_streams; i++)
    {
        AVCodecParameters *pLocalCodecParameters = NULL;
        pLocalCodecParameters = pFormatContext->streams[i]->codecpar;
        logging("AVStream->time_base before open coded %d/%d", pFormatContext->streams[i]->time_base.num, pFormatContext->streams[i]->time_base.den);
        logging("AVStream->r_frame_rate before open coded %d/%d", pFormatContext->streams[i]->r_frame_rate.num, pFormatContext->streams[i]->r_frame_rate.den);
        logging("AVStream->start_time %" PRId64, pFormatContext->streams[i]->start_time);
        logging("AVStream->duration %" PRId64, pFormatContext->streams[i]->duration);

        logging("finding the proper decoder (CODEC)");

        // finds the registered decoder for a codec ID
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga19a0ca553277f019dd5b0fec6e1f9dca
        const AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (pLocalCodec == NULL)
        {
            logging("ERROR unsupported codec!");
            // In this example if the codec is not found we just skip it
            continue;
        }

        // when the stream is a video we store its index, codec parameters and codec
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if (video_stream_index == -1)
            {
                video_stream_index = i;
                *pCodec = pLocalCodec;
                *pCodecParameters = pLocalCodecParameters;
            }

            logging("Video Codec: resolution %d x %d", pLocalCodecParameters->width, pLocalCodecParameters->height);
        }
        else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            logging("Audio Codec: %d channels, sample rate %d", pLocalCodecParameters->ch_layout.nb_channels, pLocalCodecParameters->sample_rate);
        }

        // print its name, id and bitrate
        logging("\tCodec %s ID %d bit_rate %lld", pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate);
    }

    return video_stream_index;
}

static int decode_packet_and_resize_frames(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *tmpFrame1, AVFrame *tmpFrame2, int resized_width, int resized_height)
{
    // Supply raw packet data as input to a decoder
    // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
    int response = avcodec_send_packet(pCodecContext, pPacket);

    if (response < 0)
    {
        logging("Error while sending a packet to the decoder: %s", av_err2str(response));
        return response;
    }

    for (int i = 0; response >= 0; i++)
    {
        // Return decoded output data (into a frame) from a decoder
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c
        response = avcodec_receive_frame(pCodecContext, tmpFrame1);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF)
        {
            break;
        }
        else if (response < 0)
        {
            logging("Error while receiving a frame from the decoder: %s", av_err2str(response));
            return -1;
        }

        if (response >= 0)
        {
            // logging(
            //     "Frame %d (type=%c, size=%d bytes, format=%d) pts %d key_frame %d [DTS %d]",
            //     pCodecContext->frame_number,
            //     av_get_picture_type_char(tmpFrame1->pict_type),
            //     tmpFrame1->pkt_size,
            //     tmpFrame1->format,
            //     tmpFrame1->pts,
            //     tmpFrame1->key_frame,
            //     tmpFrame1->coded_picture_number);

            // Check if the frame is a planar YUV 4:2:0, 12bpp
            // That is the format of the provided .mp4 file
            // RGB formats will definitely not give a gray image
            // Other YUV image may do so, but untested, so give a warning
            if (tmpFrame1->format != AV_PIX_FMT_YUV420P)
            {
                logging("Warning: the generated file may not be a grayscale image, but could e.g. be just the R component if the video format is RGB");
            }

            // resize image
            int ret = resize_image(pCodecContext, tmpFrame1, tmpFrame2, resized_width, resized_height);
            if (ret < 0)
            {
                logging("failed to resize frame");
                return ret;
            }

            print_charimg(tmpFrame2->data[0], tmpFrame2->linesize[0], tmpFrame2->width, tmpFrame2->height);
        }
    }

    return 0;
}

static int resize_image(AVCodecContext *codec_context, AVFrame *input_frame, AVFrame *output_frame, int resized_width, int resized_height)
{
    int err;
    struct SwsContext *sws_c;

    if (input_frame == NULL || output_frame == NULL)
    {
        fprintf(stderr, "Error allocating resources for input_frame or output_frame\n");
        return -1;
    }

    if (!resized_width)
    {
        resized_width = input_frame->width;
    }

    if (!resized_height)
    {
        resized_height = input_frame->height;
    }

    sws_c = sws_getContext(input_frame->width, input_frame->height, codec_context->pix_fmt, resized_width, resized_height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
    if (sws_c == NULL)
    {
        fprintf(stderr, "Error sws_getContext\n");
        return -1;
    }

    av_image_alloc(output_frame->data, output_frame->linesize, resized_width, resized_height, codec_context->pix_fmt, 32);
    output_frame->width = resized_width;
    output_frame->height = resized_height;
    output_frame->format = AV_PIX_FMT_YUV420P;
    err = sws_scale(sws_c, (const uint8_t *const *)input_frame->data, input_frame->linesize, 0, input_frame->height, output_frame->data, output_frame->linesize);
    // free SwsContext
    sws_freeContext(sws_c);

    if (err < 0)
    {
        fprintf(stderr, "Error sws_scale: %d\n", err);
        return -1;
    }

    return 0;
}

static void print_charimg(unsigned char *buf, int wrap, int xsize, int ysize)
{
    struct timespec ts = {.tv_nsec = 1000000000 / MAX_FPS};
    char newline = '\n';

    initscr();
    // write line by line
    for (int yi = 0; yi < ysize; yi++)
    {
        for (int xi = 0; xi < xsize; xi++)
        {
            // the yi line's xi element
            int index = (int)*(buf + yi * wrap + xi);
            index = index % (strlen(PIXELS) -1);
            // write a char into file
            fprintf(stdout, "%c ", PIXELS[index]);
        }
        fwrite(&newline, 1, 1, stdout);
    }
    nanosleep(&ts, &ts);
    endwin();
}
