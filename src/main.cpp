//
//  main.cpp
//  rleduce
//
//  Created by Andrew Simmonds on 19/05/21.
//

#include <algorithm>
#include <filesystem>
#include <iostream>
#include "libGraphite/data/reader.hpp"
#include "libGraphite/data/writer.hpp"
#include "libGraphite/quickdraw/pict.hpp"
#include "libGraphite/quickdraw/rle.hpp"
#include "libGraphite/rsrc/file.hpp"
using namespace graphite;

static struct options {
    bool trim = false;
    bool picts = false;
    bool reduce = false;
    bool encode = false;
    bool dither = true;
    bool verbose = false;
} options;

enum rleop: uint8_t {
    eof = 0x00,
    line_start = 0x01,
    pixel_data = 0x02,
    transparent_run = 0x03,
    pixel_run = 0x04,
};

typedef struct Spin {
    int16_t spriteID;
    int16_t maskID;
    qd::size frame;
    qd::size grid;

    Spin(std::shared_ptr<rsrc::resource> resource) {
        auto reader = data::reader(resource->data());
        spriteID = reader.read_short();
        maskID = reader.read_short();
        frame = qd::size::read(reader, qd::size::pict);
        grid = qd::size::read(reader, qd::size::pict);
    }
} Spin;

typedef struct Shan {
    int16_t baseSpriteID;
    int16_t baseMaskID;
    int16_t baseSetCount;
    qd::size baseFrame;
    int16_t altSpriteID;
    int16_t altMaskID;
    int16_t altSetCount;
    qd::size altFrame;
    int16_t engineSpriteID;
    int16_t engineMaskID;
    qd::size engineFrame;
    int16_t lightSpriteID;
    int16_t lightMaskID;
    qd::size lightFrame;
    int16_t weaponSpriteID;
    int16_t weaponMaskID;
    qd::size weaponFrame;
    int16_t framesPer;
    int16_t shieldSpriteID;
    int16_t shieldMaskID;
    qd::size shieldFrame;

    Shan(std::shared_ptr<rsrc::resource> resource) {
        auto reader = data::reader(resource->data());
        baseSpriteID = reader.read_short();
        baseMaskID = reader.read_short();
        baseSetCount = reader.read_short();
        baseFrame = qd::size::read(reader, qd::size::pict);
        reader.move(2);

        altSpriteID = reader.read_short();
        altMaskID = reader.read_short();
        altSetCount = reader.read_short();
        altFrame = qd::size::read(reader, qd::size::pict);

        engineSpriteID = reader.read_short();
        engineMaskID = reader.read_short();
        engineFrame = qd::size::read(reader, qd::size::pict);

        lightSpriteID = reader.read_short();
        lightMaskID = reader.read_short();
        lightFrame = qd::size::read(reader, qd::size::pict);

        weaponSpriteID = reader.read_short();
        weaponMaskID = reader.read_short();
        weaponFrame = qd::size::read(reader, qd::size::pict);

        reader.move(6);
        framesPer = reader.read_short();
        reader.move(10);

        shieldSpriteID = reader.read_short();
        shieldMaskID = reader.read_short();
        shieldFrame = qd::size::read(reader, qd::size::pict);
    }
} Shan;

int64_t processRle(std::shared_ptr<rsrc::resource> resource) {
    auto reader = data::reader(resource->data());
    auto width = reader.read_short();
    auto height = reader.read_short();
    reader.move(4);
    auto frames = reader.read_short();
    reader.move(6);
    
    rleop opcode;
    int32_t op;
    int32_t count;
    int trim = 0;
    // Figure out how many lines can be trimmed from top/bottom of the whole sprite
    if (options.trim) {
        trim = height/2;
        for (int i=0; i<frames; i++) {
            int line = 0;
            int top = height;
            int bottom = 0;
            while (true) {
                op = reader.read_long();
                opcode = static_cast<rleop>(op >> 24);
                if (opcode == line_start) {
                    count = op & 0x00FFFFFF;
                    if (count != 0) {
                        reader.move(count);
                        if (top > line) {
                            top = line;
                        }
                        bottom = line+1;
                    }
                    line++;
                } else {
                    break;
                }
            }
            if (top < trim) {
                trim = top;
            }
            bottom = height-bottom;
            if (bottom < trim) {
                trim = bottom;
            }
        }
    }
    
    auto writer = data::writer();
    writer.write_short(width);
    auto newHeight = height - (trim * 2);
    writer.write_short(newHeight);
    reader.set_position(4);
    writer.write_data(reader.read_data(12));
    for (int i=0; i<frames; i++) {
        int skip = trim;
        int blank = 0;
        while (true) {
            op = reader.read_long();
            opcode = static_cast<rleop>(op >> 24);
            if (opcode == line_start) {
                if (skip-- > 0) {
                    continue;
                }
                count = op & 0x00FFFFFF;
                if (count != 0) {
                    for (int j=0; j<blank; j++) {
                        writer.write_long(rleop::line_start << 24);
                    }
                    writer.write_long(op);
                    writer.write_data(reader.read_data(count));
                    blank = 0;
                } else {
                    blank++;
                }
            } else {
                writer.write_long(0);
                break;
            }
        }
    }
    
    auto size = resource->data()->size();
    auto data = writer.data();
    auto diff = size - data->size();
    if (options.verbose) {
        double pc = diff * 100.0 / size;
        std::string result = diff > 0 ? "Written" : "Not written";
        printf("%7lld  %6d  %6d  %8ld  %10d  %8ld  %5.1f%%  %s\n",
               resource->id(), frames, height, size, newHeight, data->size(), pc, result.c_str());
    }
    if (diff > 0) {
        resource->set_data(data);
        return diff;
    }
    return 0;
}

