#ifndef SH7058_OFFLINE_FLASH_MODEL_H
#define SH7058_OFFLINE_FLASH_MODEL_H
#ifndef FASTECU_OFFLINE_KERNEL_TESTS
#error Offline model must never be linked into the application
#endif
// Independently written stateful fixture for the exact bundled kernel's command
// contract. It does not execute SH code/MMIO or model electrical flash physics.
// Invalid ranges beyond the synthetic allocation are rejected, not emulated as
// the real kernel's unchecked accesses; kernel weaknesses are established statically.
// Static basis: pinned kernel 5fb9cb89..., dispatch 3AC4; 22:3DF8,
// 23:3E52, 24:3EF2, 25:402C, erase lookup:4778, program/readback:47E4.
class FlashKernelModel {
public:
    QByteArray flash, buffer=QByteArray(4096,'\0');
    QString fault;
    int index=0, transfers=0, erases=0, commits=0, validations=0;
    bool enabled=false;
    explicit FlashKernelModel(const QByteArray &input):flash(input) {
        // Two independently erased sectors differ, all other flash is unchanged.
        for(int i=0;i<8192;++i) flash[i]=char(uint8_t(flash.at(i))^0x55);
    }
    static uint32_t word(const QByteArray &b,int p,int n=4) {
        uint32_t v=0;for(int i=0;i<n;++i)v=(v<<8)|uint8_t(b.at(p+i));return v;
    }
    static QByteArray be(uint32_t v,int n=4) {
        QByteArray b;for(int i=n-1;i>=0;--i)b+=char(v>>(8*i));return b;
    }
    static uint32_t crc(const QByteArray &b) {
        uint32_t v=0xffffffffu;
        for(char c:b) { v^=uint8_t(c);for(int i=0;i<8;++i)v=(v>>1)^((v&1)?0x5aa5a55au:0); }
        return ~v;
    }
    static QByteArray reply(const QByteArray &body) {
        return QByteArray::fromHex("000007e8beef")+be(uint32_t(body.size()),2)+body;
    }
    static QByteArray error(int cmd,int status) {return reply(QByteArray(1,char(0x7f))+char(cmd)+char(status));}
    QByteArray respond(const QByteArray &request) {
        if(request.size()<9 || request.left(6)!=QByteArray::fromHex("000007e0beef") ||
            word(request,6,2)!=uint32_t(request.size()-8)) return error(0,0x30);
        const auto body=request.mid(8);const int cmd=uint8_t(body.at(0));
        if(cmd==2 && body.size()==9) {
            const auto address=word(body,1),length=word(body,5);
            if(!SubaruSH7058Safety::validRange(address,length)) return error(cmd,0x30);
            return reply(QByteArray(1,char(0x42))+be(crc(flash.mid(int(address),int(length)))));
        }
        if(cmd==5 && body.size()==1) return reply(QByteArray::fromHex(fault=="capabilities"?"4500000100":"4500000200"));
        if(cmd==6 && body.size()==1) return reply(QByteArray::fromHex(fault=="block-capabilities"?"4600002000":"4600001000"));
        if(cmd==4 && body.size()==1) return reply(QByteArray::fromHex(fault=="short-voltage"?"440230":"4402300230")); // not measured voltage
        if((cmd==0x20 || cmd==0x21) && body.size()==1) {enabled=cmd==0x20;return reply(QByteArray(1,char(cmd|0x40)));}
        if(cmd==0x25 && body.size()==5) {
            index=0;
            if(fault=="erase-error") return error(cmd,0x80);
            // Deliberately reproduce the binary's unmatched-address => index0
            // lookup. Host tests, not this fixture, must prevent that request.
            unsigned sector=0;const auto address=word(body,1);
            for(unsigned i=0;i<16;++i)if(fblocks_SH7058[i].start==address)sector=i;
            if(enabled) {++erases;const auto b=fblocks_SH7058[sector];flash.replace(int(b.start),int(b.len),QByteArray(int(b.len),char(0xff)));}
            if(fault=="erase-timeout") return {};
            return reply(QByteArray(1,char(0x65)));
        }
        if(cmd==0x22 && body.size()==517) {
            ++transfers;
            // The actual kernel ignores the address and appends by rolling index.
            buffer.replace(index,512,body.mid(5));
            if(fault=="buffer-corruption" && transfers==3) buffer[index]=char(uint8_t(buffer.at(index))^1);
            index=(index+512)%4096;
            return reply(QByteArray(1,char(0x62)));
        }
        if((cmd==0x23 || cmd==0x24) && body.size()==11) {
            const auto address=word(body,1),length=word(body,5,2),expected=word(body,7);
            if(length>4096 || !SubaruSH7058Safety::validRange(address,4096)) return error(cmd,0x88);
            uint32_t observed=0;
            if(cmd==0x23) {++validations;observed=crc(buffer.left(int(length)));}
            else {
                // Binary always programs 4096, regardless of the CRC length.
                if(enabled) {
                    ++commits;
                    if(fault=="partial-program") {flash.replace(int(address),2048,buffer.left(2048));return error(cmd,0x80);}
                    flash.replace(int(address),4096,buffer);
                }
                observed=crc(flash.mid(int(address),int(length)));
            }
            if(observed!=expected) return reply(QByteArray::fromHex("7f")+char(cmd)+be(observed));
            if(cmd==0x24 && fault=="commit-timeout") return {};
            if(cmd==0x24 && fault=="final-crc") flash[int(address)]=char(uint8_t(flash.at(int(address)))^1);
            return reply(QByteArray(1,char(cmd|0x40)));
        }
        return error(cmd,0x30);
    }
};
#endif
