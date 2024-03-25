#include <libdjvu/ddjvuapi.h>
#include <iostream>
#include <fstream>
#include "toojpeg.h"
#include <tiffio.h>
#include <cstring>
#include <cstdio>

int zoom;

int currPage;
char* imageBuf;
int row_stride;
std::ofstream file;

ddjvu_context_t* context;
ddjvu_document_t* document;

ddjvu_page_t* page;
ddjvu_rect_t rendRect;
ddjvu_rect_t pageRect;
ddjvu_format_t* format;


void handle_ddjvu_messages(ddjvu_context_t* ctx, int wait)
{
    const ddjvu_message_t* msg;
    if (wait)
        ddjvu_message_wait(ctx);
    while ((msg = ddjvu_message_peek(ctx)))
        ddjvu_message_pop(ctx);
}

void output(unsigned char byte)
{
    file << byte;
}

int main(int argc, char *argv[])
{
    if (argc == 1)
    {
        std::cerr << "usage : DjVu2JPG.exe <djvu filename> <encoding type JPEG|TIFF>" << std::endl;
        return 0;
    }
    context = ddjvu_context_create("eyepiece");
    if (!context)
    {
        std::cerr << "cannot create context\n";
        return 0;
    }

    if (!(document = ddjvu_document_create_by_filename(context, argv[1], 0)))
    {
        std::cerr << "cannot create document\n";
        return 0;
    }

    handle_ddjvu_messages(context, true);
    int pagenum = ddjvu_document_get_pagenum(document);

    for (int i = 0; i < pagenum; i++)
    {
        std::cout << "Processing page #" << i+1 << "(" << pagenum << ")" << std::endl;
        page = ddjvu_page_create_by_pageno(document, i);
        if (!page)
        {
            return 0;
        }

        int resolution = ddjvu_page_get_resolution(page);

        while (!ddjvu_page_decoding_done(page)) {}

        pageRect.w = ddjvu_page_get_width(page);
        pageRect.h = ddjvu_page_get_height(page);

        if (pageRect.w < 1 || pageRect.h < 1)
            return 0;

        pageRect.x = 0;
        pageRect.y = 0;
        pageRect.w = (int)pageRect.w;
        pageRect.h = (int)pageRect.h;
        rendRect = pageRect;

        row_stride = pageRect.w * 3;
        format = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, 0);
        if (!format)
            return 0;

        ddjvu_format_set_row_order(format, 1);

        if (imageBuf) delete imageBuf;
        imageBuf = new char[row_stride * pageRect.h];
        if (!ddjvu_page_render(page, DDJVU_RENDER_COLOR,
            &pageRect,
            &rendRect,
            format,
            row_stride,
            (char*)imageBuf));

        char fn[255];
        if (strcmp(argv[2], "JPEG") == 0)
        {
            sprintf(fn, "page_%d.jpg", i);
            file.open(fn, std::ios_base::out | std::ios_base::binary);
            TooJpeg::writeJpeg(output, imageBuf, pageRect.w, pageRect.h, true, 100);
            file.close();
        }
        if (strcmp(argv[2], "TIFF") == 0)
        {
            sprintf(fn, "C:\\djvu\\page_%d.tif", i);
            TIFF* image = TIFFOpen(fn, "w");
            TIFFSetField(image, TIFFTAG_IMAGEWIDTH, pageRect.w);
            TIFFSetField(image, TIFFTAG_IMAGELENGTH, pageRect.h);
            TIFFSetField(image, TIFFTAG_BITSPERSAMPLE, 8);
            TIFFSetField(image, TIFFTAG_SAMPLESPERPIXEL, 3);
            TIFFSetField(image, TIFFTAG_ROWSPERSTRIP, 24);
            TIFFSetField(image, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
            TIFFSetField(image, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
            TIFFSetField(image, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
            TIFFSetField(image, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
            TIFFSetField(image, TIFFTAG_COMPRESSION, COMPRESSION_JPEG);

            uint32* scan_line = new uint32[pageRect.w];

            for (int i = 0; i < pageRect.h; i++) {

                memcpy(scan_line, &imageBuf[i * row_stride], row_stride);
                TIFFWriteScanline(image, scan_line, i, 0);
            }

            TIFFClose(image);
            free(scan_line);
        }
    }
}
