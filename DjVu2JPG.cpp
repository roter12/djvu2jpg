#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>

#include <tiffio.h>
#include <libdjvu/ddjvuapi.h>

#include "toojpeg.h"

void handle_ddjvu_messages(ddjvu_context_t *ctx, int wait)
{
    const ddjvu_message_t *msg;
    if (wait)
        ddjvu_message_wait(ctx);

    while ((msg = ddjvu_message_peek(ctx)))
        ddjvu_message_pop(ctx);
}

void callback_ddjvu_output(std::ofstream *file, unsigned char byte)
{
    *file << byte;
}

int count_done = 0;
std::mutex mtx_cound;
std::mutex mtx_print;

inline void finish_write_ddjvu_file(int idx, int pagenum)
{
    mtx_cound.lock();
    count_done++;
    mtx_cound.unlock();

    mtx_print.lock();
    std::cout << "Finished processing page #" << idx + 1 << " (" << count_done << "/" << pagenum << ") " << std::endl;
    mtx_print.unlock();
}

std::vector<int> backlog;
std::mutex mtx_backlog;

int write_ddjvu_file(ddjvu_document_t *document,
                     const unsigned int pagenum,
                     const char *ext)
{
    while (1) 
    {
        if (backlog.size() == 0)
            return 0;

        mtx_backlog.lock();
        int idx = backlog.back();
        backlog.pop_back();
        mtx_backlog.unlock();

        mtx_print.lock();
        std::cout << "Start processing page #" << idx + 1 << " (" << pagenum << ")" << std::endl;
        mtx_print.unlock();

        ddjvu_page_t *page = ddjvu_page_create_by_pageno(document, idx);
        if (!page)
        {
            finish_write_ddjvu_file(idx, pagenum);
            continue;
        }

        int resolution = ddjvu_page_get_resolution(page);
        while (!ddjvu_page_decoding_done(page));

        ddjvu_rect_t pageRect;
        pageRect.w = ddjvu_page_get_width(page);
        pageRect.h = ddjvu_page_get_height(page);
        if (pageRect.w < 1 || pageRect.h < 1)
        {
            finish_write_ddjvu_file(idx, pagenum);
            continue;
        }

        pageRect.x = 0;
        pageRect.y = 0;
        pageRect.w = (int)pageRect.w;
        pageRect.h = (int)pageRect.h;

        ddjvu_rect_t rendRect;
        rendRect = pageRect;
        int row_stride = pageRect.w * 3;
        ddjvu_format_t *format = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, 0);
        if (!format)
        {
            finish_write_ddjvu_file(idx, pagenum);
            continue;
        }

        ddjvu_format_set_row_order(format, 1);

        char *imageBuf = new char[row_stride * pageRect.h];
        if (!ddjvu_page_render(page, DDJVU_RENDER_COLOR,
                            &pageRect,
                            &rendRect,
                            format,
                            row_stride,
                            (char *)imageBuf));

        char fn[255];
        if (strcmp(ext, "JPEG") == 0)
        {
            sprintf(fn, "page_%d.jpg", idx);
            std::ofstream file;
            file.open(fn, std::ios_base::out | std::ios_base::binary);
            TooJpeg::writeJpeg(callback_ddjvu_output, &file, imageBuf, pageRect.w, pageRect.h, true, 100);
            file.close();
        }

        if (strcmp(ext, "TIFF") == 0)
        {
            sprintf(fn, "page_%d.tif", idx);
            TIFF *image = TIFFOpen(fn, "w");
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

            uint32 *scan_line = new uint32[pageRect.w];
            for (int i = 0; i < pageRect.h; i++)
            {
                memcpy(scan_line, &imageBuf[i * row_stride], row_stride);
                TIFFWriteScanline(image, scan_line, i, 0);
            }

            TIFFClose(image);
            free(scan_line);
        }
        delete [] imageBuf;

        finish_write_ddjvu_file(idx, pagenum);
    }    
}

int main(int argc, char *argv[])
{
    if (argc == 1)
    {
        std::cerr << "usage : DjVu2JPG.exe <djvu filename> <encoding type JPEG|TIFF>" << std::endl;
        return 0;
    }

    ddjvu_context_t *context = ddjvu_context_create("eyepiece");
    if (!context)
    {
        std::cerr << "cannot create context" << std::endl;
        return 0;
    }

    ddjvu_document_t *document;
    if (!(document = ddjvu_document_create_by_filename(context, argv[1], 0)))
    {
        std::cerr << "cannot create document" << std::endl;
        return 0;
    }

    handle_ddjvu_messages(context, true);
    int pagenum = ddjvu_document_get_pagenum(document);

    // Prepare backlog
    for (int j = pagenum - 1; j >= 0; j--)
        backlog.push_back(j);

    // Wait for all tasks to finish
    std::vector<std::thread> threads;
    int corenum = std::thread::hardware_concurrency();
    for (int i = 0; i < corenum; i++)
        threads.push_back(std::thread(write_ddjvu_file, document, pagenum, argv[2]));

    for (auto &th : threads)
        th.join();

    std::cerr << std::endl << "Finished successfully!" << std::endl;
}
