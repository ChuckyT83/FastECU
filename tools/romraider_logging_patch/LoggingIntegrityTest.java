// Offline regression against the real patched RomRaider classes. No native
// driver is registered/opened: constructor bypass is limited to this test.
import java.lang.reflect.*;
import java.util.*;
import com.sun.jna.*;
import com.sun.jna.ptr.NativeLongByReference;
import com.romraider.io.j2534.api.*;
import com.romraider.logger.ecu.comms.manager.PollingState;

public class LoggingIntegrityTest {
    static int assertions;
    static void check(boolean ok,String name){if(!ok)throw new AssertionError(name);++assertions;}
    interface Operation {void run() throws Exception;}
    static void rejects(Operation operation,String name) throws Exception {
        boolean failed=false;try{operation.run();}catch(J2534Exception e){failed=true;}
        check(failed,name);
    }
    static Object allocate(Class<?> cls)throws Exception {
        Class<?> unsafe=Class.forName("sun.misc.Unsafe");Field f=unsafe.getDeclaredField("theUnsafe");f.setAccessible(true);
        return unsafe.getMethod("allocateInstance",Class.class).invoke(f.get(null),cls);
    }
    static void set(Object o,String field,Object value)throws Exception {
        Field f=o.getClass().getDeclaredField(field);f.setAccessible(true);f.set(o,value);
    }
    public static class Bridge extends J2534_v0404 {
        Bridge(){super("THIS TEST CONSTRUCTOR MUST NEVER RUN");}
        int count=1,size=9,protocol=6,extra=9,reads,txStatus,rxClears;byte[] data;
        public NativeLong PassThruReadMsgs(NativeLong channel,Pointer output,NativeLongByReference n,NativeLong timeout) {
            ++reads;n.setValue(new NativeLong(count));
            if(count>0 && count<=n.getValue().intValue()) {
                output.setNativeLong(0,new NativeLong(protocol));output.setNativeLong(NativeLong.SIZE,new NativeLong(0));
                output.setNativeLong(2*NativeLong.SIZE,new NativeLong(0));output.setNativeLong(3*NativeLong.SIZE,new NativeLong(1));
                output.setNativeLong(4*NativeLong.SIZE,new NativeLong(size));output.setNativeLong(5*NativeLong.SIZE,new NativeLong(extra));
                if(data!=null)output.write(6*NativeLong.SIZE,data,0,data.length);
            }
            return new NativeLong(0);
        }
        public NativeLong PassThruGetLastError(java.nio.ByteBuffer b) {b.put(0,(byte)0);return new NativeLong(0);}
        public NativeLong PassThruIoctl(NativeLong channel,NativeLong id,Pointer input,Pointer output) {
            if(id.intValue()==J2534Impl.IOCtl.CLEAR_TX_BUFFER.getValue())return new NativeLong(txStatus);
            ++rxClears;return new NativeLong(0);
        }
    }
    static byte[] payload={0,0,7,(byte)0xe8,(byte)0xe8,0x52,0x64,0x18,0x18};
    public static void main(String[] args)throws Exception {
        final J2534Impl impl=(J2534Impl)allocate(J2534Impl.class);set(impl,"protocolID",new NativeLong(6));
        final Bridge bridge=(Bridge)allocate(Bridge.class);bridge.count=1;bridge.size=9;bridge.protocol=6;bridge.extra=9;bridge.data=payload;
        Field lib=J2534Impl.class.getDeclaredField("lib");lib.setAccessible(true);lib.set(null,bridge);
        check(Arrays.equals(impl.readMsg(6,1,100),payload),"exact payload");
        bridge.count=2;rejects(()->impl.readMsg(6,1,100),"excess count");
        bridge.count=-1;rejects(()->impl.readMsg(6,1,100),"negative count");
        bridge.count=1;bridge.size=4129;rejects(()->impl.readMsg(6,1,100),"oversized metadata");
        bridge.size=9;bridge.extra=10;rejects(()->impl.readMsg(6,1,100),"extra index");
        bridge.extra=9;bridge.protocol=5;rejects(()->impl.readMsg(6,1,100),"protocol mismatch");bridge.protocol=6;
        final byte[][] response={payload};
        J2534 fake=(J2534)Proxy.newProxyInstance(J2534.class.getClassLoader(),new Class<?>[]{J2534.class},(p,m,a)->{
            if(m.getName().equals("readMsg"))return response[0];
            if(m.getReturnType()==int.class)return 0;
            return null;
        });
        final J2534ConnectionISO15765 connection=(J2534ConnectionISO15765)allocate(J2534ConnectionISO15765.class);
        set(connection,"api",fake);set(connection,"timeout",2000L);
        byte[] received=new byte[9];PollingState state=(PollingState)Proxy.newProxyInstance(PollingState.class.getClassLoader(),new Class<?>[]{PollingState.class},(p,m,a)->m.getReturnType()==boolean.class?false:null);
        connection.send(new byte[]{1},received,state);check(Arrays.equals(received,payload),"exact connection response");
        response[0]=Arrays.copyOf(payload,7);Arrays.fill(received,(byte)0x55);
        rejects(()->connection.send(new byte[]{1},received,state),"short response rejected");check(received[8]==0x55,"no zero-padding/partial mutation");
        response[0]=Arrays.copyOf(payload,10);rejects(()->connection.send(new byte[]{1},received,state),"long response rejected");
        bridge.txStatus=1;set(impl,"boundedCustomDriver",true);set(impl,"noWritesYet",true);
        impl.clearBuffers(6);check(bridge.rxClears==1,"fresh reviewed channel RX clear");
        set(impl,"protocolID",new NativeLong(3));rejects(()->impl.clearBuffers(6),"no K-Line purge fallback");set(impl,"protocolID",new NativeLong(6));
        set(impl,"noWritesYet",false);rejects(()->impl.clearBuffers(6),"no active-channel TX-purge fallback");
        final byte[][] values=new byte[4][];
        java.util.List<com.romraider.logger.ecu.comms.query.EcuQuery> queries=new ArrayList<>();
        for(int i=0;i<4;++i) {
            final int index=i;
            queries.add((com.romraider.logger.ecu.comms.query.EcuQuery)Proxy.newProxyInstance(
                LoggingIntegrityTest.class.getClassLoader(),new Class<?>[]{com.romraider.logger.ecu.comms.query.EcuQuery.class},(p,m,a)->{
                    if(m.getName().equals("getBytes"))return new byte[]{0,0,(byte)(0x3c+index)};
                    if(m.getName().equals("getAddresses"))return new String[]{"addr"+index};
                    if(m.getName().equals("getHex"))return "addr"+index;
                    if(m.getName().equals("equals"))return p==a[0];
                    if(m.getName().equals("setResponse")){values[index]=((byte[])a[0]).clone();return null;}
                    return null;
                }));
        }
        com.romraider.io.protocol.ssm.iso15765.SSMLoggerProtocol ssm=new com.romraider.io.protocol.ssm.iso15765.SSMLoggerProtocol();
        com.romraider.logger.ecu.definition.Module ecu=new com.romraider.logger.ecu.definition.Module("ecu",new byte[]{0,0,7,(byte)0xe8},"test",new byte[]{0,0,7,(byte)0xe0},false);
        ssm.constructEcuInitRequest(ecu);
        byte[] request=ssm.constructReadAddressRequest(ecu,queries);
        check(Arrays.equals(request,new byte[]{0,0,7,(byte)0xe0,(byte)0xa8,0,0,0,0x3c,0,0,0x3d,0,0,0x3e,0,0,0x3f}),"ordered explicit addresses");
        ssm.processReadAddressResponses(queries,payload,state);
        for(int i=0;i<4;++i)check(values[i].length==1 && values[i][0]==payload[i+5],"SSM byte assignment "+i);
        queries.add(queries.get(0));check(ssm.constructReadAddressRequest(ecu,queries).length==18,"duplicate address removed");
        boolean bad=false;try{ssm.processReadAddressResponses(queries,Arrays.copyOf(payload,8),state);}catch(RuntimeException e){bad=true;}check(bad,"SSM short count");
        byte[] wrong=payload.clone();wrong[4]=(byte)0xea;bad=false;try{ssm.processReadAddressResponses(queries,wrong,state);}catch(RuntimeException e){bad=true;}check(bad,"SSM wrong command");
        com.romraider.logger.ecu.definition.EcuParameterConvertorImpl angle=new com.romraider.logger.ecu.definition.EcuParameterConvertorImpl("degrees","x-50","0",-1,"uint8",com.romraider.Settings.Endian.BIG,new HashMap<String,String>(),new com.romraider.logger.ecu.ui.handler.dash.GaugeMinMax(0,50,5));
        check(angle.convert(new byte[]{0x52})==32 && angle.convert(new byte[]{0x64})==50,"AVCS conversions");
        com.romraider.logger.ecu.definition.EcuParameterConvertorImpl duty=new com.romraider.logger.ecu.definition.EcuParameterConvertorImpl("%","x*100/255","0.00",-1,"uint8",com.romraider.Settings.Endian.BIG,new HashMap<String,String>(),new com.romraider.logger.ecu.ui.handler.dash.GaugeMinMax(0,100,10));
        check(Math.abs(duty.convert(new byte[]{0x18})-24.0*100/255)<0.000001,"OCV conversion");
        System.out.println("PASS "+assertions+" RomRaider logging-integrity assertions; no adapter calls");
    }
}
