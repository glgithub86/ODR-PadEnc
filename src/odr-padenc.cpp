/*
    Copyright (C) 2014 CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/)

    Copyright (C) 2014, 2015 Matthias P. Braendli (http://opendigitalradio.org)

    Copyright (C) 2015, 2016, 2017, 2018 Stefan Pöschel (http://opendigitalradio.org)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*!
    \file odr-padenc.cpp
    \brief Generate PAD data for MOT Slideshow and DLS

    \author Sergio Sagliocco <sergio.sagliocco@csp.it>
    \author Matthias P. Braendli <matthias@mpb.li>
    \author Stefan Pöschel <odr@basicmaster.de>
*/

#include "odr-padenc.h"


static PadEncoder *pad_encoder = NULL;

static void break_handler(int) {
    fprintf(stderr, "...ODR-PadEnc exits...\n");
    if(pad_encoder)
        pad_encoder->DoExit();
}

static void header() {
    fprintf(stderr, "ODR-PadEnc %s - DAB PAD encoder for MOT Slideshow and DLS\n\n"
                    "By CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/) and\n"
                    "Opendigitalradio.org\n\n"
                    "Reads image data from the specified directory, DLS text from a file,\n"
                    "and outputs PAD data to the given FIFO.\n"
                    "  https://opendigitalradio.org\n\n",
#if defined(GITVERSION)
                    GITVERSION
#else
                    PACKAGE_VERSION
#endif
                    );
}

static void usage(const char* name) {
    PadEncoderOptions options_default;
    fprintf(stderr, "Usage: %s [OPTIONS...]\n", name);
    fprintf(stderr, " -d, --dir=DIRNAME         Directory to read images from.\n"
                    " -e, --erase               Erase slides from DIRNAME once they have\n"
                    "                             been encoded.\n"
                    " -s, --sleep=DUR           Wait DUR seconds between each slide\n"
                    "                             Default: %d\n"
                    " -o, --output=FILENAME     FIFO to write PAD data into.\n"
                    "                             Default: %s\n"
                    " -t, --dls=FILENAME        FIFO or file to read DLS text from.\n"
                    "                             If specified more than once, use next file after slide switch (for uniform PAD encoder, -l is used instead).\n"
                    " -p, --pad=LENGTH          Set the PAD length in bytes.\n"
                    "                             Possible values: %s\n"
                    "                             Default: %zu\n"
                    " -c, --charset=ID          ID of the character set encoding used for DLS text input.\n"
                    "                             ID =  0: Complete EBU Latin based repertoire\n"
                    "                             ID =  6: ISO/IEC 10646 using UCS-2 BE\n"
                    "                             ID = 15: ISO/IEC 10646 using UTF-8\n"
                    "                             Default: 15\n"
                    " -r, --remove-dls          Always insert a DLS Remove Label command when replacing a DLS text.\n"
                    " -C, --raw-dls             Do not convert DLS texts to Complete EBU Latin based repertoire\n"
                    "                             character set encoding.\n"
                    " -m, --max-slide-size=SIZE Recompress slide if above the specified maximum size in bytes.\n"
                    "                             Default: %zu (Simple Profile)\n"
                    " -R, --raw-slides          Do not process slides. Integrity checks and resizing\n"
                    "                             slides is skipped. Use this if you know what you are doing !\n"
                    "                             It is useful only when -d is used\n"
                    " -v, --verbose             Print more information to the console\n"
                    "\n"
                    "Parameters for uniform PAD encoder only:\n"
                    " -f, --frame-dur=DUR       Enable the uniform PAD encoder and set the duration of one frame/AU in milliseconds.\n"
                    " -l, --label=DUR           Wait DUR seconds between each label (if more than one file used)\n"
                    "                             Default: %d\n"
                    " -L, --label-ins=DUR       Insert label every DUR milliseconds\n"
                    "                             Default: %d\n"
                    " -i, --init-burst=COUNT    Sets a PAD burst amount to initially fill the output FIFO\n"
                    "                             Default: %d\n",
                    options_default.slide_interval,
                    options_default.output,
                    PADPacketizer::ALLOWED_PADLEN.c_str(),
                    options_default.padlen,
                    options_default.max_slide_size,
                    options_default.label_interval,
                    options_default.label_insertion,
                    options_default.init_burst
           );
}


static std::string list_dls_files(std::vector<std::string> dls_files) {
    std::string result = "";
    for(std::string dls_file : dls_files) {
        if(!result.empty())
            result += "/";
        result += "'" + dls_file + "'";
    }
    return result;
}


