
#include "parser.hpp"
#include "epsondisk.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fstream>

int main(int argc, char **argv) {

    if(argc < 3) {
        std::cerr << "Please provide input and temporary file\n";
        return 1;
    }

    const char *inputfile = argv[1];
    const char *tempfile = argv[2];

    try {
        TeleDiskParser::Disk disk1(inputfile);

        disk1.advancedCompression = true;
        disk1.write(tempfile);

        TeleDiskParser::Disk disk2(tempfile);

        if(!disk1.comment != !disk2.comment) {
            std::cerr << "Comment mismatch" << std::endl;
            return 1;
        } else if (disk1.comment) {
            if(disk1.comment->comment != disk2.comment->comment) {
                std::cerr << "Comment text mismatch" << std::endl;
                return 1;
            }
            if(memcmp(&disk1.comment->timestamp, &disk2.comment->timestamp,
                      sizeof(TeleDiskParser::Comment::Timestamp)) != 0) {
                std::cerr << "Comment timestamp mismatch" << std::endl;
                return 1;
            }
        }
        if(disk1.dosMode != disk2.dosMode) {
            std::cerr << "dosMode mismatch" << std::endl;
            return 1;
        }
        if(disk1.driveType != disk2.driveType) {
            std::cerr << "driveType mismatch" << std::endl;
            return 1;
        }
        if(disk1.mediaSurfaces != disk2.mediaSurfaces) {
            std::cerr << "mediaSurfaces mismatch" << std::endl;
            return 1;
        }
        if(disk1.sourceDensity != disk2.sourceDensity) {
            std::cerr << "sourceDensity mismatch" << std::endl;
            return 1;
        }
        if(disk1.trackDensity != disk2.trackDensity) {
            std::cerr << "trackDensity mismatch" << std::endl;
            return 1;
        }
        if(disk1.tracks.size() != disk2.tracks.size()) {
            std::cerr << "Track count mismatch: original: " << disk1.tracks.size()
                      << " copy: " << disk2.tracks.size() << std::endl;
            return 1;
        }
        for(auto &track : disk1.tracks) {
            auto othtrack = disk2.findTrack(track.physCylinder, track.physSide);
            if(!othtrack) {
                std::cerr << "Cannot find track for cylinder " << track.physCylinder
                          << " side " << track.physSide << " on copy" << std::endl;
                return 1;
            }
            if(track.sectors.size() != othtrack->sectors.size()) {
                std::cerr << "On track at cylinder " << track.physCylinder
                          << " side " << track.physSide << std::endl;
                std::cerr << "Sector count mismatch: original: " << track.sectors.size()
                          << " copy: " << othtrack->sectors.size() << std::endl;
                return 1;
            }
            for(auto &sector : track.sectors) {
                auto othsector = disk2.findSector(sector.chs);
                if(sector.flags != othsector->flags) {
                    std::cerr << "Sector flags mismatch" << std::endl;
                    return 1;
                }
                if(sector.idLengthCode != othsector->idLengthCode) {
                    std::cerr << "Sector idLengthCode mismatch" << std::endl;
                    return 1;
                }
                if(sector.data.size() != othsector->data.size()) {
                    std::cerr << "Sector size mismatch" << std::endl;
                    return 1;
                }
                if(memcmp(sector.data.data(), othsector->data.data(),
                    sector.data.size()) != 0) {
                    std::cerr << "Sector data mismatch" << std::endl;
                    return 1;
                }
            }
        }

    } catch(char const *c) {
        std::cerr << "exception: \""<<c<<"\"\n";
        return 1;
    }
    return 0;
}
