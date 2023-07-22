
#include "parser.hpp"
#include "epsondisk.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fstream>
#include <boost/program_options.hpp>

void hexdump(char const *buf, unsigned int size) {
    uint16_t addr;
    for(addr = 0; addr < size; addr+=16) {
        printf("%04x:",addr);
        uint16_t a2 = addr;
        for(a2 = addr; a2 < addr+16; a2++) {
            if((a2 & 0x3) == 0)  //4-byte seperator
                printf(" ");
            if(a2 < size)
                printf(" %02x",(unsigned char)buf[a2]);
            else
                printf("   ");
        }
        printf(" ");
        for(a2 = addr; a2 < addr+16; a2++) {
            if(a2 < size) {
                if(0x20 <= buf[a2] && buf[a2] < 0x7f)
                    printf("%c",buf[a2]);
                else
                    printf(".");
            } else
                printf(" ");
        }
        printf("\n");
    }
    if((addr & 0xf) != 0xf)
        printf("\n");
}

namespace po = boost::program_options;

int main(int argc, char **argv) {

    // Declare the supported options.
    po::options_description desc("Options");
    desc.add_options()
    ("help", "produce help message")
    ("input,i", po::value<std::string>(), "Input teledisk image")
    ("output,o", po::value<std::string>(), "Output flat image")
    ("extract,x", po::value<std::vector<std::string> >(), "File to extract")
    ("verbose,v", "Verbose output")
    ("ls", "List directory")
    ;

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch(boost::program_options::error &e) {
        std::cout << "ERROR: " << e.what() << "\n";
        std::cout << desc << "\n";
        return 1;
    }

    if(vm.count("help")) {
        std::cout << desc << "\n";
        return 1;
    }

    if(vm.count("input") == 0) {
        std::cout << "No input teledisk image set\n";
        std::cout << desc << "\n";
        return 1;
    }

    bool verbose = vm.count("verbose") != 0;

    //std::cout << vm["input"].as<std::string>() << std::endl;
    //std::cout << vm["output"].as<std::string>() << std::endl;

    try {
        TeleDiskParser::Disk disk(vm["input"].as<std::string>().c_str());

        if(verbose) {
            std::cerr << "Found idCylinder: " << disk.min.idCylinder
                      << " - " << disk.max.idCylinder
                      << " idSide: " << disk.min.idSide
                      << " - " << disk.max.idSide
                      << " idSector: " << disk.min.idSector
                      << " - " << disk.max.idSector
                      << "\n";
        }

        if(vm.count("output")) {
            std::ofstream of(vm["output"].as<std::string>());

            //now dump disk in 0-39/0-1/1-16 format into one flat image file
            for(unsigned int c = disk.min.idCylinder;
                    c <= disk.max.idCylinder; c++)
                for(unsigned int h = disk.min.idSide;
                        h <= disk.max.idSide; h++)
                    for(unsigned int s = disk.min.idSector;
                            s <= disk.max.idSector; s++) {
                        TeleDiskParser::Sector *se = disk.findSector(TeleDiskParser::CHS(c,h,s));
                        if(se->data) {
                            if(verbose)
                                printf("dumping CHS %d/%d/%d\n",
                                       se->chs.idCylinder,se->chs.idSide,se->chs.idSector);
                            of.seekp(
                            ((se->chs.idCylinder*2+
                              se->chs.idSide)*16+
                             (se->chs.idSector-1))*256
                            );
                            of.write(se->data,256);
                        } else {
                            if(verbose)
                                printf("no data at CHS %d/%d/%d\n",
                                       se->chs.idCylinder,se->chs.idSide,se->chs.idSector);
                        }
                    }
        }
        epsonDirEnt directory[64];
        TeleDiskParser::CHS directory_base(4,0,1);
        TeleDiskParser::CHS data_space_base(4,0,9);
        if(vm.count("ls") || vm.count("extract")) {
            //first, copy all of it into continuous ram for easy access
            for(unsigned int sect = 1; sect <= 8; sect++) {
                TeleDiskParser::Sector *s = disk.findSector(TeleDiskParser::CHS(4,0,sect));
                if(s->data)
                    memcpy(((char *)directory)+(sect-1)*256,
                           s->data,
                           256);
            }
        }

        if(vm.count("ls")) {
            //now dump the directory
            for(unsigned int i = 0; i < 64; i++) {
                bool hasData = false;
                for(unsigned int j = 0; j < 32; j++) {
                    if(((unsigned char *)directory)[32*i+j] !=
                            0xe5) {
                        hasData = true;
                        break;
                    }
                }
                if(!hasData) {
                    if(verbose)
                        std::cerr << "Directory entry " << i << " is not set\n";
                    continue;
                }
                if(directory[i].us != 0 && !verbose)
                    continue;
                std::string filename = std::string(directory[i].file,8);
                filename.erase(filename.find_last_not_of(" ")+1);
                filename += ".";
                filename += (char)(directory[i].type[0] & 0x7f);
                filename += (char)(directory[i].type[1] & 0x7f);
                filename += (char)(directory[i].type[2] & 0x7f);
                filename.erase(filename.find_last_not_of(" ")+1);
                std::cerr << "Directory entry " << i << ": \"" << filename << "\"\n";
                if(verbose) {
                    std::cerr << "  valid:     "
                              << ((directory[i].us == 0)?"yes":"no")
                              << "\n";
                }
                std::cerr << "  filename:  \""
                          << std::string(directory[i].file,8)
                          << "\"\n";
                std::cerr << "  type:      \""
                          << (char)(directory[i].type[0] & 0x7f)
                          << (char)(directory[i].type[1] & 0x7f)
                          << (char)(directory[i].type[2] & 0x7f)
                          << "\"\n";
                std::cerr << "  read only: "
                          << ((directory[i].type[0] & 0x80)?"yes":"no")
                          << "\n";
                std::cerr << "  system:    "
                          << ((directory[i].type[1] & 0x80)?"yes":"no")
                          << "\n";
                std::cerr << "  size:      "
                          << (unsigned int)directory[i].rc
                          << " records (referenced in this entry)\n";
                std::cerr << "  extent num:"
                          << (unsigned int)directory[i].ex
                          << "\n";
                if(verbose) {
                    std::cerr << "  blocks:   ";
                    for(unsigned int j = 0; j < (unsigned int)((directory[i].rc+15)/16); j++) {
                        std::cerr << " " << (unsigned int)directory[i].block[j];
                    }
                    std::cerr << "\n";
                }
            }
        }

        if(vm.count("extract")) {
            auto extract_list = vm["extract"].as< std::vector<std::string> >();

            for(auto &e : extract_list) {
                bool found = false;
                for(unsigned int i = 0; i < 64; i++) {
                    if(directory[i].us != 0)
                        continue;
                    std::string filename = std::string(directory[i].file,8);
                    filename.erase(filename.find_last_not_of(" ")+1);
                    filename += ".";
                    filename += (char)(directory[i].type[0] & 0x7f);
                    filename += (char)(directory[i].type[1] & 0x7f);
                    filename += (char)(directory[i].type[2] & 0x7f);
                    filename.erase(filename.find_last_not_of(" ")+1);

                    if(filename != e)
                        continue;
                    found = true;
                    std::ofstream of(e);

                    //directory is at 0x8000, 0x800 bytes
                    //block 1 is at offset 0x8800
                    //we have data at
                    //0x8800-0x8900, (0x100 bytes, in block 1; 2 records at 0x80 per record)
                    //0x9000-0xa400, (0x1400 bytes, in block 2,3,4; 40 records at 0x80 per record)
                    //0xa800-0xb400, (0x0c00 bytes, in block 5,6; 24 records at 0x80 per record)
                    //   this actually has been changed, because there is an interesting
                    //   break at 0xb080, makes this
                    //               (0x0880 bytes, in block 5,6; 17 records at 0x80 per record)
                    //0xb800-0xbb00  (0x0300 bytes, in block 7; 6 records at 0x80 per record)
                    //we have files at
                    //block 1 (2 records)
                    //block 2,3,4 (39 records)
                    //block 5,6 (17 records)
                    //block 7 (6 records)
                    //no, 2k blocks at 8 sectors, 16 records
                    for(unsigned int j = 0; j < (unsigned int)((directory[i].rc+15)/16); j++) {
                        TeleDiskParser::CHS cur = data_space_base.advanceSHC(
                                                  (directory[i].block[j]-1) * 8,
                                                  disk.min, disk.max);
                        unsigned int num_sects = 8;
                        unsigned int last_sect_records = 2;
                        if(j == (unsigned int)((directory[i].rc-1)/16)) {
                            num_sects = ((directory[i].rc % 16)+1)/2;
                            last_sect_records = directory[i].rc % 2;
                            if(last_sect_records == 0)
                                last_sect_records = 2;
                        }
                        for(unsigned int k = 0; k < num_sects; k++) {
                            TeleDiskParser::Sector *se = disk.findSector(cur);
                            if(verbose)
                                printf("dumping CHS %d/%d/%d\n",
                                       se->chs.idCylinder,se->chs.idSide,se->chs.idSector);
                            unsigned int bytes = 256;
                            if(k == num_sects-1)
                                bytes = last_sect_records * 128;
                            if(se->data) {
                                of.write(se->data,bytes);
                            } else {
                                char buf[256];
                                memset(buf, 0xe5, 256);
                                of.write(buf, bytes);
                            }
                            cur = cur.advanceSHC(1, disk.min, disk.max);
                        }
                    }
                }
                if(!found)
                    std::cerr << "file " << e << " not found\n";
            }
        }

        return 0;
    } catch(char const *c) {
        std::cerr << "exception: \""<<c<<"\"\n";
        return 1;
    }
}
