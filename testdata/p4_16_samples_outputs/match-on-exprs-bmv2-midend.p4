#include <core.p4>
#define V1MODEL_VERSION 20180101
#include <v1model.p4>

header ethernet_t {
    bit<48> dstAddr;
    bit<48> srcAddr;
    bit<16> etherType;
}

struct headers_t {
    ethernet_t ethernet;
}

struct metadata_t {
}

parser parserImpl(packet_in packet, out headers_t hdr, inout metadata_t meta, inout standard_metadata_t stdmeta) {
    state start {
        packet.extract<ethernet_t>(hdr.ethernet);
        transition accept;
    }
}

control verifyChecksum(inout headers_t hdr, inout metadata_t meta) {
    apply {
    }
}

control ingressImpl(inout headers_t hdr, inout metadata_t meta, inout standard_metadata_t stdmeta) {
    bit<48> key_1;
    bit<16> key_2;
    @noWarn("unused") @name(".NoAction") action NoAction_1() {
    }
    @name("ingressImpl.my_drop") action my_drop() {
        mark_to_drop(stdmeta);
    }
    @name("ingressImpl.foo") action foo(@name("out_port") bit<9> out_port) {
        hdr.ethernet.dstAddr[22:18] = hdr.ethernet.srcAddr[5:1];
        stdmeta.egress_spec = out_port;
    }
    @name("ingressImpl.t1") table t1_0 {
        key = {
            hdr.ethernet.srcAddr[22:18]: exact @name("ethernet.srcAddr.slice") ;
            key_1                      : exact @name("dstAddr_lsbs") ;
            key_2                      : exact @name("etherType_less_10") ;
        }
        actions = {
            foo();
            my_drop();
            NoAction_1();
        }
        const default_action = NoAction_1();
    }
    @hidden action matchonexprsbmv2l64() {
        key_1 = hdr.ethernet.dstAddr & 48w0x10101010101;
        key_2 = hdr.ethernet.etherType + 16w65526;
    }
    @hidden table tbl_matchonexprsbmv2l64 {
        actions = {
            matchonexprsbmv2l64();
        }
        const default_action = matchonexprsbmv2l64();
    }
    apply {
        tbl_matchonexprsbmv2l64.apply();
        t1_0.apply();
    }
}

control egressImpl(inout headers_t hdr, inout metadata_t meta, inout standard_metadata_t stdmeta) {
    apply {
    }
}

control updateChecksum(inout headers_t hdr, inout metadata_t meta) {
    apply {
    }
}

control deparserImpl(packet_out packet, in headers_t hdr) {
    apply {
        packet.emit<ethernet_t>(hdr.ethernet);
    }
}

V1Switch<headers_t, metadata_t>(parserImpl(), verifyChecksum(), ingressImpl(), egressImpl(), updateChecksum(), deparserImpl()) main;

