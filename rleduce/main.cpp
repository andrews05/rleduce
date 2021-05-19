//
//  main.cpp
//  rleduce
//
//  Created by Andrew Simmonds on 19/05/21.
//

#include <iostream>
#include "libGraphite/data/reader.hpp"
#include "libGraphite/data/writer.hpp"
#include "libGraphite/rsrc/file.hpp"

enum rleop : uint8_t
{
    eof = 0x00,
    line_start = 0x01,
    pixel_data = 0x02,
    transparent_run = 0x03,
    pixel_run = 0x04,
};

bool processRle(std::shared_ptr<graphite::rsrc::resource> resource, bool resize) {
    auto reader = graphite::data::reader(resource->data());
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
        trim = height;
        for (int i=0; i<frames; i++) {
            bool start = true;
            int top = 0;
            int bottom = 0;
            while (true) {
                op = reader.read_long();
                opcode = static_cast<rleop>(op >> 24);
                if (opcode == line_start) {
                    count = op & 0x00FFFFFF;
                    if (count != 0) {
                        reader.move(count);
                        start = false;
                        bottom = 0;
                    } else if (start) {
                        top++;
                    } else {
                        bottom++;
                    }
                } else {
                    break;
                }
            }
            if (top < trim) {
                trim = top;
            }
            if (bottom < trim) {
                trim = bottom;
            }
        }
    }
    
    auto writer = graphite::data::writer();
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
        return false;
    }
    std::cout << resource->id() << ": saved " << diff << std::endl;
    resource->set_data(writer.data());
    return true;
}

bool processFile(const char * path, bool resize) {
    graphite::rsrc::file file;
    try {
        file = graphite::rsrc::file(path);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return false;
    }
    std::cout << path << std::endl;
    bool altered = false;
    for (auto typeList : file.types()) {
        if (typeList->code() == "rlëD") {
            for (auto resource : typeList->resources()) {
                if (processRle(resource, resize)) {
                    altered = true;
                }
            }
        }
    }
    if (!altered) {
        return false;
    }
    file.write(path, file.current_format());
    return true;
}

int main(int argc, const char * argv[]) {
    bool resize = false;
    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            resize = true;
        } else {
            processFile(argv[i], resize);
        }
    }
    return 0;
}
