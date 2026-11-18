#include "../runtime/src/hw/ipu.cpp"
#include <cassert>
namespace hw { DmaChannel dma[10]; u32 d_enable = 0; }
namespace mem { u8* ram = nullptr; u8* spr = nullptr; }
namespace kernel { EEContext* current_ctx() { return nullptr; } void raise_intc(int) {} }
void dma_finish_external_ipu(int) { abort(); }
bool g_log_enabled[LOG_COUNT] = {};
void log_write(int,const char*,...) {}
[[noreturn]] void fatal(const char*,...) { abort(); }
static void bits(const std::string& s) {
 ipu::write_reg(0x10002010,0x40000000);
 ipu::g_in.assign((s.size()+127)/128*16,0);
 for(size_t i=0;i<s.size();++i) if(s[i]=='1') ipu::g_in[i/8] |= 1u<<(7-i%8);
}
int main() {
 ipu::init();
 bits("01111111111111111111111111111111");
 ipu::write_reg(0x10002000,0x30000000);
 assert(ipu::g_cmd_data==0x30002); assert(ipu::g_bitpos==3);
 bits("00000001000111111111111111111111");
 ipu::write_reg(0x10002000,0x30000000); assert(ipu::g_cmd_data==0xb0023);
 bits("00000000000000000000000110110011");
 ipu::write_reg(0x10002000,0x30000000); assert(ipu::g_cmd_data==0); assert(ipu::g_ctrl&(1<<14));
 ipu::write_reg(0x10002000,0x40000000); assert(!(ipu::g_ctrl&(3<<14)));
 bits("11111111111111111111111111111111");
 ipu::write_reg(0x10002000,0x38000000); assert(ipu::g_cmd_data==0x10000);
 // Four luma zero-DC/EOB blocks, two chroma zero-DC/EOB blocks, padding then slice header.
 std::string mb="10010100101001010010" "00100010";
 while(mb.size()%8) mb+='0';
 mb += "00000000000000000000000100000010";
 bits(mb); ipu::write_reg(0x10002000,0x2c010000);
 assert(!ipu::g_busy); assert(ipu::g_out.size()==768);
 assert(ipu::g_ctrl&(1<<15)); assert(!(ipu::g_ctrl&(1<<14)));
 assert(ipu::g_top==0x00000102);
 bits("0000000011111111"); ipu::g_ctrl = 1u<<23;
 auto r = ipu::begin(); assert(ipu::escape_level(r)==255 && r.pos==16);
 bits("1000000000000001"); ipu::g_ctrl = 1u<<23;
 r=ipu::begin(); assert(ipu::escape_level(r)==-255 && r.pos==16);
 bits("11111111"); ipu::g_ctrl = 1u<<23;
 r=ipu::begin(); assert(ipu::escape_level(r)==-1 && r.pos==8);
 bits("111111111111"); r=ipu::begin(); assert(ipu::escape_level(r)==-1 && r.pos==12);
 // Intra block with MPEG-1 AC escape (+1), exactly 25 bits including EOB.
 bits("1000000010000000000000110"); ipu::g_ctrl = 1u<<23;
 s16 block[64]; r=ipu::begin();
 assert(ipu::decode_intra_block(r,0,2,block) && r.pos==25);
 puts("PASS: MPEG1/2 escape levels and bit alignment;  VDEC length/escape/zero/error reset; BDEC slice start detection");
}
