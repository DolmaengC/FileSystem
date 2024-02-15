#include <iostream>
#include <fstream>
#include <vector>
#include "byte_buffer.hpp"
#include <sys/stat.h>
#include <sys/types.h>

using namespace std;



class Superblock {
  public:
    Superblock(uint8_t* buffer, int size) {
      sys::io::byte_buffer bb((uint8_t*)buffer, 0, size); 
      bb.skip(0x0b);
      sector_size = bb.get_uint16_le();
      sector_count = bb.get_int8();
      reserved_sector_size = bb.get_uint16_le();
      cluster_size = sector_size * sector_count;
      
      fat_count = bb.get_int8();
      bb.skip(0x13);
      FAT_sector_count = bb.get_uint32_le();
      bb.skip(0x4);
      root_cluster = bb.get_uint32_le();
      fat_area_addr = reserved_sector_size * sector_size;
      fat_area_size = FAT_sector_count * sector_size * 2;
    }
    uint16_t sector_size;
    uint16_t sector_count;
    uint32_t cluster_size;
    uint16_t reserved_sector_size;
    uint8_t fat_count;
    uint32_t FAT_sector_count;
    uint32_t root_cluster;
    uint32_t fat_area_addr;
    uint32_t fat_area_size;
    auto sb_all_print();
};

class FatArea {
  public:
    FatArea(const std::vector<uint8_t>& buffer, int size) {
      sys::io::byte_buffer bb((uint8_t*)buffer.data(), 0, size); 
      int entry_count = buffer.size() / 4;
      
      fat.resize(entry_count);
      for (int i = 0; i < entry_count; i++) {
        fat[i] = bb.get_uint32_le();
        // cout << "fat[" << i << "] = " << fat[i] << endl;
      }
    }
    std::vector<uint32_t> fat;
};

class FileWrite {
public:
    FileWrite(const std::vector<uint8_t>& buffer, int size, string path) {
        sys::io::byte_buffer bb((uint8_t*)buffer.data(), 0, size);
        fstream ofs(path, ios::out);
        ofs << bb.get_ascii(size);
        ofs.close();
    }
};

class DirectoryEntry {
  public:
    DirectoryEntry(const std::vector<uint8_t>& buffer, int size, string path) {
      sys::io::byte_buffer bb((uint8_t*)buffer.data(), 0, size); 
      name = bb.get_ascii(8);
      extention = bb.get_ascii(3);
      attr = bb.get_uint8();
      reserved = bb.get_uint16_le();
      creation_time = bb.get_uint16_le();
      created_date = bb.get_uint16_le();
      last_access_date = bb.get_uint16_le();
      cluster_hi = bb.get_uint16_le();
      last_written_time = bb.get_uint16_le();
      last_written_date = bb.get_uint16_le();
      cluster_lo = bb.get_uint16_le();
      file_size = bb.get_uint32_le();
      cluster_no = (cluster_hi << 16) | cluster_lo;
      
      if (static_cast<int>(name[0]) == -27) {
        cout << "Deleted file is founded : " << name << endl;
        name = name.erase(0,1);
      }
      full_name = name + "." + extention;
    }
    string name;
    string extention;
    string full_name;
    uint8_t attr;
    uint32_t reserved;
    uint32_t creation_time;
    uint32_t created_date;
    uint32_t last_access_date;
    uint32_t cluster_hi;
    uint32_t cluster_lo;
    uint64_t cluster_no;
    uint32_t last_written_time;
    uint32_t last_written_date;
    uint64_t file_size;
};

std::vector<int> toExtents(Superblock sb, FatArea fa, uint64_t cluster_no) {
  vector<int> extents;
  uint64_t next = cluster_no;
  while (next != 0xfffffff && next != 0) {
    // cout << "cluster no = " << next << endl;
    //uint64_t offset = 0x400000 + (next-2)*sb.cluster_size;
    int offset = 0x400000 + ((int)next - 2) * sb.cluster_size;
    extents.push_back(offset);
    next = fa.fat[next];
  }
  return extents;
}

class Finder {
  public:
  Finder(Superblock sb, FatArea fa, string input_file, string path, int addr) {
    std::vector<uint8_t> buffer(32, 0);
    fstream ifs(input_file);
    fstream tmp_ifs(input_file);
    ifs.seekp(addr);
    
    int root_addr = sb.fat_area_addr + sb.fat_area_size;
    // look around every component in a directory
    while(1) {
      ifs.read((char*)buffer.data(), 32);
      DirectoryEntry node(buffer, 32, path);
      if(node.attr == 0) break;
      dr.push_back(node);
      int physical_addr = root_addr + ((int)node.cluster_no - 2) * sb.cluster_size;
      tmp_ifs.seekp(physical_addr);

      if (node.attr == 0x20) {
        if (node.file_size != 0) {
          string file_path = path + "/" + node.full_name;
          cout << file_path << endl;
          std::vector<uint8_t> file_buffer(node.file_size, 0);
          std::vector<int> extents = toExtents(sb, fa, node.cluster_no);
          if (extents.size() != node.file_size / sb.cluster_size + 1) {
            tmp_ifs.read((char*)file_buffer.data(), node.file_size);
          }
          else {
            uint8_t count = 0; 
            for (int i = 0; i < extents.size(); i++) {
              tmp_ifs.seekp(extents[i]);
              tmp_ifs.read((char*)file_buffer.data(), sb.cluster_size);
            }
          }
          FileWrite fw(file_buffer, node.file_size, file_path);
        } 
      }
      else if (node.attr == 0x10) {
        string new_path = path + "/" + node.name;
        if (mkdir(new_path.c_str(), 0776) == -1 && errno != EEXIST) {
          perror("Failed mkdir");
        }
        // cout << "dir name = " << node.file_name << endl;;
        if (node.name[0] != '.') {
          Finder f(sb, fa, input_file, new_path, physical_addr);
        } 
      }
    }
    
  }
  vector<DirectoryEntry> dr;
};

// class Fat32 {
//   public:
//   Fat32(Superblock sb, FatArea fa, string input_file) {
//     this->sb = sb;
//     this->fa = fa;
//     this->input_file = input_file;
//   }
//   Superblock sb;
//   FatArea fa;
//   string input_file;
// };


int main(int argc, char* argv[])
{
  // fstream ifs("binary_io_practice.bin"s);
  string input_file = "FAT32_simple2.mdf";
  fstream ifs(input_file);
  if (!ifs.good())
    cout << "error";

  char buffer[200] = {0};
  ifs.read(buffer, 200);

  sys::io::byte_buffer bb((uint8_t*)buffer, 0, 200);
  Superblock sb((uint8_t*)buffer, 200);
  int fat_addr = (int)sb.fat_area_addr;
  int fat_size = (int)sb.fat_area_size;

  std::vector<uint8_t> fat_buffer(fat_size, 0);
  
  ifs.seekp(fat_addr);
  ifs.read((char*)fat_buffer.data(), fat_size);

  FatArea fa(fat_buffer, fat_size);
  
  int data_addr = fat_addr + fat_size;
  // cout << "data_addr = " << data_addr << endl; // 0x400000 = 4194304

  if (mkdir("out", 0776) == -1 && errno != EEXIST) {
    perror("Failed mkdir");
  }

  Finder finder(sb, fa, input_file, "out", data_addr);
  //Finder finder(sb, fa, input_file, "out", 0x404000);  
  
  
  return 0;
}