int main(int argc, char *argv[]) {
    header();

    // get/check options
    PadEncoderOptions options;

    const struct option longopts[] = {
        {"charset",         required_argument,  0, 'c'},
        {"raw-dls",         no_argument,        0, 'C'},
        {"remove-dls",      no_argument,        0, 'r'},
        {"dir",             required_argument,  0, 'd'},
        {"erase",           no_argument,        0, 'e'},
        {"output",          required_argument,  0, 'o'},
        {"dls",             required_argument,  0, 't'},
        {"pad",             required_argument,  0, 'p'},
        {"sleep",           required_argument,  0, 's'},
        {"max-slide-size",  required_argument,  0, 'm'},
        {"raw-slides",      no_argument,        0, 'R'},
        {"help",            no_argument,        0, 'h'},
        {"frame-dur",       required_argument,  0, 'f'},
        {"label",           required_argument,  0, 'l'},
        {"label-ins",       required_argument,  0, 'L'},
        {"init-burst",      required_argument,  0, 'i'},
        {"verbose",         no_argument,        0, 'v'},
        {0,0,0,0},
    };

    int ch;
    while((ch = getopt_long(argc, argv, "eChRrc:d:o:p:s:t:f:l:L:i:vm:", longopts, NULL)) != -1) {
        switch (ch) {
            case 'c':
                options.dl_params.charset = (DABCharset) atoi(optarg);
                break;
            case 'C':
                options.dl_params.raw_dls = true;
                break;
            case 'r':
                options.dl_params.remove_dls = true;
                break;
            case 'd':
                options.sls_dir = optarg;
                break;
            case 'e':
                options.erase_after_tx = true;
                break;
            case 'o':
                options.output = optarg;
                break;
            case 's':
                options.slide_interval = atoi(optarg);
                break;
            case 't':   // can be used more than once!
                options.dls_files.push_back(optarg);
                break;
            case 'p':
                options.padlen = atoi(optarg);
                break;
            case 'm':
                options.max_slide_size = atoi(optarg);
                break;
            case 'R':
                options.raw_slides = true;
                break;
            case 'f':
                options.frame_dur = atoi(optarg);
                break;
            case 'l':
                options.label_interval = atoi(optarg);
                break;
            case 'L':
                options.label_insertion = atoi(optarg);
                break;
            case 'i':
                options.init_burst = atoi(optarg);
                break;
            case 'v':
                verbose++;
                break;
            case '?':
            case 'h':
                usage(argv[0]);
                return 0;
        }
    }

    if (!PADPacketizer::CheckPADLen(options.padlen)) {
        fprintf(stderr, "ODR-PadEnc Error: PAD length %zu invalid: Possible values: %s\n",
                options.padlen, PADPacketizer::ALLOWED_PADLEN.c_str());
        return 2;
    }
    if (options.max_slide_size > SLSEncoder::MAXSLIDESIZE_SIMPLE) {
        fprintf(stderr, "ODR-PadEnc Error: max slide size %zu exceeds Simple Profile limit %zu\n",
                options.max_slide_size, SLSEncoder::MAXSLIDESIZE_SIMPLE);
        return 2;
    }

    if (options.sls_dir && not options.dls_files.empty()) {
        fprintf(stderr, "ODR-PadEnc encoding Slideshow from '%s' and DLS from %s to '%s' (PAD length: %zu)\n",
                options.sls_dir, list_dls_files(options.dls_files).c_str(), options.output, options.padlen);
    }
    else if (options.sls_dir) {
        fprintf(stderr, "ODR-PadEnc encoding Slideshow from '%s' to '%s' (PAD length: %zu). No DLS.\n",
                options.sls_dir, options.output, options.padlen);
    }
    else if (not options.dls_files.empty()) {
        fprintf(stderr, "ODR-PadEnc encoding DLS from %s to '%s' (PAD length: %zu). No Slideshow.\n",
                list_dls_files(options.dls_files).c_str(), options.output, options.padlen);
    }
    else {
        fprintf(stderr, "ODR-PadEnc Error: Neither DLS nor Slideshow to encode !\n");
        usage(argv[0]);
        return 1;
    }

    const char* user_charset;
    switch (options.dl_params.charset) {
        case DABCharset::COMPLETE_EBU_LATIN:
            user_charset = "Complete EBU Latin";
            break;
        case DABCharset::EBU_LATIN_CY_GR:
            user_charset = "EBU Latin core, Cyrillic, Greek";
            break;
        case DABCharset::EBU_LATIN_AR_HE_CY_GR:
            user_charset = "EBU Latin core, Arabic, Hebrew, Cyrillic, Greek";
            break;
        case DABCharset::ISO_LATIN_ALPHABET_2:
            user_charset = "ISO Latin Alphabet 2";
            break;
        case DABCharset::UCS2_BE:
            user_charset = "UCS-2 BE";
            break;
        case DABCharset::UTF8:
            user_charset = "UTF-8";
            break;
        default:
            fprintf(stderr, "ODR-PadEnc Error: Invalid charset!\n");
            usage(argv[0]);
            return 1;
    }

    fprintf(stderr, "ODR-PadEnc using charset %s (%d)\n",
           user_charset, (int) options.dl_params.charset);

    if (not options.dl_params.raw_dls) {
        switch (options.dl_params.charset) {
        case DABCharset::COMPLETE_EBU_LATIN:
            // no conversion needed
            break;
        case DABCharset::UTF8:
            fprintf(stderr, "ODR-PadEnc converting DLS texts to Complete EBU Latin\n");
            break;
        default:
            fprintf(stderr, "ODR-PadEnc Error: DLS conversion to EBU is currently only supported for UTF-8 input!\n");
            return 1;
        }
    }


    // TODO: check uniform PAD encoder options!?


    // invoke selected encoder
    if (options.frame_dur) {
        fprintf(stderr, "ODR-PadEnc using uniform PAD encoder\n");
        pad_encoder = new UniformPadEncoder(options);
    } else {
        fprintf(stderr, "ODR-PadEnc using burst PAD encoder\n");
        pad_encoder = new BurstPadEncoder(options);
    }
    int result = pad_encoder->Main();
    delete pad_encoder;

    return result;
}


