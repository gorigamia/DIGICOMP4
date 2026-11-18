#include "../runtime/src/iop/pad.cpp"
#include <cassert>
namespace mem { u8* iop_ram; }
static iop::RpcFunc handler;
static int writes;
static u32 last_address;
static u8 last_frame[128];
namespace iop {
RpcServer* register_native_server(u32,u32,RpcFunc fn) { handler=fn; return nullptr; }
}
namespace sif {
void ee_write(u32 address,const void* data,u32 bytes) {
 assert(bytes==128); ++writes; last_address=address; memcpy(last_frame,data,128);
}
}
namespace platform {
PadState read_pad(int) { return PadState{8,128,128,128,128,{}}; }
void set_vibration(float,float) {}
}
namespace iop::modules { void hexdump(int,const char*,const u8*,u32) {} }
bool g_log_enabled[LOG_COUNT]={};
void log_write(int,const char*,...) {}
int main() {
 mem::iop_ram=new u8[0x200000]{};
 iop::pad::register_servers();
 auto* b=(u32*)mem::iop_ptr(0x100);
 b[0]=1; b[1]=0; b[2]=0; b[4]=0x10000;
 handler(0,0x100,128); assert(b[3]==1 && writes==2);
 for(int slot=1;slot<4;++slot) {
  b[0]=1;b[1]=0;b[2]=slot;b[4]=0x20000+slot*256;
  handler(0,0x100,128); assert(b[3]==0 && writes==2);
 }
 iop::pad::vblank();
 assert(writes==3 && last_address>=0x10000 && last_address<0x10100);
 assert(last_frame[2]==0xf7 && last_frame[3]==0xff);
 assert(last_frame[112]==6 && last_frame[103]==1);
 puts("PASS: absent multitap probes preserve player one and Start button DMA");
}