void applyError(std::shared_ptr<qd::surface> surface, int x, int y, int errors[3], bool up) {
    auto color = surface->at(x, y);
    int add = up ? 1 : 0;
    int comps[3] = { color.red_component(), color.green_component(), color.blue_component() };
    for (int i=0; i<3; i++) {
        comps[i] = std::clamp(comps[i] + ((errors[i] + add) / 2), 0, 255);
    }
    surface->set(x, y, qd::color(comps[0], comps[1], comps[2], color.alpha_component()));
}

void rgb555dither(std::shared_ptr<qd::surface> surface) {
    // QuickDraw dithering algorithm.
    // Half the error is diffused right on even rows, left on odd rows. The remainder is diffused down.
    auto frameWidth = surface->size().width();
    auto frameHeight = surface->size().height();
    for (int y=0; y<frameHeight; y++) {
        bool even = y % 2 == 0;
        for (int w=0; w<frameWidth; w++) {
            int x = even ? w : frameWidth - w - 1;
            auto color = surface->at(x, y);
            auto newColor = qd::color(color.rgb555());
            int errors[3] = {
                color.red_component() - newColor.red_component(),
                color.green_component() - newColor.green_component(),
                color.blue_component() - newColor.blue_component()
            };
            if (errors[0] || errors[1] || errors[2]) {
                surface->set(x, y, newColor);
                if (even && x+1 < frameWidth) {
                    applyError(surface, x+1, y, errors, false);
                } else if (!even && x > 0) {
                    applyError(surface, x-1, y, errors, false);
                }
                if (y+1 < frameHeight) {
                    applyError(surface, x, y+1, errors, true);
                }
            }
        }
    }
}

std::string fourCC(uint32_t code) {
    std::string str;
    str.push_back(code >> 24);
    str.push_back(code >> 16);
    str.push_back(code >> 8);
    str.push_back(code);
    return str;
}

int64_t processPict(std::shared_ptr<rsrc::resource> resource) {
    qd::pict pict(resource->data());
    auto format = pict.format();
    // Don't dither low depth images
    if (options.reduce && options.dither && format > 4 && format != 16) {
        rgb555dither(pict.image_surface().lock());
    }
    auto size = resource->data()->size();
    auto data = pict.data(options.reduce || format == 16);
    int64_t diff = size - data->size();
    // Force write if format is non-standard (QuickTime) or reduction occurred
    bool save = diff > 0 || format > 32 || (options.reduce && format != 16);
    if (options.verbose) {
        std::string inFormat = format > 32 ? fourCC(format) : std::to_string(format)+"-bit";
        std::string outFormat = pict.format() > 32 ? fourCC(pict.format()) : std::to_string(pict.format())+"-bit";
        double pc = diff * 100.0 / size;
        std::string result = save ? (diff > 0 ? "Written" : "Written (forced)") : "Not written";
        printf("%7lld  %-6s  %8ld  %-8s  %8ld  %5.1f%%  %s\n",
               resource->id(), inFormat.c_str(), size, outFormat.c_str(), data->size(), pc, result.c_str());
    }
    if (save) {
        resource->set_data(data);
        return diff;
    }
    return 0;
}