// --- PadEncoder -----------------------------------------------------------------
int PadEncoder::Main() {
    output_fd = open(options.output, O_WRONLY);
    if (output_fd == -1) {
        perror("ODR-PadEnc Error: failed to open output");
        return 3;
    }

    // check for FIFO
    struct stat fifo_stat;
    if (fstat(output_fd, &fifo_stat)) {
        perror("ODR-PadEnc Error: could not retrieve output file stat");
        return 1;
    }
    if ((fifo_stat.st_mode & S_IFMT) != S_IFIFO) {
        fprintf(stderr, "ODR-PadEnc Error: the output file must be a FIFO!\n");
        return 3;
    }

#if HAVE_MAGICKWAND
    MagickWandGenesis();
    if (verbose)
        fprintf(stderr, "ODR-PadEnc using ImageMagick version '%s'\n", GetMagickVersion(NULL));
#endif

    // handle signals
    if (signal(SIGINT, break_handler) == SIG_ERR) {
        perror("ODR-PadEnc Error: could not set SIGINT handler");
        return 1;
    }
    if (signal(SIGTERM, break_handler) == SIG_ERR) {
        perror("ODR-PadEnc Error: could not set SIGTERM handler");
        return 1;
    }
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("ODR-PadEnc Error: could not set SIGPIPE to be ignored");
        return 1;
    }

    // invoke actual encoder
    int result = 0;
    while (!do_exit) {
        result = Encode();

        // abort on error
        if (result)
            break;

        // sleep until next run
        std::this_thread::sleep_until(run_timeline);
    }

    // cleanup
    if (close(output_fd)) {
        perror("ODR-PadEnc Error: failed to close output");
        return 1;
    }

#if HAVE_MAGICKWAND
    MagickWandTerminus();
#endif

    return result;
}

int PadEncoder::EncodeSlide(bool skip_if_already_queued) {
    // skip insertion, if desired and previous one not yet finished
    if (skip_if_already_queued && pad_packetizer.QueueContainsDG(SLSEncoder::APPTYPE_MOT_START)) {
        fprintf(stderr, "ODR-PadEnc Warning: skipping slide insertion, as previous one still in transmission!\n");
        return 0;
    }

    // check for slides dir re-read request
    std::string request_reread_path = std::string(options.sls_dir) + "/" + SLSEncoder::REQUEST_REREAD_FILENAME;
    struct stat request_reread_stat;
    if (stat(request_reread_path.c_str(), &request_reread_stat)) {
        // ignore missing request file
        if (errno != ENOENT) {
            perror("ODR-PadEnc Error: could not retrieve slides dir re-read request file stat");
            return 1;
        }
    } else {
        // handle request
        fprintf(stderr, "ODR-PadEnc received slides dir re-read request!\n");
        if (unlink(request_reread_path.c_str()))
            perror(("ODR-PadEnc Error: erasing file '" + request_reread_path +"' failed").c_str());

        slides.Clear();
    }

    // usually invoked once
    for (;;) {
        // try to read slides dir (if present)
        if (slides.Empty()) {
            if (!slides.InitFromDir(options.sls_dir))
                return 1;
            slides_success = false;
        }

        // if slides available, encode the first one
        if (!slides.Empty()) {
            slide_metadata_t slide = slides.GetSlide();

            if (sls_encoder.encodeSlide(slide.filepath, slide.fidx, options.raw_slides, options.max_slide_size)) {
                slides_success = true;
                if (options.erase_after_tx) {
                    if (unlink(slide.filepath.c_str()))
                        perror(("ODR-PadEnc Error: erasing file '" + slide.filepath +"' failed").c_str());
                }
            } else {
                /* skip to next slide, except this is the last slide and so far
                 * no slide worked, to prevent an infinite loop and because
                 * re-reading the slides dir just moments later won't result in
                 * a different amount of slides. */
                bool skipping = !(slides.Empty() && !slides_success);
                fprintf(stderr, "ODR-PadEnc Error: cannot encode file '%s'; %s\n", slide.filepath.c_str(), skipping ? "skipping" : "giving up for now");
                if (skipping)
                    continue;
            }
        }

        break;
    }

    return 0;
}

