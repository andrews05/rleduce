//
//  main.cpp
//  rleduce
//
//  Created by Andrew Simmonds on 19/05/21.
//

#include <filesystem>
#include <iostream>
#include "libGraphite/data/reader.hpp"
#include "libGraphite/data/writer.hpp"
#include "libGraphite/quickdraw/pict.hpp"
#include "libGraphite/quickdraw/rle.hpp"
#include "libGraphite/rsrc/file.hpp"
using namespace graphite;

enum rleop: uint8_t {
    eof = 0x00,
    line_start = 0x01,
    pixel_data = 0x02,
    transparent_run = 0x03,
    pixel_run = 0x04,
};

std::size_t processRle(std::shared_ptr<rsrc::resource> resource, bool resize) {
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
    if (resize) {
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
    writer.write_short(height - (trim * 2));
    reader.set_position(4);
    writer.write_data(reader.read_data(12));
    for (int i=0; i<frames; i++) {
        int skip = trim;
        int blank = 0;
        while (true) {
            op = reader.read_long();
            if (skip-- > 0) {
                continue;
            }
            opcode = static_cast<rleop>(op >> 24);
            if (opcode == line_start) {
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
    
    auto diff = resource->data()->size() - writer.data()->size();
    if (diff <= 0) {
        return 0;
    }
    resource->set_data(writer.data());
    return diff;
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

void ditherSurface(std::shared_ptr<qd::surface> surface) {
    // QuickDraw dithering algorithm.
    // Half the error is diffused right on even rows, left on odd rows. The remainder is diffused down.
    auto frameWidth = surface->size().width();
    auto frameHeight = surface->size().height();
    for (int y=0; y<frameHeight; y++) {
        bool even = y % 2 == 0;
        for (int w=0; w<frameWidth; w++) {
            int x = even ? w : frameWidth - w - 1;
            auto color = surface->at(x, y);
            int comps[3] = { color.red_component(), color.green_component(), color.blue_component() };
            int errors[3];
            for (int i=0; i<3; i++) {
                int newComp = (comps[i] & 0xF8) * 0xFF / 0xF8;
                errors[i] = comps[i] - newComp;
            }
            if (errors[0] || errors[1] || errors[2]) {
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

std::size_t processPict(std::shared_ptr<rsrc::resource> resource, bool rgb555) {
    qd::pict pict(resource->data());
    ditherSurface(pict.image_surface().lock());
    auto data = pict.data(rgb555);
    auto diff = resource->data()->size() - data->size();
    if (diff <= 0) {
        return 0;
    }
    resource->set_data(data);
    return diff;
}

bool processSpin(std::shared_ptr<rsrc::resource> resource, rsrc::file file) {
    auto reader = data::reader(resource->data());
    auto spriteID = reader.read_short();
    auto maskID = reader.read_short();
    auto frameX = reader.read_short();
    auto frameY = reader.read_short();
    auto gridX = reader.read_short();
    auto gridY = reader.read_short();
    
    auto spriteRes = file.find("PICT", spriteID, {}).lock();
    auto maskRes = file.find("PICT", maskID, {}).lock();
    if (spriteRes == nullptr || maskRes == nullptr || frameX <= 0 || frameY <= 0 || gridX <= 0 || gridY <= 0) {
        return false;
    }
    auto spritePict = qd::pict(spriteRes->data());
    auto maskPict = qd::pict(maskRes->data());
    auto sprite = spritePict.image_surface().lock();
    auto mask = maskPict.image_surface().lock();
    if (sprite->size().width() < frameX * gridX || sprite->size().height() < frameY * gridY
        || mask->size().width() < frameX * gridX || mask->size().height() < frameY * gridY) {
        return false;
    }
    
    auto rle = qd::rle(qd::size(frameX, frameY), gridX * gridY);
    ditherSurface(sprite);
    for (int gy=0; gy<gridY; gy++) {
        for (int gx=0; gx<gridX; gx++) {
            auto frame = std::make_shared<qd::surface>(qd::surface(frameX, frameY));
            for (int fy=0; fy<frameY; fy++) {
                for (int fx=0; fx<frameX; fx++) {
                    auto maskPixel = mask->at(gx * frameX + fx, gy * frameY + fy);
                    if (maskPixel.red_component() || maskPixel.green_component() || maskPixel.blue_component()) {
                        frame->set(fx, fy, sprite->at(gx * frameX + fx, gy * frameY + fy));
                    }
                }
            }
            rle.write_frame(gy * gridX + gx, frame);
        }
    }
    
    file.add_resource("rlëD", spriteID, resource->name(), rle.data());
    return true;
}

bool processFile(std::string path, bool resize, bool rgb555) {
    auto filename = std::filesystem::path(path).filename();
    rsrc::file file;
    try {
        file = rsrc::file(path);
    } catch (const std::exception& e) {
        std::cerr << filename << ": " << e.what() << std::endl;
        return false;
    }
    std::cout << "Processing " << filename << "..." << std::endl;
    bool writeFile = false;
    for (auto typeList : file.types()) {
        std::size_t saved = 0;
        if (typeList->code() == "rlëD") {
            for (auto resource : typeList->resources()) {
                saved += processRle(resource, resize);
            }
            std::cout << "Saved " << saved << " bytes from " << typeList->count() << " rlëDs." << std::endl;
        } else if (typeList->code() == "PICT") {
            for (auto resource : typeList->resources()) {
                saved += processPict(resource, rgb555);
            }
            std::cout << "Saved " << saved << " bytes from " << typeList->count() << " PICTs." << std::endl;
        } else if (typeList->code() == "spïn") {
            for (auto resource : typeList->resources()) {
                processSpin(resource, file);
            }
        }
        if (saved) {
            writeFile = true;
        }
    }
    if (!writeFile) {
        std::cout << "No changes written." << std::endl;
        return false;
    }
    file.write(path+".rez", file.current_format());
    return true;
}

int main(int argc, const char * argv[]) {
    if (argc < 2) {
        std::cerr << "Optimize the size of rlëD and PICT resources in resource files." << std::endl;
        std::cerr << "Usage: rleduce [-f] file ..." << std::endl;
        std::cerr << "  -f  allow rlëD frame resizing (further optimization)" << std::endl;
        std::cerr << "  -d  reduce PICT depth to 16-bit (further optimization)" << std::endl;
        return 1;
    }
    bool resize = false;
    bool rgb555 = false;
    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i], "-f") == 0) {
            resize = true;
        } else if (strcmp(argv[i], "-d") == 0) {
            rgb555 = true;
        } else {
            processFile(std::string(argv[i]), resize, rgb555);
        }
    }
    return 0;
}