bool enRle(std::shared_ptr<rsrc::resource> resource, rsrc::file& file, int16_t spriteID, int16_t maskID, qd::size frame) {
    if (spriteID <= 0 || maskID <= 0) {
        return false;
    }
    auto spriteRes = file.find("PICT", spriteID, {}).lock();
    auto maskRes = file.find("PICT", maskID, {}).lock();
    if (spriteRes == nullptr || maskRes == nullptr) {
        return false;
    }

    if (frame.width() <= 0 || frame.height() <= 0) {
        std::cerr << "Invalid frame size in " << resource->type_code() << " " << resource->id() << "." << std::endl;
        return false;
    }

    auto spritePict = qd::pict(spriteRes->data());
    auto sprite = spritePict.image_surface().lock();
    auto spriteX = sprite->size().width();
    auto spriteY = sprite->size().height();
    if (spriteX % frame.width() != 0 || spriteY % frame.height() != 0) {
        std::cerr << "Sprite PICT " << spriteID << " for " << resource->type_code() << " " << resource->id() << " does not match frame size." << std::endl;
        return false;
    }
    auto maskPict = qd::pict(maskRes->data());
    auto mask = maskPict.image_surface().lock();
    if (mask->size().width() != spriteX || mask->size().height() != spriteY) {
        std::cerr << "Mask PICT " << maskID << " for " << resource->type_code() << " " << resource->id() << " does not match sprite size." << std::endl;
        return false;
    }

    if (options.dither && spritePict.format() != 16) {
        rgb555dither(sprite);
    }

    // Apply the mask
    auto black = qd::color::black();
    for (int y=0; y<spriteY; y++) {
        for (int x=0; x<spriteX; x++) {
            if (mask->at(x, y) == black) {
                sprite->set(x, y, {0, 0, 0, 0});
            }
        }
    }

    auto rle = qd::rle(sprite, frame);
    auto data = rle.data();
    if (options.verbose) {
        auto sSize = spriteRes->data()->size();
        auto mSize = maskRes->data()->size();
        printf("%7lld  %7d  %6d  %6d  %6d  %11ld  %9ld  %9ld\n",
               resource->id(), spriteID, rle.frame_count(), frame.width(), frame.height(), sSize, mSize, data->size());
    }
    file.add_resource("rlëD", spriteID, spriteRes->name(), data);

    // Remove the PICTs
    spriteRes->remove();
    maskRes->remove();

    return true;
}

bool processSpin(std::shared_ptr<rsrc::resource> resource, rsrc::file& file) {
    auto spin = Spin(resource);
    return enRle(resource, file, spin.spriteID, spin.maskID, spin.frame);
}

int processShan(std::shared_ptr<rsrc::resource> resource, rsrc::file& file) {
    auto shan = Shan(resource);
    int encoded = 0;
    encoded += enRle(resource, file, shan.baseSpriteID, shan.baseMaskID, shan.baseFrame);
    encoded += enRle(resource, file, shan.altSpriteID, shan.altMaskID, shan.altFrame);
    encoded += enRle(resource, file, shan.engineSpriteID, shan.engineMaskID, shan.engineFrame);
    encoded += enRle(resource, file, shan.lightSpriteID, shan.lightMaskID, shan.lightFrame);
    encoded += enRle(resource, file, shan.weaponSpriteID, shan.weaponMaskID, shan.weaponFrame);
    encoded += enRle(resource, file, shan.shieldSpriteID, shan.shieldMaskID, shan.shieldFrame);
    return encoded;
}

bool processType(rsrc::file& file, std::string typeCode) {
    auto typeList = file.type_container(typeCode).lock();
    if (typeList->count() == 0) {
        return false;
    }
    int64_t saved = 0;
    if (typeCode == "rlëD") {
        if (options.verbose) {
            printf("rlëD ID  Frames  Height      Size  New Height  New Size   Saved  Action\n");
        }
        for (auto resource : typeList->resources()) {
            try {
                saved += processRle(resource);
            } catch (const std::exception& e) {
                std::cerr << typeCode << " " << resource->id() << ": " << e.what() << std::endl;
            }
        }
        std::cout << "Saved " << saved << " bytes from " << typeList->count() << " rlëDs." << std::endl;
    } else if (typeCode == "PICT") {
        if (options.verbose) {
            printf("PICT ID  Type        Size  New Type  New Size   Saved  Action\n");
        }
        for (auto resource : typeList->resources()) {
            try {
                saved += processPict(resource);
            } catch (const std::exception& e) {
                std::cerr << typeCode << " " << resource->id() << ": " << e.what() << std::endl;
            }
        }
        std::cout << "Saved " << saved << " bytes from " << typeList->count() << " PICTs." << std::endl;
    } else if (typeCode == "spïn") {
        if (options.verbose) {
            printf("spïn ID  rlëD ID  Frames   Width  Height  Sprite Size  Mask Size  rlëD Size\n");
        }
        for (auto resource : typeList->resources()) {
            try {
                saved += processSpin(resource, file);
            } catch (const std::exception& e) {
                std::cerr << typeCode << " " << resource->id() << ": " << e.what() << std::endl;
            }
        }
        std::cout << "Encoded " << saved << " rlëDs from " << typeList->count() << " spïns." << std::endl;
    } else if (typeCode == "shän") {
        if (options.verbose) {
            printf("shän ID  rlëD ID  Frames   Width  Height  Sprite Size  Mask Size  rlëD Size\n");
        }
        for (auto resource : typeList->resources()) {
            try {
                saved += processShan(resource, file);
            } catch (const std::exception& e) {
                std::cerr << typeCode << " " << resource->id() << ": " << e.what() << std::endl;
            }
        }
        std::cout << "Encoded " << saved << " rlëDs from " << typeList->count() << " shäns." << std::endl;
    }
    return saved != 0;
}