int PadEncoder::EncodeLabel(bool skip_if_already_queued) {
    // skip insertion, if desired and previous one not yet finished
    if (skip_if_already_queued && pad_packetizer.QueueContainsDG(DLSEncoder::APPTYPE_START)) {
        fprintf(stderr, "ODR-PadEnc Warning: skipping label insertion, as previous one still in transmission!\n");
        return 0;
    }

    dls_encoder.encodeLabel(options.dls_files[curr_dls_file], options.dl_params);

    return 0;
}


// --- BurstPadEncoder -----------------------------------------------------------------
const int BurstPadEncoder::DLS_REPETITION_WHILE_SLS = 50; // PADs

int BurstPadEncoder::Encode() {
    int result = 0;

    // encode SLS
    if (options.SLSEnabled()) {
        result = EncodeSlide(false);
        if (result)
            return result;

        // while flushing, insert DLS (if present) after a certain PAD amout
        while (pad_packetizer.QueueFilled()) {
            if (options.DLSEnabled()) {
                result = EncodeLabel(false);
                if (result)
                    return result;
            }

            pad_packetizer.WriteAllPADs(output_fd, DLS_REPETITION_WHILE_SLS);
        }
    }

    // encode (a last) DLS (if present)
    if (options.DLSEnabled()) {
        result = EncodeLabel(false);
        if (result)
            return result;

        // switch to next DLS file
        curr_dls_file = (curr_dls_file + 1) % options.dls_files.size();
    }

    // flush all remaining PADs
    pad_packetizer.WriteAllPADs(output_fd);

    // schedule next run at next slide interval
    run_timeline += std::chrono::seconds(options.slide_interval);

    return 0;
}


// --- UniformPadEncoder -----------------------------------------------------------------
UniformPadEncoder::UniformPadEncoder(PadEncoderOptions options) : PadEncoder(options) {
    // PAD related timelines
    pad_timeline = steady_clock::now();
    next_slide = pad_timeline;
    next_label = pad_timeline;
    next_label_insertion = pad_timeline;

    // consider initial burst
    run_timeline -= std::chrono::milliseconds(options.init_burst * options.frame_dur);

    // if multiple DLS files, ensure that initial increment leads to first one
    if (options.dls_files.size() > 1)
        curr_dls_file = -1;
}

int UniformPadEncoder::Encode() {
    int result = 0;

    // handle SLS
    if (options.SLSEnabled()) {
        if (options.slide_interval > 0) {
            // encode slides regularly
            if (pad_timeline >= next_slide) {
                result = EncodeSlide(true);
                next_slide += std::chrono::seconds(options.slide_interval);
            }
        } else {
            // encode slide as soon as previous slide has been transmitted
            if (!pad_packetizer.QueueContainsDG(SLSEncoder::APPTYPE_MOT_START))
                result = EncodeSlide(true);
        }
    }
    if (result)
        return result;

    // handle DLS
    if (options.DLSEnabled()) {
        if (options.dls_files.size() > 1 && pad_timeline >= next_label) {
            // switch to next DLS file
            curr_dls_file = (curr_dls_file + 1) % options.dls_files.size();
            next_label += std::chrono::seconds(options.label_interval);

            // enforce label insertion
            next_label_insertion = pad_timeline;
        }

        if (pad_timeline >= next_label_insertion) {
            // encode label
            result = EncodeLabel(true);
            next_label_insertion += std::chrono::milliseconds(options.label_insertion);
        }
    }
    if (result)
        return result;

    // flush one PAD
    pad_packetizer.WriteAllPADs(output_fd, 1, true);
    pad_timeline += std::chrono::milliseconds(options.frame_dur);

    // schedule next run at next frame/AU
    run_timeline += std::chrono::milliseconds(options.frame_dur);

    return 0;
}