bool processFile(std::filesystem::path path, std::filesystem::path outpath) {
    auto filename = path.filename();
    rsrc::file file;
    try {
        file = rsrc::file(path.generic_string());
    } catch (const std::exception& e) {
        std::cerr << filename << ": " << e.what() << std::endl;
        return false;
    }
    
    std::cout << "Processing " << filename << "..." << std::endl;
    // Don't rewrite file if nothing changed and outpath not provided
    bool writeFile = !outpath.empty();
    // If trim is on, do encodes before processing rleDs so they can also be trimmed, otherwise encode after
    if (options.encode && options.trim) {
        writeFile |= processType(file, "spïn");
        writeFile |= processType(file, "shän");
    }
    writeFile |= processType(file, "rlëD");
    if (options.encode && !options.trim) {
        writeFile |= processType(file, "spïn");
        writeFile |= processType(file, "shän");
    }
    if (options.picts) {
        writeFile |= processType(file, "PICT");
    }
    if (!writeFile) {
        std::cout << "No changes written." << std::endl;
        return false;
    }
    
    auto format = file.current_format();
    if (outpath.empty()) {
        outpath = path;
    } else {
        auto ext = outpath.extension();
        if (ext == ".rez") {
            format = rsrc::file::format::rez;
        } else if (ext == ".ndat" || ext == ".npif" || ext == ".rsrc") {
            format = rsrc::file::format::classic;
        }
    }
    file.write(outpath.generic_string(), format);
    return true;
}

void printUsage() {
    std::cerr << "Usage: rleduce [options] file ..." << std::endl;
    std::cerr << "  -p --picts          normalize PICTs by rewriting them in a standard format" << std::endl;
    std::cerr << "  -r --reduce         reduce PICT depth to 16-bit (smaller output)" << std::endl;
    std::cerr << "  -e --encode         encode rlëDs from spïns/shäns with PICTs" << std::endl;
    std::cerr << "  -n --no-dither      don't dither when reducing to 16-bit (applies to -r and -e)" << std::endl;
    std::cerr << "  -t --trim           allow rlëD frame height trimming (not recommended)" << std::endl;
    std::cerr << "  -o --output <path>  set output file/directory" << std::endl;
    std::cerr << "  -v --verbose        enable verbose output" << std::endl;
}

void processOption(std::string arg) {
    if (arg == "p" || arg == "--picts") {
        options.picts = true;
    } else if (arg == "r" || arg == "--reduce") {
        options.picts = true;
        options.reduce = true;
    } else if (arg == "e" || arg == "--encode") {
        options.encode = true;
    } else if (arg == "n" || arg == "--no-dither") {
        options.dither = false;
    } else if (arg == "t" || arg == "--trim") {
        options.trim = true;
    } else if (arg == "v" || arg == "--verbose") {
        options.verbose = true;
    } else {
        std::cerr << "Unknown option: " << arg << std::endl;
        printUsage();
        exit(1);
    }
}

int main(int argc, const char * argv[]) {
    if (argc < 2) {
        std::cerr << "Optimize the size of rlëD and PICT resources in resource files." << std::endl;
        printUsage();
        return 1;
    }
    std::vector<std::filesystem::path> files;
    std::filesystem::path outpath;
    bool outdir = false;
    for (int i=1; i<argc; i++) {
        std::string arg(argv[i]);
        if (arg[0] == '-' && arg.size() > 1) {
            if (arg == "-o" || arg == "--output") {
                if (++i == argc) {
                    std::cerr << arg << " option requires a value." << std::endl;
                    return 1;
                }
                outpath = std::filesystem::path(argv[i]);
                if (std::filesystem::is_directory(outpath)) {
                    outdir = true;
                } else {
                    auto parent = outpath.parent_path();
                    if (!std::filesystem::is_directory(parent)) {
                        std::cerr << "Output directory " << parent << " does not exist." << std::endl;
                        return 1;
                    }
                }
            } else if (arg[1] == '-') {
                processOption(arg);
            } else {
                for (int j=1; j<arg.size(); j++) {
                    processOption(arg.substr(j, 1));
                }
            }
        } else {
            files.emplace_back(std::filesystem::path(arg));
        }
    }
    if (!files.size()) {
        std::cerr << "No files provided." << std::endl;
        printUsage();
        return 1;
    }
    for (auto file : files) {
        processFile(file, outdir ? outpath / file.filename() : outpath);
    }
    return 0;
}
